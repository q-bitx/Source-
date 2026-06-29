#ifndef RPCWALLETBOOTSTRAP_H
#define RPCWALLETBOOTSTRAP_H

#include <QObject>
#include <QProcess>

class SettingsManager;
class LogManager;

/// After the embedded node starts: wait for RPC (getblockchaininfo), then load wallets from listwalletdir.
class RpcWalletBootstrap : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool rpcReady READ rpcReady NOTIFY rpcReadyChanged)

public:
    explicit RpcWalletBootstrap(SettingsManager *settings, LogManager *logManager, QObject *parent = nullptr);

    bool rpcReady() const { return m_rpcReady; }

    /// Starts non-blocking RPC polling (1s interval, 60s max), then wallet autoload.
    void begin();

signals:
    void rpcReadyChanged();
    void rpcReadinessTimedOut(const QString &message);
    /// Emitted after autoload finishes (success, partial failure, or no wallets).
    void walletLoadPhaseCompleted();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    enum class Phase {
        Idle,
        RpcWait,
        ListWalletDir,
        ListWallets,
        LoadWallet
    };

    void startPhase(Phase next);
    void appendLog(const QString &level, const QString &msg);
    void scheduleRpcRetry();
    void advanceLoadWallet();
    void setRpcReady(bool ready);
    QString cliPath() const;

    SettingsManager *m_settings;
    LogManager *m_logManager;
    QProcess *m_process;
    Phase m_phase;
    int m_rpcAttempts;
    QStringList m_dirWallets;
    QStringList m_loadedWallets;
    QStringList m_pendingLoads;
    int m_loadIndex;
    bool m_rpcReady;
};

#endif
