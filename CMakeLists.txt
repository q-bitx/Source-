# Copyright (c) 2025-present The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Ubuntu 22.04 LTS Jammy Jellyfish, https://wiki.ubuntu.com/Releases, EOSS in June 2027:
#  - CMake 3.22.1, https://packages.ubuntu.com/jammy/cmake
#
# Centos Stream 9, https://www.centos.org/cl-vs-cs/#end-of-life, EOL in May 2027:
#  - CMake 3.26.5, https://mirror.stream.centos.org/9-stream/AppStream/x86_64/os/Packages/
cmake_minimum_required(VERSION 3.22)
project(QBitX)

# include(AddWindowsResources)
# include(CheckSourceCompilesWithFlags)
# configure_file(${CMAKE_SOURCE_DIR}/src/cmake/bitcoin-build-config.h.in ${CMAKE_BINARY_DIR}/config/bitcoin-build-config.h)

# configure_file(${CMAKE_SOURCE_DIR}/src/cmake/bitcoin-build-config.h.in ${CMAKE_BINARY_DIR}/config/bitcoin-build-config.h)

include(CTest)
enable_testing()

include_directories(${CMAKE_SOURCE_DIR}/contrib/db4)
link_directories(${CMAKE_SOURCE_DIR}/contrib/db4)
include_directories(${CMAKE_SOURCE_DIR}/src/common) #posle
#include_directories(${CMAKE_SOURCE_DIR}/src/secp-install/include)
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/crypto
   # ${PROJECT_SOURCE_DIR}/src/script #2
   # ${PROJECT_SOURCE_DIR}/src/support #2
   # ${PROJECT_SOURCE_DIR}/src/util #2
   # ${PROJECT_SOURCE_DIR}/src/univalue/include #2
   # ${LIBEVENT_INCLUDE_DIRS}  #2
)

set(BUILD_DESC "qbitx-dev")
set(BUILD_SUFFIX "")

# Define ENABLE_WALLET option before configure_file so it's available for config header
option(ENABLE_WALLET "Enable wallet functionality" ON)

configure_file(
  ${CMAKE_SOURCE_DIR}/src/bitcoin-build-config.h.in
  ${CMAKE_BINARY_DIR}/bitcoin-build-config.h
  @ONLY
)
configure_file(
  ${CMAKE_SOURCE_DIR}/src/bitcoin-build-info.h.in
  ${CMAKE_BINARY_DIR}/bitcoin-build-info.h
  @ONLY
)
include_directories("${CMAKE_BINARY_DIR}")
#include "common/system.h" #posle
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
option(BUILD_BITCOIN_TESTS "Build Bitcoin tests" OFF)
option(BUILD_BITCOIN_UTILS "Build Bitcoin utils" OFF)

add_subdirectory(crypto/dilithium)

add_subdirectory(src/univalue)
include_directories(src/univalue/include)
#find_package(univalue REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
include_directories(${LIBEVENT_INCLUDE_DIRS})
link_directories(${LIBEVENT_LIBRARY_DIRS})

