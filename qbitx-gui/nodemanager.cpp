#include "nodemanager.h"
#include "clipathutil.h"
#include "qbitxembedded.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace {

bool debugNodeOutputEnabled()
{
    const QByteArray v = qgetenv("QBITX_GUI_DEBUG_NODE_OUTPUT");
    return !v.isEmpty() && v != "0";
}

} // namespace

NodeManager::NodeManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_stopCliProcess(nullptr)
    , m_dataDir(defaultDataDir())
    , m_startedEmitted(false)
    , m_stopRequested(false)
    , m_shutdownInProgress(false)
    , m_shutdownCompleted(false)
    , m_debugNodeOutput(debugNodeOutputEnabled())
    , m_stdoutLinesSinceSummary(0)
    , m_stderrLinesSinceSummary(0)
    , m_outputSummaryTimer(new QTimer(this))
    , m_gracefulExitTimer(new QTimer(this))
    , m_terminateTimer(new QTimer(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    m_outputSummaryTimer->setInterval(1000);
    connect(m_outputSummaryTimer, &QTimer::timeout, this, &NodeManager::flushOutputSummary);

    m_gracefulExitTimer->setSingleShot(true);
    m_gracefulExitTimer->setInterval(NODE_GRACEFUL_EXIT_MS);
    connect(m_gracefulExitTimer, &QTimer::timeout, this, &NodeManager::onGracefulExitTimeout);

    m_terminateTimer->setSingleShot(true);
    m_terminateTimer->setInterval(TERMINATE_TIMEOUT_MS);
    connect(m_terminateTimer, &QTimer::timeout, this, &NodeManager::onTerminateTimeout);

    connect(m_process, &QProcess::started, this, &NodeManager::onProcessStarted);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NodeManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &NodeManager::onProcessErrorOccurred);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &NodeManager::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &NodeManager::onReadyReadStderr);
}

NodeManager::~NodeManager()
{
    if (!m_shutdownCompleted)
        stopNodeBlocking();
}

QString NodeManager::defaultDataDir()
{
    return QBitXEmbedded::defaultDataDir();
}

