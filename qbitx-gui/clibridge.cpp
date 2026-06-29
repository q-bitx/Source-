#include "clibridge.h"
#include "logmanager.h"
#include "qbitxembedded.h"
#include <QDebug>
#include <QMetaType>
#include <QVariantMap>
#include <QRegularExpression>
#include <QTimer>
#include <memory>

namespace {

constexpr int CLI_TIMEOUT_MS = 30000;
constexpr int STDOUT_PREVIEW_MAX = 800;
constexpr int STDERR_PREVIEW_MAX = 1000;

QString truncatePreview(const QString &s, int maxLen)
{
    if (s.length() <= maxLen)
        return s;
    return s.left(maxLen) + QStringLiteral("...");
}

void logCliResult(LogManager *logManager, const QString &method, int exitCode,
                  const QString &out, const QString &err, const QString &commandLogLine)
{
    if (!logManager)
        return;

    const bool ok = exitCode == 0;
    QString msg = QStringLiteral("CLI %1 %2 exit=%3 cmd=%4")
                      .arg(method, ok ? QStringLiteral("OK") : QStringLiteral("FAIL"))
                      .arg(exitCode)
                      .arg(commandLogLine);

    const QString outPreview = truncatePreview(out, STDOUT_PREVIEW_MAX);
    const QString errPreview = truncatePreview(err, STDERR_PREVIEW_MAX);
    if (!outPreview.isEmpty())
        msg += QStringLiteral(" stdout=") + outPreview;
    if (!errPreview.isEmpty())
        msg += QStringLiteral(" stderr=") + errPreview;

    logManager->append(ok ? QStringLiteral("INFO") : QStringLiteral("ERROR"), msg);
}

} // namespace