add_executable(qbitx
    src/main.cpp
    src/common/system.cpp
    src/crypto/dilithium/fips202.c
    src/script/pq_script.cpp
    src/crypto/dilithium_wrapper.cpp
    src/node/chainclient_min.cpp
    src/util/exception.cpp
    src/util/string.cpp
    src/util/time.cpp
    src/util/threadnames.cpp
    src/util/fs.cpp
    src/logging.cpp
    src/util/check.cpp
    src/common/args.cpp       
    src/noui.cpp          
    src/init.cpp
    src/init/common.cpp
    $<$<NOT:$<BOOL:${ENABLE_WALLET}>>:src/wallet/walletdb_stub.cpp>
    src/node/interfaces.cpp
    src/rpc/node.cpp
    src/wallet/pqsign.cpp
    src/wallet/pqkey.cpp
    src/node/coin.cpp
    src/wallet/wallet.cpp
    src/common/interfaces.cpp
    src/script/op_pqchecksig.cpp
    src/crypto/dilithium.cpp
    src/node/mini_miner.cpp
    src/wallet/transaction.cpp
    src/wallet/migrate.cpp
    src/wallet/context.cpp
    src/wallet/scriptpubkeyman.cpp
    src/interfaces/scheduler.cpp
   # src/wallet/walletdb.cpp
   # src/wallet/bdb.cpp
   # src/wallet/db.cpp
   # src/wallet/sqlite.cpp
    src/crypto/aes.cpp
    src/interfaces/scheduler.cpp
    src/kernel/checks.cpp
    src/node/coins_view_args.cpp
    src/node/context.cpp
    src/node/warnings.cpp
    src/kernel/context.cpp
    src/wallet/crypter.cpp
    src/util/syserror.cpp
    src/util/strencodings.cpp
    src/crypto/hex_base.cpp
    src/util/signalinterrupt.cpp
    src/clientversion.cpp
    src/common/init.cpp
    src/common/settings.cpp
    src/util/chaintype.cpp
    src/interfaces/init.cpp
    src/node/interface_ui.cpp
    src/util/batchpriority.cpp
    src/chainparamsbase.cpp
    src/chain.cpp
    src/httprpc.cpp
    src/httpserver.cpp
    src/util/ecc_context.cpp
    src/kernel/chain.cpp
    src/scheduler.cpp
    src/banman.cpp
    src/validation.cpp
    src/net.cpp
    src/addrman.cpp
    src/util/fs_helpers.cpp
    src/netaddress.cpp
    src/protocol.cpp
    src/netgroup.cpp
    src/crypto/sha256.cpp
    src/util/asmap.cpp
    src/crypto/sha256_sse4.cpp
    src/streams.cpp
    src/crypto/sha3.cpp
    src/uint256.cpp
    src/validationinterface.cpp
    src/coins.cpp
    src/node/blockstorage.cpp
    src/consensus/tx_check.cpp
    src/txmempool.cpp
    src/policy/policy.cpp
    src/policy/feerate.cpp
    src/consensus/tx_verify.cpp
    src/primitives/transaction.cpp
    src/script/interpreter.cpp
    src/arith_uint256.cpp
    src/script/standard.cpp
    src/script/script.cpp
    src/random.cpp
    src/util/threadinterrupt.cpp
    src/crypto/siphash.cpp
    src/hash.cpp
    src/crypto/ripemd160.cpp
    src/base58.cpp
    src/crypto/chacha20.cpp
    src/txdb.cpp
    src/pow.cpp
    src/merkleblock.cpp
    src/support/cleanse.cpp
    src/crypto/hmac_sha512.cpp
    src/crypto/sha512.cpp
    src/compressor.cpp
    src/common/bloom.cpp
    src/primitives/block.cpp
    src/policy/truc_policy.cpp
    src/netbase.cpp
    src/torcontrol.cpp
    src/net_processing.cpp
    src/util/tokenpipe.cpp
    src/index/base.cpp
    src/index/coinstatsindex.cpp
    src/index/txindex.cpp
    src/dbwrapper.cpp
    src/txdb.cpp
    src/rpc/server.cpp
    src/rest.cpp
    src/crypto/muhash.cpp
    src/mapport.cpp
    src/policy/fees.cpp
    src/node/chainstate.cpp
    src/util/thread.cpp
    src/wallet/init.cpp
    src/rpc/blockchain.cpp
    src/rpc/fees.cpp
    src/rpc/mempool.cpp
    src/rpc/mining.cpp
    src/rpc/net.cpp
    src/rpc/pq.cpp
    src/rpc/rawtransaction.cpp
    src/rpc/signmessage.cpp
    src/rpc/txoutproof.cpp
    src/rpc/util.cpp
    src/node/chainstate.cpp
    src/core_write.cpp
    src/core_read.cpp
    src/rpc/server_util.cpp
    src/key.cpp
    src/bip324.cpp
    src/chainparams.cpp
    src/deploymentinfo.cpp 
    src/pubkey.cpp
    src/support/lockedpool.cpp
    src/blockfilter.cpp
    src/kernel/chainparams_main.cpp
    src/consensus/merkle.cpp
    src/util/bytevectorhash.cpp
    src/kernel/cs_main.cpp
    src/addrdb.cpp
    src/util/sock.cpp
    src/netbase.cpp
    src/torcontrol.cpp
    src/rpc/request.cpp
    src/script/solver.cpp
    src/outputtype.cpp
    src/addresstype.cpp
    src/key_io.cpp
    src/wallet/walletutil.cpp
    src/blockencodings.cpp
    src/bech32.cpp
    src/net_types.cpp
    src/common/config.cpp
    src/node/transaction.cpp
    src/common/signmessage.cpp
    src/common/messages.cpp
    src/crypto/hkdf_sha256_32.cpp
    src/script/sign.cpp
    src/script/miniscript.cpp
    src/crypto/hmac_sha256.cpp
    src/crypto/chacha20poly1305.cpp
    src/script/script_error.cpp
    src/psbt.cpp
    src/flatfile.cpp
    src/crypto/poly1305.cpp
    src/script/descriptor.cpp
    src/script/signingprovider.cpp
    src/util/hasher.cpp
    src/policy/settings.cpp
    src/util/readwritefile.cpp
    src/node/timeoffsets.cpp
    src/script/parsing.cpp
    src/node/miner.cpp
    src/util/bip32.cpp
    src/versionbits.cpp
    src/node/connection_types.cpp
    src/net_permissions.cpp
    src/rpc/rawtransaction_util.cpp
    src/util/moneystr.cpp
    src/node/psbt.cpp
    src/node/mempool_persist.cpp
    src/node/mempool_persist_args.cpp
    src/policy/rbf.cpp
    src/policy/packages.cpp
    src/i2p.cpp
    src/kernel/mempool_removal_reason.cpp
    src/signet.cpp
    src/index/blockfilterindex.cpp
    src/kernel/coinstats.cpp
    src/util/rbf.cpp
    src/util/feefrac.cpp
    src/util/serfloat.cpp
    src/node/abort.cpp
    src/common/netif.cpp
    src/common/pcp.cpp
    src/node/txdownloadman_impl.cpp
    src/node/txreconciliation.cpp
    src/node/database_args.cpp
    src/txorphanage.cpp
    src/txrequest.cpp
    src/headerssync.cpp
    src/randomenv.cpp
    src/node/eviction.cpp
    src/crypto/sha1.cpp
    src/policy/ephemeral_policy.cpp
    src/kernel/disconnected_transactions.cpp
    src/node/utxo_snapshot.cpp
    src/script/sigcache.cpp
    src/node/peerman_args.cpp
    src/node/blockmanager_args.cpp
    src/node/chainstatemanager_args.cpp
    src/node/mempool_args.cpp
    src/node/miner.cpp
    src/rpc/output_script.cpp
    src/policy/fees_args.cpp
    src/node/kernel_notifications.cpp
    src/node/caches.cpp
)