QStringList NodeManager::candidateBinaryPaths()
{
    QStringList paths;
    const QString appDir = QCoreApplication::applicationDirPath();

    const auto push = [&paths](const QString &p) {
        const QString cleaned = QDir::cleanPath(p);
        if (!cleaned.isEmpty() && !paths.contains(cleaned))
            paths.append(cleaned);
    };

    push(QDir(appDir).filePath(QStringLiteral("qbitx")));
    push(QDir(appDir).filePath(QStringLiteral("qbitx.exe")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/qbitx")));
    push(QDir(appDir).filePath(QStringLiteral("../../build_wallet2/qbitx")));
    push(QDir::homePath() + QStringLiteral("/pesok/qbx/build/qbitx"));
    push(QDir::homePath() + QStringLiteral("/qbitx_restore/build_wallet2/qbitx"));

#if defined(Q_OS_WIN)
    push(QDir(appDir).filePath(QStringLiteral("../../build/qbitx.exe")));
    push(QDir(appDir).filePath(QStringLiteral("../../build_wallet2/qbitx.exe")));
    push(QDir::homePath() + QStringLiteral("/pesok/qbx/build/qbitx.exe"));
    push(QDir::homePath() + QStringLiteral("/qbitx_restore/build_wallet2/qbitx.exe"));
#endif

    return paths;
}

QString NodeManager::findNodeBinary()
{
    const QStringList candidates = candidateBinaryPaths();
    for (const QString &candidate : candidates) {
        QFileInfo fi(candidate);
        if (!fi.isFile())
            continue;
#if defined(Q_OS_WIN)
        if (fi.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
            continue;
#else
        if (!fi.isExecutable())
            continue;
#endif
        qDebug() << "NodeManager: selected node binary:" << fi.absoluteFilePath();
        return fi.absoluteFilePath();
    }

    qWarning() << "NodeManager: qbitx binary not found. Searched locations:";
    for (const QString &p : candidates)
        qWarning() << "  -" << p;
    return QString();
}

void NodeManager::logDataDirPersistenceState() const
{
    const QString blocksPath = QDir(m_dataDir).filePath(QStringLiteral("blocks"));
    const QString chainstatePath = QDir(m_dataDir).filePath(QStringLiteral("chainstate"));
    const QString debugLogPath = QDir(m_dataDir).filePath(QStringLiteral("debug.log"));

    const bool blocksExists = QDir(blocksPath).exists();
    const bool chainstateExists = QDir(chainstatePath).exists();
    const QFileInfo debugLogInfo(debugLogPath);

    qInfo() << "NodeManager: persistence check — datadir:" << m_dataDir;
    qInfo() << "NodeManager: persistence check — blocks/ exists:" << blocksExists
            << "path:" << blocksPath;
    qInfo() << "NodeManager: persistence check — chainstate/ exists:" << chainstateExists
            << "path:" << chainstatePath;
    qInfo() << "NodeManager: persistence check — debug.log exists:" << debugLogInfo.exists()
            << "size:" << (debugLogInfo.exists() ? debugLogInfo.size() : 0);
}

void NodeManager::handleLine(const QString &text, bool isStderr)
{
    const char *channelLabel = isStderr ? "stderr" : "stdout";
    const QString prefixed = QStringLiteral("[%1] %2").arg(QLatin1String(channelLabel), text);
    emit nodeOutput(prefixed);

    if (m_debugNodeOutput) {
        if (isStderr)
            qWarning() << "NodeManager:" << prefixed;
        else
            qDebug() << "NodeManager:" << prefixed;
        return;
    }

    if (isStderr)
        ++m_stderrLinesSinceSummary;
    else
        ++m_stdoutLinesSinceSummary;
}

void NodeManager::drainChannel(QByteArray *buffer, const QByteArray &chunk, bool isStderr)
{
    *buffer += chunk;
    for (;;) {
        int nl = buffer->indexOf('\n');
        if (nl < 0)
            break;
        QByteArray line = buffer->left(nl);
        buffer->remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(QString::fromLocal8Bit(line), isStderr);
    }
}

void NodeManager::flushOutputSummary()
{
    if (m_stdoutLinesSinceSummary > 0) {
        qInfo("NodeManager: stdout %d line(s) in the last second", m_stdoutLinesSinceSummary);
        m_stdoutLinesSinceSummary = 0;
    }
    if (m_stderrLinesSinceSummary > 0) {
        qWarning("NodeManager: stderr %d line(s) in the last second", m_stderrLinesSinceSummary);
        m_stderrLinesSinceSummary = 0;
    }
}

bool NodeManager::startNode()
{
    if (m_process->state() != QProcess::NotRunning) {
        qDebug() << "NodeManager: startNode skipped (already running)";
        return true;
    }

    m_stopRequested = false;
    m_shutdownInProgress = false;
    m_shutdownCompleted = false;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_stdoutLinesSinceSummary = 0;
    m_stderrLinesSinceSummary = 0;

    m_nodePath = findNodeBinary();
    if (m_nodePath.isEmpty()) {
        const QString err = QStringLiteral(
            "QBitX node executable (qbitx) was not found. Install or build qbitx and place it next to "
            "the GUI, or under a known build path (see logs for search list).");
        qWarning() << "NodeManager:" << err;
        emit nodeError(err);
        return false;
    }

    if (!QDir().mkpath(m_dataDir)) {
        const QString err = QStringLiteral("Failed to create data directory: %1").arg(m_dataDir);
        qWarning() << "NodeManager:" << err;
        emit nodeError(err);
        return false;
    }

    logDataDirPersistenceState();

    qDebug() << "NodeManager: starting node process...";

    m_startedEmitted = false;
    emit nodeStarting();

    QStringList args;
    args << QStringLiteral("-server=1");
    args << QStringLiteral("-printtoconsole");
    args << QStringLiteral("-rpcuser=qbx");
    args << QStringLiteral("-rpcpassword=qbx_local_wallet");
    args << QStringLiteral("-datadir=%1").arg(m_dataDir);

    m_process->setProgram(m_nodePath);
    m_process->setArguments(args);
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        const QString err = QStringLiteral("Failed to start qbitx process: %1")
                                .arg(m_process->errorString());
        qWarning() << "NodeManager:" << err;
        emit nodeError(err);
        m_nodePath.clear();
        return false;
    }

    if (!m_outputSummaryTimer->isActive())
        m_outputSummaryTimer->start();

    return true;
}

void NodeManager::requestGracefulShutdown()
{
    if (m_shutdownCompleted) {
        qInfo() << "NodeManager: graceful shutdown already completed";
        emit gracefulShutdownFinished();
        return;
    }
    if (m_shutdownInProgress) {
        qInfo() << "NodeManager: graceful shutdown already in progress";
        return;
    }

    m_shutdownInProgress = true;
    qInfo() << "NodeManager: GUI close requested";
    qInfo() << "NodeManager: embedded node shutdown requested";

    if (m_outputSummaryTimer->isActive())
        m_outputSummaryTimer->stop();
    flushOutputSummary();

    if (!isRunning()) {
        qInfo() << "NodeManager: qbitx.exe not running; shutdown complete";
        completeGracefulShutdown();
        return;
    }

    m_stopRequested = true;

    const QString cli = discoverQbitxCliExecutable();
    if (cli.isEmpty()) {
        qWarning() << "NodeManager: qbitx-cli not found; skipping RPC stop, waiting for node exit";
        beginWaitingForNodeExit();
        return;
    }

    if (m_stopCliProcess) {
        m_stopCliProcess->kill();
        m_stopCliProcess->deleteLater();
        m_stopCliProcess = nullptr;
    }

    QStringList args;
    QBitXEmbedded::appendConnectionCliArgsForDatadir(m_dataDir, &args);
    args << QStringLiteral("stop");

    const QStringList cmdForLog = QStringList{cli} + args;
    qInfo() << "NodeManager: running qbitx-cli stop:"
            << QBitXEmbedded::redactCliCommandForLog(cmdForLog).join(QLatin1Char(' '));

    m_stopCliProcess = new QProcess(this);
    QTimer *cliTimeout = new QTimer(m_stopCliProcess);
    cliTimeout->setSingleShot(true);
    cliTimeout->setInterval(CLI_STOP_TIMEOUT_MS);
    connect(cliTimeout, &QTimer::timeout, m_stopCliProcess, [this]() {
        if (m_stopCliProcess && m_stopCliProcess->state() != QProcess::NotRunning) {
            qWarning() << "NodeManager: qbitx-cli stop timed out after"
                       << (CLI_STOP_TIMEOUT_MS / 1000) << "seconds";
            m_stopCliProcess->kill();
        }
    });
    connect(m_stopCliProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [cliTimeout](int, QProcess::ExitStatus) { cliTimeout->stop(); });
    connect(m_stopCliProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NodeManager::onCliStopFinished);

    m_stopCliProcess->start(cli, args);
    cliTimeout->start();
}

void NodeManager::onCliStopFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);

    if (m_stopCliProcess) {
        const QString errOut = QString::fromUtf8(m_stopCliProcess->readAllStandardError()).trimmed();
        if (!errOut.isEmpty())
            qInfo() << "NodeManager: qbitx-cli stop stderr:" << errOut.left(500);
        m_stopCliProcess->deleteLater();
        m_stopCliProcess = nullptr;
    }

    qInfo() << "NodeManager: stop RPC finished with exit code" << exitCode;
    beginWaitingForNodeExit();
}

