// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/init.h>
#include <common/system.h>
#include <compat/compat.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <kernel/context.h>
#include <node/context.h>
#include <node/interface_ui.h>
#include <node/warnings.h>
#include <noui.h>
#include <util/check.h>
#include <util/exception.h>
#include <util/signalinterrupt.h>
#include <util/strencodings.h>
#include <util/syserror.h>
#include <util/threadnames.h>
#include <util/tokenpipe.h>
#include <util/translation.h>

#include <any>
#include <functional>
#include <optional>
#include <db_cxx.h>

using node::NodeContext;

const TranslateFn G_TRANSLATION_FUN{nullptr};

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/** Daemonize the process using traditional double-fork method.
 * Implements: double-fork, setsid, umask(0), chdir("/"), redirect stdin/stdout/stderr to /dev/null.
 *
 * @param err Reference to store error message if daemonization fails.
 * @returns true on success (returns in child process), false on error (returns in parent process).
 */
static bool Daemonize(bilingual_str& err)
{
    // First fork
    pid_t pid = fork();
    if (pid < 0) {
        err = Untranslated(strprintf("fork() failed: %s", SysErrorString(errno)));
        return false;
    }
    if (pid > 0) {
        // Parent process exits immediately
        exit(EXIT_SUCCESS);
    }

    // First child process: create new session
    if (setsid() < 0) {
        exit(EXIT_FAILURE); // Can't return to parent, so exit
    }

    // Second fork to ensure we're not a session leader
    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE); // Can't return to parent, so exit
    }
    if (pid > 0) {
        // First child exits
        exit(EXIT_SUCCESS);
    }

    // Second child: set umask, change directory, redirect stdio
    umask(0);

    if (chdir("/") != 0) {
        exit(EXIT_FAILURE); // Can't return to parent, so exit
    }

    // Redirect stdin, stdout, stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        exit(EXIT_FAILURE); // Can't return to parent, so exit
    }

    if (dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        exit(EXIT_FAILURE); // Can't return to parent, so exit
    }

    if (fd > STDERR_FILENO) {
        close(fd);
    }

    // Success: second child process continues running as daemon
    return true;
}
#endif

static bool ParseArgs(NodeContext& node, int argc, char* argv[])
{
    ArgsManager& args{*Assert(node.args)};
    // If Qt is used, parameters/bitcoin.conf are parsed in qt/bitcoin.cpp's main()
    SetupServerArgs(args, node.init->canListenIpc());
    std::string error;
    if (!args.ParseParameters(argc, argv, error)) {
        return InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
    }

    if (auto error = common::InitConfig(args)) {
        return InitError(error->message, error->details);
    }

    // Error out when loose non-argument tokens are encountered on command line
    for (int i = 1; i < argc; i++) {
        if (!IsSwitchChar(argv[i][0])) {
            return InitError(Untranslated(strprintf("Command line contains unexpected token '%s', see bitcoind -h for a list of options.", argv[i])));
        }
    }
    return true;
}

static bool ProcessInitCommands(ArgsManager& args)
{
    // Process help and version before taking care about datadir
    if (HelpRequested(args) || args.GetBoolArg("-version", false)) {
        std::string strUsage = CLIENT_NAME " daemon version " + FormatFullVersion() + "\n";

        if (args.GetBoolArg("-version", false)) {
            strUsage += FormatParagraph(LicenseInfo());
        } else {
            strUsage += "\n"
                "The " CLIENT_NAME " daemon (bitcoind) is a headless program that connects to the Bitcoin network to validate and relay transactions and blocks, as well as relaying addresses.\n\n"
                "It provides the backbone of the Bitcoin network and its RPC, REST and ZMQ services can provide various transaction, block and address-related services.\n\n"
                "There is an optional wallet component which provides transaction services.\n\n"
                "It can be used in a headless environment or as part of a server setup.\n"
                "\n"
                "Usage: bitcoind [options]\n"
                "\n";
            strUsage += args.GetHelpMessage();
        }

        tfm::format(std::cout, "%s", strUsage);
        return true;
    }

    return false;
}

static bool AppInit(NodeContext& node)
{
    LogPrintf("Q-BitX node starting up... Welcome to the future of post-quantum money.\n");
    bool fRet = false;
    ArgsManager& args = *Assert(node.args);

    std::any context{&node};
    try
    {
        // -server defaults to true for bitcoind but not for the GUI so do this here
        args.SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging(args);
        InitParameterInteraction(args);
        if (!AppInitBasicSetup(args, node.exit_status)) {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitParameterInteraction(args)) {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }

        node.warnings = std::make_unique<node::Warnings>();

        node.kernel = std::make_unique<kernel::Context>();
        node.ecc_context = std::make_unique<ECC_Context>();
        if (!AppInitSanityChecks(*node.kernel))
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }

        if (args.GetBoolArg("-daemon", DEFAULT_DAEMON) || args.GetBoolArg("-daemonwait", DEFAULT_DAEMONWAIT)) {
#ifdef WIN32
            return InitError(Untranslated("-daemon is not supported on this operating system"));
#else
            bilingual_str err;
            if (!Daemonize(err)) {
                return InitError(err);
            }
            // Daemonized child process continues here
#endif // WIN32
        }
        // Lock critical directories after daemonization
        if (!AppInitLockDirectories())
        {
            // If locking a directory failed, exit immediately
            return false;
        }
        fRet = AppInitInterfaces(node) && AppInitMain(node);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
    }
    return fRet;
}

MAIN_FUNCTION
{
#ifdef WIN32
    common::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif

    NodeContext node;
    int exit_status;
    std::unique_ptr<interfaces::Init> init = interfaces::MakeNodeInit(node, argc, argv, exit_status);
    if (!init) {
        return exit_status;
    }

    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    util::ThreadSetInternalName("init");

    // Interpret command line arguments
    ArgsManager& args = *Assert(node.args);
    if (!ParseArgs(node, argc, argv)) return EXIT_FAILURE;
    // Process early info return commands such as -help or -version
    if (ProcessInitCommands(args)) return EXIT_SUCCESS;

    // Start application
    if (!AppInit(node) || !Assert(node.shutdown_signal)->wait()) {
        node.exit_status = EXIT_FAILURE;
    }
    Interrupt(node);
    Shutdown(node);

    return node.exit_status;
}