target_link_libraries(qbitx PRIVATE pqcrystals m univalue m  ${LIBEVENT_LIBRARIES} ${LEVELDB_LIB}
    event_pthreads )


# LevelDB
include_directories(/usr/include/leveldb)
find_library(LEVELDB_LIB leveldb REQUIRED)
target_link_libraries(qbitx PRIVATE ${LEVELDB_LIB})
target_link_libraries(qbitx PRIVATE event_pthreads)
target_link_libraries(qbitx PRIVATE db_cxx)

# Wallet support
if(ENABLE_WALLET)
  target_compile_definitions(qbitx PRIVATE ENABLE_WALLET=1)
  target_link_libraries(qbitx PRIVATE bitcoin_wallet)
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed.")
endif()

if(POLICY CMP0171)
  # `codegen` is a reserved target name.
  # See: https://cmake.org/cmake/help/latest/policy/CMP0171.html
  cmake_policy(SET CMP0171 NEW)
endif()

include_directories(${PROJECT_SOURCE_DIR}/src/crypto/dilithium)
#=============================
# Project / Package metadata
#=============================
set(CLIENT_NAME "Bitcoin Core")
set(CLIENT_VERSION_MAJOR 29)
set(CLIENT_VERSION_MINOR 99)
set(CLIENT_VERSION_BUILD 0)
set(CLIENT_VERSION_RC 0)
set(CLIENT_VERSION_IS_RELEASE "false")
set(COPYRIGHT_YEAR "2025")

# During the enabling of the CXX and CXXOBJ languages, we modify
# CMake's compiler/linker invocation strings by appending the content
# of the user-defined `APPEND_*` variables, which allows overriding
# any flag. We also ensure that the APPEND_* flags are considered
# during CMake's tests, which use the `try_compile()` command.
#
# CMake's docs state that the `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`
# variable "is meant to be set by CMake's platform information modules
# for the current toolchain, or by a toolchain file." We do our best
# to set it before the `project()` command.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  CMAKE_CXX_COMPILE_OBJECT
  CMAKE_OBJCXX_COMPILE_OBJECT
  CMAKE_CXX_LINK_EXECUTABLE
)

project(BitcoinCore
  VERSION ${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_BUILD}
  DESCRIPTION "Bitcoin client software"
  HOMEPAGE_URL "https://bitcoincore.org/"
  LANGUAGES NONE
)

set(CLIENT_VERSION_STRING ${PROJECT_VERSION})
if(CLIENT_VERSION_RC GREATER 0)
  string(APPEND CLIENT_VERSION_STRING "rc${CLIENT_VERSION_RC}")
endif()

set(COPYRIGHT_HOLDERS "The %s developers")
set(COPYRIGHT_HOLDERS_FINAL "The ${CLIENT_NAME} developers")
set(CLIENT_BUGREPORT "https://github.com/bitcoin/bitcoin/issues")

#=============================
# Language setup
#=============================
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_HOST_APPLE)
  # We do not use the install_name_tool when cross-compiling for macOS.
  # So disable this tool check in further enable_language() commands.
  set(CMAKE_PLATFORM_HAS_INSTALLNAME FALSE)
endif()
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/module)
include(ProcessConfigurations)

# Flatten static lib dependencies.
# Without this, if libfoo.a depends on libbar.a, libfoo's objects can't begin
# to be compiled until libbar.a has been created.
if (NOT DEFINED CMAKE_OPTIMIZE_DEPENDENCIES)
  set(CMAKE_OPTIMIZE_DEPENDENCIES TRUE)
endif()

#=============================
# Configurable options
#=============================
include(CMakeDependentOption)
# When adding a new option, end the <help_text> with a full stop for consistency.
    # Use of the `unofficial::` namespace is a vcpkg package manager convention.
   
 find_package(SQLite3 3.7.17 REQUIRED)
option(WITH_BDB "Enable Berkeley DB (BDB) wallet support." OFF)
cmake_dependent_option(WARN_INCOMPATIBLE_BDB "Warn when using a Berkeley DB (BDB) version other than 4.8." ON "WITH_BDB" OFF)
if(WITH_BDB)
  find_package(BerkeleyDB 4.8 MODULE REQUIRED)
  set(USE_BDB ON)
  if(NOT BerkeleyDB_VERSION VERSION_EQUAL 4.8)
    message(WARNING "Found Berkeley DB (BDB) other than 4.8.\n"
                    "BDB (legacy) wallets opened by this build will not be portable!"
    )
    if(WARN_INCOMPATIBLE_BDB)
      message(WARNING "If this is intended, pass \"-DWARN_INCOMPATIBLE_BDB=OFF\".\n"
                      "Passing \"-DWITH_BDB=OFF\" will suppress this warning."
      )
    endif()
  endif()
endif()