void NodeManager::beginWaitingForNodeExit()
{
    if (!isRunning()) {
        qInfo() << "NodeManager: qbitx.exe exited gracefully";
        completeGracefulShutdown();
        return;
    }

    qInfo() << "NodeManager: waiting for qbitx.exe graceful exit (up to"
            << (NODE_GRACEFUL_EXIT_MS / 1000) << "seconds)";

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NodeManager::onNodeExitAfterStop, Qt::SingleShotConnection);

    m_gracefulExitTimer->start();
}

void NodeManager::onNodeExitAfterStop(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    m_gracefulExitTimer->stop();
    m_terminateTimer->stop();
    qInfo() << "NodeManager: qbitx.exe exited gracefully with code" << exitCode;
    completeGracefulShutdown();
}

void NodeManager::onGracefulExitTimeout()
{
    if (!isRunning()) {
        qInfo() << "NodeManager: qbitx.exe exited gracefully";
        completeGracefulShutdown();
        return;
    }

    qWarning() << "NodeManager: terminate fallback used (node did not exit after RPC stop)";
    m_process->terminate();
    m_terminateTimer->start();
}

void NodeManager::onTerminateTimeout()
{
    if (!isRunning()) {
        qInfo() << "NodeManager: qbitx.exe exited after terminate";
        completeGracefulShutdown();
        return;
    }

    qWarning() << "NodeManager: kill fallback used";
    m_process->kill();
    if (!m_process->waitForFinished(KILL_TIMEOUT_MS))
        qWarning() << "NodeManager: kill wait timed out";
    completeGracefulShutdown();
}