CliBridge::CliBridge(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_logManager(nullptr)
    , m_pathErrorShown(false)
{
}

QStringList CliBridge::baseArgsForWallet(const QString &wallet) const
{
    QStringList args;
    QString w = wallet.trimmed();
    if (!w.isEmpty())
        args << "-rpcwallet=" + w;
    return args;
}

void CliBridge::call(const QString &method, const QStringList &params, const QString &wallet)
{
    if (m_settings->effectiveQbitxCliPath().isEmpty()) {
        if (!m_pathErrorShown) {
            emit errorOccurred("qbitx-cli path not configured");
            m_pathErrorShown = true;
        }
        return;
    }

    m_pathErrorShown = false;

    if (method == "getaddressbalances" && params.size() >= 2) {
        QVariantMap namedParams;
        namedParams["minconf"] = params[0].toInt();
        namedParams["include_unsafe"] = (params[1].toLower() == "true" || params[1] == "1");
        QStringList command = buildNamedCommand(method, namedParams, wallet);
        executeCommand(command, method);
        return;
    }

    QStringList command = buildCommand(method, params, wallet);
    executeCommand(command, method);
}

void CliBridge::callWithTag(const QString &method, const QStringList &params, const QString &wallet, const QString &tag)
{
    if (m_settings->effectiveQbitxCliPath().isEmpty()) {
        if (!m_pathErrorShown) {
            emit errorOccurredWithTag(QStringLiteral("qbitx-cli path not configured"), tag);
            m_pathErrorShown = true;
        }
        return;
    }

    m_pathErrorShown = false;
    QStringList command = buildCommand(method, params, wallet);
    executeCommand(command, method, tag);
}

void CliBridge::callNamed(const QString &method, const QVariantMap &namedParams, const QString &wallet)
{
    if (m_settings->effectiveQbitxCliPath().isEmpty()) {
        if (!m_pathErrorShown) {
            emit errorOccurred("qbitx-cli path not configured");
            m_pathErrorShown = true;
        }
        return;
    }

    m_pathErrorShown = false;
    QStringList command = buildNamedCommand(method, namedParams, wallet);
    executeCommand(command, method, QString());
}

void CliBridge::callNamedWithTag(const QString &method, const QVariantMap &namedParams, const QString &wallet, const QString &tag)
{
    if (m_settings->effectiveQbitxCliPath().isEmpty()) {
        if (!m_pathErrorShown) {
            emit errorOccurred("qbitx-cli path not configured");
            m_pathErrorShown = true;
        }
        return;
    }

    m_pathErrorShown = false;
    QStringList command = buildNamedCommand(method, namedParams, wallet);
    executeCommand(command, method, tag);
}

void CliBridge::executeCommand(const QStringList &command, const QString &method, const QString &tag)
{
    QProcess *process = new QProcess(this);
    QTimer *timeoutTimer = new QTimer(process);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(CLI_TIMEOUT_MS);

    const bool useTag = !tag.isEmpty();
    const QString commandLogLine = QBitXEmbedded::redactCliCommandForLog(command).join(QLatin1Char(' '));
    auto handled = std::make_shared<bool>(false);

    const auto emitError = [this, useTag, tag, handled](const QString &errMsg) {
        if (*handled)
            return;
        *handled = true;
        if (useTag)
            QTimer::singleShot(0, this, [this, errMsg, tag]() { emit errorOccurredWithTag(errMsg, tag); });
        else
            QTimer::singleShot(0, this, [this, errMsg]() { emit errorOccurred(errMsg); });
    };

    connect(timeoutTimer, &QTimer::timeout, process, [process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, method, command, commandLogLine, useTag, tag, handled, timeoutTimer,
                   emitError](int exitCode, QProcess::ExitStatus exitStatus) {
        timeoutTimer->stop();
        if (*handled) {
            process->deleteLater();
            return;
        }
        *handled = true;

        const QByteArray stdoutBytes = process->readAllStandardOutput();
        const QByteArray stderrBytes = process->readAllStandardError();
        const QString out = QString::fromUtf8(stdoutBytes.constData(), stdoutBytes.size()).trimmed();
        const QString err = QString::fromUtf8(stderrBytes.constData(), stderrBytes.size()).trimmed();

        const bool timedOut = (exitStatus != QProcess::NormalExit);
        if (timedOut) {
            logCliResult(m_logManager, method, -1, out, err, commandLogLine);
            emitError(QStringLiteral("qbitx-cli timed out after %1 seconds\ncommand: %2")
                          .arg(CLI_TIMEOUT_MS / 1000)
                          .arg(commandLogLine));
            process->deleteLater();
            return;
        }

        if (exitCode != 0) {
            QString errorMsg = err.isEmpty() ? out : err;

            if (!out.isEmpty() && (out.startsWith('{') || out.startsWith('['))) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);

                if (parseError.error == QJsonParseError::NoError) {
                    QVariantMap errorObj = doc.toVariant().value<QVariantMap>();
                    int errorCode = errorObj.value("code").toInt();
                    QString errorMessage = errorObj.value("message").toString();

                    if (method == "loadwallet" && errorCode == -35 &&
                        errorMessage.contains("already loaded", Qt::CaseInsensitive)) {
                        logCliResult(m_logManager, method, 0, out, err, commandLogLine);
                        QString walletName;
                        QRegularExpression nameRegex("Wallet\\s+([^\\s]+)\\s+is\\s+already\\s+loaded",
                                                     QRegularExpression::CaseInsensitiveOption);
                        QRegularExpressionMatch match = nameRegex.match(errorMessage);
                        if (match.hasMatch()) {
                            walletName = match.captured(1);
                        } else {
                            for (int i = 0; i < command.size(); ++i) {
                                if (command[i] == "loadwallet" && i + 1 < command.size()) {
                                    walletName = command[i + 1];
                                    break;
                                }
                            }
                        }
                        if (!walletName.isEmpty()) {
                            QVariantMap successResult;
                            successResult["name"] = walletName;
                            QVariant payload = successResult;
                            if (useTag)
                                QTimer::singleShot(0, this, [this, payload, tag]() { emit successWithTag(payload, tag); });
                            else
                                QTimer::singleShot(0, this, [this, payload]() { emit success(payload); });
                        } else {
                            if (useTag)
                                QTimer::singleShot(0, this, [this, tag]() { emit successWithTag(QVariant(), tag); });
                            else
                                QTimer::singleShot(0, this, [this]() { emit success(QVariant()); });
                        }
                        process->deleteLater();
                        return;
                    }
                }
            }

            logCliResult(m_logManager, method, exitCode, out, err, commandLogLine);
            if (errorMsg.isEmpty())
                errorMsg = QString("Process exited with code %1").arg(exitCode);
            const QString errMsgFinal = errorMsg + "\n"
                + QString("command: %1\nexitCode: %2\nstderr: %3")
                      .arg(commandLogLine, QString::number(exitCode), err);
            emitError(errMsgFinal);
            process->deleteLater();
            return;
        }

        logCliResult(m_logManager, method, exitCode, out, err, commandLogLine);

        if (out.startsWith('{') || out.startsWith('[')) {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);

            if (parseError.error == QJsonParseError::NoError) {
                QVariant payload = doc.toVariant();
                if (useTag)
                    QTimer::singleShot(0, this, [this, payload, tag]() { emit successWithTag(payload, tag); });
                else
                    QTimer::singleShot(0, this, [this, payload]() { emit success(payload); });
            } else {
                const QString preview = truncatePreview(out, 200);
                const QString parseErrMsg = QString("JSON parse error: %1. Output: %2")
                                                .arg(parseError.errorString())
                                                .arg(preview);
                emitError(parseErrMsg);
            }
        } else {
            QVariant payload = QVariant(out);
            if (useTag)
                QTimer::singleShot(0, this, [this, payload, tag]() { emit successWithTag(payload, tag); });
            else
                QTimer::singleShot(0, this, [this, payload]() { emit success(payload); });
        }

        process->deleteLater();
    });

    connect(process, &QProcess::errorOccurred,
            this, [this, process, useTag, tag, handled, timeoutTimer, commandLogLine, method,
                   emitError](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *handled)
            return;

        timeoutTimer->stop();
        *handled = true;

        const QString errMsg = QStringLiteral("Failed to start qbitx-cli (%1)\ncommand: %2")
                                   .arg(method, commandLogLine);
        logCliResult(m_logManager, method, -1, QString(), QStringLiteral("Failed to start"), commandLogLine);
        emitError(errMsg);
        process->deleteLater();
    });

    process->start(command.first(), command.mid(1));
    timeoutTimer->start();
}