cmake_dependent_option(BUILD_WALLET_TOOL "Build bitcoin-wallet tool." OFF "ENABLE_WALLET" STRENGTH)

# ENABLE_WALLET option is now defined earlier (before configure_file)
option(ENABLE_HARDENING "Attempt to harden the resulting executables." ON)
option(REDUCE_EXPORTS "Attempt to reduce exported symbols in the resulting executables." OFF)
option(WERROR "Treat compiler warnings as errors." OFF)
option(WITH_CCACHE "Attempt to use ccache for compiling." ON)

option(WITH_ZMQ "Enable ZMQ notifications." OFF)
if(WITH_ZMQ)
  find_package(ZeroMQ 4.0.0 MODULE REQUIRED)
endif()

option(WITH_USDT "Enable tracepoints for Userspace, Statically Defined Tracing." OFF)
if(WITH_USDT)
  find_package(USDT MODULE REQUIRED)
endif()

cmake_dependent_option(ENABLE_EXTERNAL_SIGNER "Enable external signer support." ON "NOT WIN32" OFF)

cmake_dependent_option(WITH_QRENCODE "Enable QR code support." ON "BUILD_GUI" OFF)
if(WITH_QRENCODE)
  find_package(QRencode MODULE REQUIRED)
  set(USE_QRCODE TRUE)
endif()

cmake_dependent_option(WITH_DBUS "Enable DBus support." ON "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND BUILD_GUI" OFF)
if(WITH_MULTIPROCESS)
  find_package(Libmultiprocess REQUIRED COMPONENTS Lib)
  find_package(LibmultiprocessNative REQUIRED COMPONENTS Bin
    NAMES Libmultiprocess
  )
endif()

cmake_dependent_option(BUILD_GUI_TESTS "Build test_bitcoin-qt executable." ON "BUILD_GUI;BUILD_TESTS" OFF)
if(BUILD_GUI)
  set(qt_components Core Gui Widgets LinguistTools)
  if(ENABLE_WALLET)
    list(APPEND qt_components Network)
  endif()
  if(WITH_DBUS)
    list(APPEND qt_components DBus)
    set(USE_DBUS TRUE)
  endif()
  if(BUILD_GUI_TESTS)
    list(APPEND qt_components Test)
  endif()
  find_package(Qt 6.2 MODULE REQUIRED
    COMPONENTS ${qt_components}
  )
  unset(qt_components)
endif()

option(BUILD_BENCH "Build bench_bitcoin executable." OFF)
option(BUILD_FUZZ_BINARY "Build fuzz binary." OFF)
option(BUILD_FOR_FUZZING "Build for fuzzing. Enabling this will disable all other targets and override BUILD_FUZZ_BINARY." OFF)

option(INSTALL_MAN "Install man pages." ON)

set(APPEND_CPPFLAGS "" CACHE STRING "Preprocessor flags that are appended to the command line after all other flags added by the build system. This variable is intended for debugging and special builds.")
set(APPEND_CFLAGS "" CACHE STRING "C compiler flags that are appended to the command line after all other flags added by the build system. This variable is intended for debugging and special builds.")
set(APPEND_CXXFLAGS "" CACHE STRING "(Objective) C++ compiler flags that are appended to the command line after all other flags added by the build system. This variable is intended for debugging and special builds.")
set(APPEND_LDFLAGS "" CACHE STRING "Linker flags that are appended to the command line after all other flags added by the build system. This variable is intended for debugging and special builds.")
# Appending to this low-level rule variables is the only way to
# guarantee that the flags appear at the end of the command line.
string(APPEND CMAKE_CXX_COMPILE_OBJECT " ${APPEND_CPPFLAGS} ${APPEND_CXXFLAGS}")
string(APPEND CMAKE_CXX_CREATE_SHARED_LIBRARY " ${APPEND_LDFLAGS}")
string(APPEND CMAKE_CXX_LINK_EXECUTABLE " ${APPEND_LDFLAGS}")

set(configure_warnings)

include(CheckLinkerSupportsPIE)
check_linker_supports_pie(configure_warnings)

# The core_interface library aims to encapsulate common build flags.
# It is a usage requirement for all targets except for secp256k1, which
# gets its flags by other means.
add_library(core_interface INTERFACE)
add_library(core_interface_relwithdebinfo INTERFACE)
add_library(core_interface_debug INTERFACE)
target_link_libraries(core_interface INTERFACE
  $<$<CONFIG:RelWithDebInfo>:core_interface_relwithdebinfo>
  $<$<CONFIG:Debug>:core_interface_debug>
)


if(BUILD_FOR_FUZZING)
  message(WARNING "BUILD_FOR_FUZZING=ON will disable all other targets and force BUILD_FUZZ_BINARY=ON.")
  set(BUILD_DAEMON OFF)
  set(BUILD_CLI OFF)
  set(BUILD_TX OFF)
  set(BUILD_UTIL OFF)
  set(BUILD_UTIL_CHAINSTATE OFF)
  set(BUILD_KERNEL_LIB OFF)
  set(BUILD_WALLET_TOOL OFF)
  set(BUILD_GUI OFF)
  set(ENABLE_EXTERNAL_SIGNER OFF)
  set(WITH_ZMQ OFF)
  set(BUILD_TESTS OFF)
  set(BUILD_GUI_TESTS OFF)
  set(BUILD_BENCH OFF)
  set(BUILD_FUZZ_BINARY ON)

  target_compile_definitions(core_interface INTERFACE
    FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  )