void NodeManager::completeGracefulShutdown()
{
    m_gracefulExitTimer->stop();
    m_terminateTimer->stop();
    m_shutdownInProgress = false;
    m_shutdownCompleted = true;
    qInfo() << "NodeManager: embedded node shutdown complete";
    emit gracefulShutdownFinished();
}

void NodeManager::stopNodeBlocking()
{
    if (m_shutdownCompleted)
        return;
    if (!isRunning() && !m_shutdownInProgress) {
        m_shutdownCompleted = true;
        return;
    }

    qInfo() << "NodeManager: blocking graceful shutdown (destructor/exit fallback)";

    if (!m_shutdownInProgress)
        requestGracefulShutdown();

    QEventLoop loop;
    QTimer safety;
    safety.setSingleShot(true);
    safety.setInterval(CLI_STOP_TIMEOUT_MS + NODE_GRACEFUL_EXIT_MS + TERMINATE_TIMEOUT_MS + KILL_TIMEOUT_MS + 5000);
    connect(&safety, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &NodeManager::gracefulShutdownFinished, &loop, &QEventLoop::quit);
    safety.start();
    loop.exec();

    if (!m_shutdownCompleted && isRunning()) {
        qWarning() << "NodeManager: blocking shutdown safety timeout; forcing kill";
        m_process->kill();
        m_process->waitForFinished(KILL_TIMEOUT_MS);
        m_shutdownCompleted = true;
    }
}

bool NodeManager::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void NodeManager::onProcessStarted()
{
    m_startedEmitted = true;
    qDebug() << "NodeManager: node process started (PID" << m_process->processId() << ")";
    emit nodeStarted();
}

void NodeManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);

    if (m_outputSummaryTimer->isActive())
        m_outputSummaryTimer->stop();
    flushOutputSummary();

    const auto flushRemain = [this](const QByteArray &buf, bool isStderr) {
        if (buf.isEmpty())
            return;
        QByteArray line = buf;
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(QString::fromLocal8Bit(line), isStderr);
    };

    flushRemain(m_stdoutBuf, false);
    flushRemain(m_stderrBuf, true);
    m_stdoutBuf.clear();
    m_stderrBuf.clear();

    qDebug() << "NodeManager: node process finished, exit code:" << exitCode;
    if (m_startedEmitted && exitCode != 0 && !m_stopRequested) {
        emit nodeError(
            QStringLiteral("QBitX node exited unexpectedly (code %1)").arg(exitCode));
    }
    m_startedEmitted = false;
    if (!m_shutdownInProgress)
        m_stopRequested = false;
    emit nodeStopped();
}

void NodeManager::onProcessErrorOccurred(QProcess::ProcessError error)
{
    qWarning() << "NodeManager: QProcess error:" << error << "-" << m_process->errorString();
}

void NodeManager::onReadyReadStdout()
{
    drainChannel(&m_stdoutBuf, m_process->readAllStandardOutput(), false);
}

void NodeManager::onReadyReadStderr()
{
    drainChannel(&m_stderrBuf, m_process->readAllStandardError(), true);
}
