#include "rpcwalletbootstrap.h"

#include "logmanager.h"
#include "qbitxembedded.h"
#include "settingsmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace {

bool stdoutLooksLikeBlockchainInfo(const QByteArray &out)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    return obj.contains(QStringLiteral("chain")) || obj.contains(QStringLiteral("blocks"));
}

QStringList parseListWalletDirJson(const QByteArray &raw)
{
    QStringList names;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return names;
    const QJsonArray wallets = doc.object().value(QStringLiteral("wallets")).toArray();
    for (const QJsonValue &v : wallets) {
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            QString n = o.value(QStringLiteral("name")).toString();
            if (n.isEmpty())
                n = o.value(QStringLiteral("path")).toString();
            if (!n.isEmpty())
                names.append(n);
        } else if (v.isString()) {
            const QString s = v.toString();
            if (!s.isEmpty())
                names.append(s);
        }
    }
    return names;
}

QStringList parseListWalletsJson(const QByteArray &raw)
{
    QStringList names;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (pe.error != QJsonParseError::NoError)
        return names;
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array()) {
            if (v.isString()) {
                const QString s = v.toString();
                if (!s.isEmpty())
                    names.append(s);
            }
        }
    } else if (doc.isObject()) {
        const QJsonArray arr = doc.object().value(QStringLiteral("wallets")).toArray();
        for (const QJsonValue &v : arr) {
            if (v.isString()) {
                const QString s = v.toString();
                if (!s.isEmpty())
                    names.append(s);
            }
        }
    }
    return names;
}

} // namespace

RpcWalletBootstrap::RpcWalletBootstrap(SettingsManager *settings, LogManager *logManager, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_logManager(logManager)
    , m_process(new QProcess(this))
    , m_phase(Phase::Idle)
    , m_rpcAttempts(0)
    , m_loadIndex(0)
    , m_rpcReady(false)
{
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RpcWalletBootstrap::onProcessFinished);
}

QString RpcWalletBootstrap::cliPath() const
{
    if (!m_settings)
        return QString();
    return m_settings->effectiveQbitxCliPath();
}

void RpcWalletBootstrap::appendLog(const QString &level, const QString &msg)
{
    if (m_logManager)
        m_logManager->append(level, msg);
}

void RpcWalletBootstrap::setRpcReady(bool ready)
{
    if (m_rpcReady == ready)
        return;
    m_rpcReady = ready;
    emit rpcReadyChanged();
}

void RpcWalletBootstrap::begin()
{
    if (m_phase != Phase::Idle) {
        appendLog(QStringLiteral("INFO"), QStringLiteral("Bootstrap already running; begin() ignored."));
        return;
    }

    const QString cli = cliPath();
    if (cli.isEmpty()) {
        appendLog(QStringLiteral("ERROR"), QStringLiteral("qbitx-cli not found; skipping RPC wait and wallet autoload."));
        emit walletLoadPhaseCompleted();
        return;
    }

    m_rpcAttempts = 0;
    m_dirWallets.clear();
    m_loadedWallets.clear();
    m_pendingLoads.clear();
    m_loadIndex = 0;
    setRpcReady(false);

    appendLog(QStringLiteral("INFO"),
              QStringLiteral("Waiting for node RPC (getblockchaininfo, 1s interval, 60s max)."));
    startPhase(Phase::RpcWait);
}

void RpcWalletBootstrap::scheduleRpcRetry()
{
    QTimer::singleShot(1000, this, [this]() {
        if (m_phase == Phase::RpcWait)
            startPhase(Phase::RpcWait);
    });
}

void RpcWalletBootstrap::startPhase(Phase next)
{
    m_phase = next;

    if (m_process->state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("WARNING"), QStringLiteral("Bootstrap process still running; phase deferred."));
        return;
    }

    const QString cli = cliPath();
    if (cli.isEmpty()) {
        appendLog(QStringLiteral("ERROR"), QStringLiteral("qbitx-cli path became empty."));
        emit walletLoadPhaseCompleted();
        m_phase = Phase::Idle;
        return;
    }

    QStringList args;
    QBitXEmbedded::appendConnectionCliArgs(m_settings, &args);

    switch (m_phase) {
    case Phase::RpcWait: {
        ++m_rpcAttempts;
        if (m_rpcAttempts > 60) {
            const QString msg = QStringLiteral(
                "Timed out waiting for node RPC (getblockchaininfo) after 60 seconds.");
            appendLog(QStringLiteral("ERROR"), msg);
            emit rpcReadinessTimedOut(msg);
            m_phase = Phase::Idle;
            emit walletLoadPhaseCompleted();
            return;
        }
        if (m_rpcAttempts == 1 || m_rpcAttempts % 10 == 0) {
            appendLog(QStringLiteral("INFO"),
                      QStringLiteral("RPC probe attempt %1/60 (getblockchaininfo).").arg(m_rpcAttempts));
        }
        args << QStringLiteral("getblockchaininfo");
        m_process->start(cli, args);
        return;
    }
    case Phase::ListWalletDir:
        args << QStringLiteral("listwalletdir");
        appendLog(QStringLiteral("INFO"), QStringLiteral("Bootstrap: listwalletdir"));
        m_process->start(cli, args);
        return;
    case Phase::ListWallets:
        args << QStringLiteral("listwallets");
        appendLog(QStringLiteral("INFO"), QStringLiteral("Bootstrap: listwallets"));
        m_process->start(cli, args);
        return;
    case Phase::Idle:
        return;
    }
}