endif()

include(TryAppendCXXFlags)
include(TryAppendLinkerFlag)

# Redefine/adjust per-configuration flags.
target_compile_definitions(core_interface_debug INTERFACE
  DEBUG
  DEBUG_LOCKORDER
  DEBUG_LOCKCONTENTION
  RPC_DOC_CHECK
  ABORT_ON_FAILED_ASSUME
)

if(WIN32)
  #[=[
  This build system supports two ways to build binaries for Windows.

  1. Building on Windows using MSVC.
  Implementation notes:
  - /DWIN32 and /D_WINDOWS definitions are included into the CMAKE_CXX_FLAGS_INIT
    and CMAKE_CXX_FLAGS_INIT variables by default.
  - A run-time library is selected using the CMAKE_MSVC_RUNTIME_LIBRARY variable.
  - MSVC-specific options, for example, /Zc:__cplusplus, are additionally required.

  2. Cross-compiling using MinGW.
  Implementation notes:
  - WIN32 and _WINDOWS definitions must be provided explicitly.
  - A run-time library must be specified explicitly using _MT definition.
  ]=]

  target_compile_definitions(core_interface INTERFACE
    _WIN32_WINNT=0x0A00
    _WIN32_IE=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )

  if(MSVC)
    if(VCPKG_TARGET_TRIPLET MATCHES "-static")
      set(msvc_library_linkage "")
    else()
      set(msvc_library_linkage "DLL")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>${msvc_library_linkage}")
    unset(msvc_library_linkage)

    target_compile_definitions(core_interface INTERFACE
      _UNICODE;UNICODE
    )
    target_compile_options(core_interface INTERFACE
      /utf-8
      /Zc:preprocessor
      /Zc:__cplusplus
      /sdl
    )
    # Improve parallelism in MSBuild.
    # See: https://devblogs.microsoft.com/cppblog/improved-parallelism-in-msbuild/.
    list(APPEND CMAKE_VS_GLOBALS "UseMultiToolTask=true")
  endif()

  if(MINGW)
    target_compile_definitions(core_interface INTERFACE
      WIN32
      _WINDOWS
      _MT
    )
    # Avoid the use of aligned vector instructions when building for Windows.
    # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412.
    try_append_cxx_flags("-Wa,-muse-unaligned-vector-move" TARGET core_interface SKIP_LINK)
    try_append_linker_flag("-static" TARGET core_interface)
    # We support Windows 10+, however it's not possible to set these values accordingly,
    # due to a bug in mingw-w64. See https://sourceforge.net/p/mingw-w64/bugs/968/.
    # As a best effort, target Windows 8.
    try_append_linker_flag("-Wl,--major-subsystem-version,6" TARGET core_interface)
    try_append_linker_flag("-Wl,--minor-subsystem-version,2" TARGET core_interface)
  endif()

  # Workaround producing large object files, which cannot be handled by the assembler.
  # More likely to happen with no, or lower levels of optimisation.
  # See discussion in https://github.com/itcoin/itcoin/issues/28109.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    try_append_cxx_flags("/bigobj" TARGET core_interface_debug SKIP_LINK)
  else()
    try_append_cxx_flags("-Wa,-mbig-obj" TARGET core_interface_debug SKIP_LINK)
  endif()
endif()

# Use 64-bit off_t on 32-bit Linux.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  # Ensure 64-bit offsets are used for filesystem accesses for 32-bit compilation.
  target_compile_definitions(core_interface INTERFACE
    _FILE_OFFSET_BITS=64
  )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_compile_definitions(core_interface INTERFACE OBJC_OLD_DISPATCH_PROTOTYPES=0)
  # These flags are specific to ld64, and may cause issues with other linkers.
  # For example: GNU ld will interpret -dead_strip as -de and then try and use
  # "ad_strip" as the symbol for the entry point.
  try_append_linker_flag("-Wl,-dead_strip" TARGET core_interface)
  try_append_linker_flag("-Wl,-dead_strip_dylibs" TARGET core_interface)
  if(CMAKE_HOST_APPLE)
    try_append_linker_flag("-Wl,-headerpad_max_install_names" TARGET core_interface)
  endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(core_interface INTERFACE
  Threads::Threads
)

