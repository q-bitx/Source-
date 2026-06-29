#ifndef NODEMANAGER_H
#define NODEMANAGER_H

#include <QObject>
#include <QString>
#include <QProcess>

class QTimer;

class NodeManager : public QObject
{
    Q_OBJECT

public:
    explicit NodeManager(QObject *parent = nullptr);
    ~NodeManager() override;

    bool startNode();
    bool isRunning() const;
    QString nodePath() const { return m_nodePath; }
    QString dataDir() const { return m_dataDir; }

    /// Async graceful shutdown: qbitx-cli stop, wait for natural exit, then signal completion.
    Q_INVOKABLE void requestGracefulShutdown();

signals:
    void nodeStarting();
    void nodeStarted();
    void nodeError(const QString &message);
    void nodeStopped();
    void nodeOutput(const QString &line);
    void gracefulShutdownFinished();

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onReadyReadStdout();
    void onReadyReadStderr();
    void flushOutputSummary();
    void onCliStopFinished(int exitCode, QProcess::ExitStatus status);
    void onNodeExitAfterStop(int exitCode, QProcess::ExitStatus status);
    void onGracefulExitTimeout();
    void onTerminateTimeout();

private:
    static QString defaultDataDir();
    static QStringList candidateBinaryPaths();
    static QString findNodeBinary();
    void drainChannel(QByteArray *buffer, const QByteArray &chunk, bool isStderr);
    void handleLine(const QString &text, bool isStderr);
    void logDataDirPersistenceState() const;
    void beginWaitingForNodeExit();
    void useTerminateFallback();
    void useKillFallback();
    void completeGracefulShutdown();
    void stopNodeBlocking();

    static constexpr int CLI_STOP_TIMEOUT_MS = 30000;
    static constexpr int NODE_GRACEFUL_EXIT_MS = 120000;
    static constexpr int TERMINATE_TIMEOUT_MS = 15000;
    static constexpr int KILL_TIMEOUT_MS = 5000;

    QProcess *m_process;
    QProcess *m_stopCliProcess;
    QString m_dataDir;
    QString m_nodePath;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    bool m_startedEmitted;
    bool m_stopRequested;
    bool m_shutdownInProgress;
    bool m_shutdownCompleted;
    bool m_debugNodeOutput;
    int m_stdoutLinesSinceSummary;
    int m_stderrLinesSinceSummary;
    QTimer *m_outputSummaryTimer;
    QTimer *m_gracefulExitTimer;
    QTimer *m_terminateTimer;
};

#endif // NODEMANAGER_H