void RpcWalletBootstrap::advanceLoadWallet()
{
    if (m_loadIndex >= m_pendingLoads.size()) {
        appendLog(QStringLiteral("INFO"), QStringLiteral("Wallet autoload phase finished."));
        m_phase = Phase::Idle;
        emit walletLoadPhaseCompleted();
        return;
    }

    const QString walletName = m_pendingLoads.at(m_loadIndex);
    QStringList args;
    QBitXEmbedded::appendConnectionCliArgs(m_settings, &args);
    args << QStringLiteral("loadwallet") << walletName;

    appendLog(QStringLiteral("INFO"),
              QStringLiteral("Bootstrap: loadwallet %1").arg(walletName));

    const QString cli = cliPath();
    m_process->start(cli, args);
}

void RpcWalletBootstrap::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);

    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    const QString errStr = QString::fromUtf8(err).trimmed();

    switch (m_phase) {
    case Phase::RpcWait: {
        if (exitCode == 0 && stdoutLooksLikeBlockchainInfo(out)) {
            setRpcReady(true);
            appendLog(QStringLiteral("INFO"),
                      QStringLiteral("RPC is ready (getblockchaininfo succeeded on attempt %1).")
                          .arg(m_rpcAttempts));
            startPhase(Phase::ListWalletDir);
        } else {
            if (m_rpcAttempts == 1 || m_rpcAttempts % 10 == 0) {
                const QString detail = errStr.isEmpty() ? QStringLiteral("(no stderr)") : errStr.left(200);
                appendLog(QStringLiteral("INFO"),
                          QStringLiteral("RPC not ready on attempt %1: %2").arg(m_rpcAttempts).arg(detail));
            }
            scheduleRpcRetry();
        }
        return;
    }
    case Phase::ListWalletDir: {
        if (exitCode != 0) {
            appendLog(QStringLiteral("ERROR"),
                      QStringLiteral("listwalletdir failed: %1").arg(errStr.isEmpty() ? QStringLiteral("(no stderr)") : errStr));
            m_phase = Phase::Idle;
            emit walletLoadPhaseCompleted();
            return;
        }
        m_dirWallets = parseListWalletDirJson(out);
        startPhase(Phase::ListWallets);
        return;
    }
    case Phase::ListWallets: {
        if (exitCode != 0) {
            appendLog(QStringLiteral("ERROR"),
                      QStringLiteral("listwallets failed: %1").arg(errStr.isEmpty() ? QStringLiteral("(no stderr)") : errStr));
            m_phase = Phase::Idle;
            emit walletLoadPhaseCompleted();
            return;
        }
        m_loadedWallets = parseListWalletsJson(out);
        QSet<QString> loadedSet;
        for (const QString &w : m_loadedWallets)
            loadedSet.insert(w);

        m_pendingLoads.clear();
        for (const QString &w : m_dirWallets) {
            if (!loadedSet.contains(w))
                m_pendingLoads.append(w);
        }

        if (m_dirWallets.isEmpty()) {
            appendLog(QStringLiteral("INFO"), QStringLiteral("No wallets found; user may create a new wallet"));
        } else if (m_pendingLoads.isEmpty()) {
            appendLog(QStringLiteral("INFO"), QStringLiteral("All wallets from listwalletdir are already loaded"));
        } else {
            appendLog(QStringLiteral("INFO"),
                      QStringLiteral("Loading %1 wallet(s) from listwalletdir").arg(m_pendingLoads.size()));
        }

        m_loadIndex = 0;
        m_phase = Phase::LoadWallet;
        advanceLoadWallet();
        return;
    }
    case Phase::LoadWallet: {
        const QString walletName = (m_loadIndex < m_pendingLoads.size()) ? m_pendingLoads.at(m_loadIndex) : QString();
        if (exitCode == 0) {
            appendLog(QStringLiteral("INFO"), QStringLiteral("loadwallet OK: %1").arg(walletName));
        } else {
            const QString msg = errStr.isEmpty() ? QStringLiteral("exit %1").arg(exitCode) : errStr;
            appendLog(QStringLiteral("ERROR"),
                      QStringLiteral("loadwallet failed for \"%1\": %2").arg(walletName, msg));
        }
        ++m_loadIndex;
        advanceLoadWallet();
        return;
    }
    case Phase::Idle:
        return;
    }
}