# Define sanitize_interface with -fsanitize flags intended to apply to all
# libraries and executables.
add_library(sanitize_interface INTERFACE)
target_link_libraries(core_interface INTERFACE sanitize_interface)
if(SANITIZERS)
  # Transform list of sanitizers into -fsanitize flags, replacing "fuzzer" with
  # "fuzzer-no-link" in sanitize_interface flags, and moving "fuzzer" to
  # fuzzer_interface flags. If -DSANITIZERS=fuzzer is specified, the fuzz test
  # binary should be built with -fsanitize=fuzzer (so it can use libFuzzer's
  # main function), but libraries should be built with -fsanitize=fuzzer-no-link
  # (so they can be linked into other executables that have their own main
  # functions).
  string(REGEX REPLACE "(^|,)fuzzer($|,)" "\\1fuzzer-no-link\\2" sanitize_opts "${SANITIZERS}")
  set(fuzz_flag "")
  if(NOT sanitize_opts STREQUAL SANITIZERS)
    set(fuzz_flag "-fsanitize=fuzzer")
  endif()

  # First check if the compiler accepts flags. If an incompatible pair like
  # -fsanitize=address,thread is used here, this check will fail. This will also
  # fail if a bad argument is passed, e.g. -fsanitize=undfeined
  try_append_cxx_flags("-fsanitize=${sanitize_opts}" TARGET sanitize_interface
    RESULT_VAR cxx_supports_sanitizers
    SKIP_LINK
  )
  if(NOT cxx_supports_sanitizers)
    message(FATAL_ERROR "Compiler did not accept requested flags.")
  endif()

  # Some compilers (e.g. GCC) require additional libraries like libasan,
  # libtsan, libubsan, etc. Make sure linking still works with the sanitize
  # flag. This is a separate check so we can give a better error message when
  # the sanitize flags are supported by the compiler but the actual sanitizer
  # libs are missing.
  try_append_linker_flag("-fsanitize=${sanitize_opts}" VAR SANITIZER_LDFLAGS
    SOURCE "
      #include <cstdint>
      #include <cstddef>
      extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return 0; }
      int main() { return 0; }
    "
    RESULT_VAR linker_supports_sanitizers
    NO_CACHE_IF_FAILED
  )
  if(NOT linker_supports_sanitizers)
    message(FATAL_ERROR "Linker did not accept requested flags, you are missing required libraries.")
  endif()
endif()
target_link_options(sanitize_interface INTERFACE ${SANITIZER_LDFLAGS})

# Define fuzzer_interface with flags intended to apply to the fuzz test binary,
# and perform a test compilation to determine correct value of
# FUZZ_BINARY_LINKS_WITHOUT_MAIN_FUNCTION.
if(BUILD_FUZZ_BINARY)
  include(CheckSourceCompilesWithFlags)
  check_cxx_source_compiles_with_flags("
      #include <cstdint>
      #include <cstddef>
      extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return 0; }
      // No main() function.
    " FUZZ_BINARY_LINKS_WITHOUT_MAIN_FUNCTION
    LDFLAGS ${SANITIZER_LDFLAGS} ${fuzz_flag}
    LINK_LIBRARIES ${FUZZ_LIBS}
  )
  add_library(fuzzer_interface INTERFACE)
  target_link_options(fuzzer_interface INTERFACE ${fuzz_flag})
  target_link_libraries(fuzzer_interface INTERFACE ${FUZZ_LIBS})
endif()

include(AddBoostIfNeeded)
add_boost_if_needed()

if(BUILD_DAEMON OR BUILD_GUI OR BUILD_CLI OR BUILD_TESTS OR BUILD_BENCH OR BUILD_FUZZ_BINARY)
  find_package(Libevent 2.1.8 MODULE REQUIRED)
endif()

include(cmake/introspection.cmake)

include(cmake/ccache.cmake)