QStringList CliBridge::buildCommand(const QString &method, const QStringList &params, const QString &wallet)
{
    QStringList args;
    QBitXEmbedded::appendConnectionCliArgs(m_settings, &args);

    QString network = m_settings->network();
    if (!network.isEmpty() && network != "main") {
        args << "-" + network;
    }

    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet", "restorewallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    if (!isGlobalOperation)
        args << baseArgsForWallet(wallet);

    args << method;
    args << params;

    QStringList fullCommand;
    fullCommand << m_settings->effectiveQbitxCliPath();
    fullCommand << args;

    return fullCommand;
}

QStringList CliBridge::buildNamedCommand(const QString &method, const QVariantMap &namedParams, const QString &wallet)
{
    QStringList args;
    QBitXEmbedded::appendConnectionCliArgs(m_settings, &args);

    QString network = m_settings->network();
    if (!network.isEmpty() && network != "main") {
        args << "-" + network;
    }

    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet", "restorewallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    if (!isGlobalOperation)
        args << baseArgsForWallet(wallet);

    args << "-named";
    args << method;

    for (auto it = namedParams.begin(); it != namedParams.end(); ++it) {
        QString key = it.key();
        QVariant value = it.value();

        QString valueStr;
        const int tid = value.metaType().id();
        if (tid == QMetaType::Bool) {
            valueStr = value.toBool() ? "true" : "false";
        } else if (tid == QMetaType::Int || tid == QMetaType::LongLong) {
            valueStr = QString::number(value.toLongLong());
        } else {
            valueStr = value.toString();
        }

        args << key + "=" + valueStr;
    }

    QStringList fullCommand;
    fullCommand << m_settings->effectiveQbitxCliPath();
    fullCommand << args;

    return fullCommand;
}