add_library(warn_interface INTERFACE)
target_link_libraries(core_interface INTERFACE warn_interface)
if(MSVC)
  try_append_cxx_flags("/W3" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4018" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4146" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4244" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4267" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4715" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("/wd4805" TARGET warn_interface SKIP_LINK)
  target_compile_definitions(warn_interface INTERFACE
    _CRT_SECURE_NO_WARNINGS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
  )
else()
  try_append_cxx_flags("-Wall" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wextra" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wgnu" TARGET warn_interface SKIP_LINK)
  # Some compilers will ignore -Wformat-security without -Wformat, so just combine the two here.
  try_append_cxx_flags("-Wformat -Wformat-security" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wvla" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wshadow-field" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wthread-safety" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wloop-analysis" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wredundant-decls" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wunused-member-function" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wdate-time" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wconditional-uninitialized" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wduplicated-branches" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wduplicated-cond" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wlogical-op" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Woverloaded-virtual" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wsuggest-override" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wimplicit-fallthrough" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wunreachable-code" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wdocumentation" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wself-assign" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wbidi-chars=any" TARGET warn_interface SKIP_LINK)
  try_append_cxx_flags("-Wundef" TARGET warn_interface SKIP_LINK)

  # Some compilers (gcc) ignore unknown -Wno-* options, but warn about all
  # unknown options if any other warning is produced. Test the -Wfoo case, and
  # set the -Wno-foo case if it works.
  try_append_cxx_flags("-Wunused-parameter" TARGET warn_interface SKIP_LINK
    IF_CHECK_PASSED "-Wno-unused-parameter"
  )
endif()

configure_file(cmake/script/Coverage.cmake Coverage.cmake USE_SOURCE_PERMISSIONS COPYONLY)
configure_file(cmake/script/CoverageFuzz.cmake CoverageFuzz.cmake USE_SOURCE_PERMISSIONS COPYONLY)
configure_file(cmake/script/CoverageInclude.cmake.in CoverageInclude.cmake USE_SOURCE_PERMISSIONS @ONLY)
configure_file(cmake/script/cov_tool_wrapper.sh.in cov_tool_wrapper.sh.in USE_SOURCE_PERMISSIONS COPYONLY)
configure_file(contrib/filter-lcov.py filter-lcov.py USE_SOURCE_PERMISSIONS COPYONLY)

# Don't allow extended (non-ASCII) symbols in identifiers. This is easier for code review.
try_append_cxx_flags("-fno-extended-identifiers" TARGET core_interface SKIP_LINK)

# Avoiding the `-ffile-prefix-map` compiler option because it implies
# `-fcoverage-prefix-map` on Clang or `-fprofile-prefix-map` on GCC,
# which can cause issues with coverage builds, particularly when using
# Clang in the OSS-Fuzz environment due to its use of other options
# and a third party script, or with GCC.
try_append_cxx_flags("-fdebug-prefix-map=A=B" TARGET core_interface SKIP_LINK
  IF_CHECK_PASSED "-fdebug-prefix-map=${PROJECT_SOURCE_DIR}/src=."
)
try_append_cxx_flags("-fmacro-prefix-map=A=B" TARGET core_interface SKIP_LINK
  IF_CHECK_PASSED "-fmacro-prefix-map=${PROJECT_SOURCE_DIR}/src=."
)

# Currently all versions of gcc are subject to a class of bugs, see the
# gccbug_90348 test case (only reproduces on GCC 11 and earlier) and
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=111843. To work around that, set
# -fstack-reuse=none for all gcc builds. (Only gcc understands this flag).
try_append_cxx_flags("-fstack-reuse=none" TARGET core_interface)

if(ENABLE_HARDENING)
  add_library(hardening_interface INTERFACE)
  target_link_libraries(core_interface INTERFACE hardening_interface)
  if(MSVC)
    try_append_linker_flag("/DYNAMICBASE" TARGET hardening_interface)
    try_append_linker_flag("/HIGHENTROPYVA" TARGET hardening_interface)
    try_append_linker_flag("/NXCOMPAT" TARGET hardening_interface)
  else()

    # _FORTIFY_SOURCE requires that there is some level of optimization,
    # otherwise it does nothing and just creates a compiler warning.
    try_append_cxx_flags("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3"
      RESULT_VAR cxx_supports_fortify_source
      SOURCE "int main() {
              # if !defined __OPTIMIZE__ || __OPTIMIZE__ <= 0
                #error
              #endif
              }"
    )
    if(cxx_supports_fortify_source)
      target_compile_options(hardening_interface INTERFACE
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=3
      )
    endif()
    unset(cxx_supports_fortify_source)

    try_append_cxx_flags("-Wstack-protector" TARGET hardening_interface SKIP_LINK)
    try_append_cxx_flags("-fstack-protector-all" TARGET hardening_interface)
    try_append_cxx_flags("-fcf-protection=full" TARGET hardening_interface)

    if(MINGW)
      # stack-clash-protection is a no-op for Windows.
      # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90458 for more details.
    else()
      try_append_cxx_flags("-fstack-clash-protection" TARGET hardening_interface)
    endif()

    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
      if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        try_append_cxx_flags("-mbranch-protection=bti" TARGET hardening_interface SKIP_LINK)
      else()
        try_append_cxx_flags("-mbranch-protection=standard" TARGET hardening_interface SKIP_LINK)
      endif()
    endif()

    try_append_linker_flag("-Wl,--enable-reloc-section" TARGET hardening_interface)
    try_append_linker_flag("-Wl,--dynamicbase" TARGET hardening_interface)
    try_append_linker_flag("-Wl,--nxcompat" TARGET hardening_interface)
    try_append_linker_flag("-Wl,--high-entropy-va" TARGET hardening_interface)
    try_append_linker_flag("-Wl,-z,relro" TARGET hardening_interface)
    try_append_linker_flag("-Wl,-z,now" TARGET hardening_interface)
    try_append_linker_flag("-Wl,-z,separate-code" TARGET hardening_interface)
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      try_append_linker_flag("-Wl,-fixup_chains" TARGET hardening_interface)
    endif()
  endif()
endif()

if(REDUCE_EXPORTS)
  set(CMAKE_CXX_VISIBILITY_PRESET hidden)
  try_append_linker_flag("-Wl,--exclude-libs,ALL" TARGET core_interface)
  try_append_linker_flag("-Wl,-no_exported_symbols" VAR CMAKE_EXE_LINKER_FLAGS)
endif()

if(WERROR)
  if(MSVC)
    set(werror_flag "/WX")
  else()
    set(werror_flag "-Werror")
  endif()
  try_append_cxx_flags(${werror_flag} TARGET core_interface SKIP_LINK RESULT_VAR compiler_supports_werror)
  if(NOT compiler_supports_werror)
    message(FATAL_ERROR "WERROR set but ${werror_flag} is not usable.")
  endif()
  unset(werror_flag)
endif()

# Prefer Unix-style package components over frameworks on macOS.
# This improves compatibility with Python version managers.
set(Python3_FIND_FRAMEWORK LAST CACHE STRING "")
# Search for generic names before more specialized ones. This
# improves compatibility with Python version managers that use shims.
set(Python3_FIND_UNVERSIONED_NAMES FIRST CACHE STRING "")
mark_as_advanced(Python3_FIND_FRAMEWORK Python3_FIND_UNVERSIONED_NAMES)
find_package(Python3 3.10 COMPONENTS Interpreter)
if(Python3_EXECUTABLE)
  set(PYTHON_COMMAND ${Python3_EXECUTABLE})
else()
  list(APPEND configure_warnings
    "Minimum required Python not found. Utils and rpcauth tests are disabled."
  )
endif()

target_compile_definitions(core_interface INTERFACE ${DEPENDS_COMPILE_DEFINITIONS})
target_compile_definitions(core_interface_relwithdebinfo INTERFACE ${DEPENDS_COMPILE_DEFINITIONS_RELWITHDEBINFO})
target_compile_definitions(core_interface_debug INTERFACE ${DEPENDS_COMPILE_DEFINITIONS_DEBUG})

# If the {CXX,LD}FLAGS environment variables are defined during building depends
# and configuring this build system, their content might be duplicated.
if(DEFINED ENV{CXXFLAGS})
  deduplicate_flags(CMAKE_CXX_FLAGS)
endif()
if(DEFINED ENV{LDFLAGS})
  deduplicate_flags(CMAKE_EXE_LINKER_FLAGS)
endif()

#if(BUILD_TESTS)
#  enable_testing()
#endif()

if(BUILD_BITCOIN_TESTS)
  enable_testing()
  add_subdirectory(src/test)
else()
  enable_testing()
  add_subdirectory(src/test/pq)
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
  # have "make test" depend on "make all"
  set(CMAKE_SKIP_TEST_ALL_DEPENDENCY FALSE)
endif()

# TODO: The `CMAKE_SKIP_BUILD_RPATH` variable setting can be deleted
#       in the future after reordering Guix script commands to
#       perform binary checks after the installation step.
# Relevant discussions:
# - https://github.com/hebasto/itcoin/pull/236#issuecomment-2183120953
# - https://github.com/bitcoin/itcoin/pull/30312#issuecomment-2191235833
# NetBSD always requires runtime paths to be set for executables.
if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
else()
  set(CMAKE_SKIP_BUILD_RPATH TRUE)
  set(CMAKE_SKIP_INSTALL_RPATH TRUE)
endif()
add_subdirectory(test)
add_subdirectory(doc)

add_subdirectory(src)

include(cmake/tests.cmake)

include(Maintenance)
setup_split_debug_script()
add_maintenance_targets()
add_windows_deploy_target()
add_macos_deploy_target()
message("=================")
message("Q-BitX Executables:")
message("  qbitx ................................ ON")
# We want all build properties to be encapsulated properly.
include(WarnAboutGlobalProperties)
#add_subdirectory(src/test)
if(ENABLE_WALLET)
  add_subdirectory(src/wallet)
endif()
#add_executable(test_standard src/test/test_standard.cpp)
#target_link_libraries(test_standard ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

#add_test(test_standard test_standard)
#add_test(NAME test_bitcoin COMMAND test_bitcoin)
#include_directories(${CMAKE_SOURCE_DIR}/src/secp-install/include)

#link_directories(${CMAKE_SOURCE_DIR}/src/secp-install/lib)

#target_link_libraries(qbitx PRIVATE secp256k1)
include_directories(${CMAKE_SOURCE_DIR}/src/secp-install/include)

link_directories(${CMAKE_SOURCE_DIR}/src/secp-install/lib)
target_link_libraries(qbitx PRIVATE ${CMAKE_SOURCE_DIR}/src/secp-install/lib/libsecp256k1.a)
#target_link_libraries(qbitx PRIVATE secp256k1)

# -------------------------
# qbitx-cli (RPC client)
# -------------------------
get_target_property(QBITX_SOURCES qbitx SOURCES)

foreach(_mainfile
    "main.cpp"
    "${CMAKE_SOURCE_DIR}/main.cpp"

    "src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/main.cpp"

    "src/bitcoind.cpp"
    "${CMAKE_SOURCE_DIR}/src/bitcoind.cpp"

    "src/qbitx.cpp"
    "${CMAKE_SOURCE_DIR}/src/qbitx.cpp"

    "src/bitcoin.cpp"
    "${CMAKE_SOURCE_DIR}/src/bitcoin.cpp"

    "src/qt/bitcoin.cpp"
    "${CMAKE_SOURCE_DIR}/src/qt/bitcoin.cpp"
)
    list(REMOVE_ITEM QBITX_SOURCES "${_mainfile}")
endforeach()

list(FILTER QBITX_SOURCES EXCLUDE REGEX "(^|/|\\\\)main\\.cpp$")

list(APPEND QBITX_SOURCES
    "${CMAKE_SOURCE_DIR}/src/bitcoin-cli.cpp"
    "${CMAKE_SOURCE_DIR}/src/rpc/client.cpp"
    "${CMAKE_SOURCE_DIR}/src/compat/stdin.cpp"
)

add_executable(qbitx-cli ${QBITX_SOURCES})
set_target_properties(qbitx-cli PROPERTIES OUTPUT_NAME "qbitx-cli")

get_target_property(QBITX_LINK_LIBS qbitx LINK_LIBRARIES)
if (NOT QBITX_LINK_LIBS)
  message(FATAL_ERROR "qbitx LINK_LIBRARIES empty/NOTFOUND. Move qbitx-cli block AFTER all target_link_libraries(qbitx ...).")
endif()
target_link_libraries(qbitx-cli PRIVATE ${QBITX_LINK_LIBS})
