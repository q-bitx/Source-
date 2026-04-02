#include "clibridge.h"
#include "logmanager.h"
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaType>
#include <QVariantMap>
#include <QRegularExpression>
#include <QTimer>

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

    // Reset error flag if path is now configured
    m_pathErrorShown = false;

    // Special case: use -named for getaddressbalances to ensure proper type conversion
    if (method == "getaddressbalances" && params.size() >= 2) {
        QVariantMap namedParams;
        namedParams["minconf"] = params[0].toInt();
        namedParams["include_unsafe"] = (params[1].toLower() == "true" || params[1] == "1");
        QStringList command = buildNamedCommand(method, namedParams, wallet);
        qDebug() << "Executing named command:" << command;
        executeCommand(command, method);
        return;
    }

    QStringList command = buildCommand(method, params, wallet);
    qDebug() << "Executing command:" << command;
    if (method == "getnewaddress") {
        qDebug() << "Generate PQ Address command (rpcwallet):" << command.join(" ");
    }
    executeCommand(command, method);
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
    qDebug() << "Executing named command:" << command;
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
    qDebug() << "Executing named command (tagged):" << command << "tag:" << tag;
    executeCommand(command, method, tag);
}

void CliBridge::executeCommand(const QStringList &command, const QString &method, const QString &tag)
{
    // Create a new QProcess for this call
    QProcess *process = new QProcess(this);
    const bool useTag = !tag.isEmpty();

    // Connect finished signal with lambda that captures the process, method name, command, and tag
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, method, command, useTag, tag](int exitCode, QProcess::ExitStatus exitStatus) {
        QByteArray stdoutBytes = process->readAllStandardOutput();
        QByteArray stderrBytes = process->readAllStandardError();
        QString out = QString::fromUtf8(stdoutBytes.constData(), stdoutBytes.size()).trimmed();
        QString err = QString::fromUtf8(stderrBytes.constData(), stderrBytes.size()).trimmed();

        if (m_logManager) {
            if (!out.isEmpty())
                m_logManager->append("INFO", "stdout: " + out);
            if (!err.isEmpty())
                m_logManager->append(exitCode != 0 ? "ERROR" : "WARN", "stderr: " + err);
            m_logManager->append("INFO", "EXIT: " + QString::number(exitCode));
        }

        QString outPreview = out.length() > 120 ? out.left(120) + "..." : out;
        QString errPreview = err.length() > 120 ? err.left(120) + "..." : err;
        qDebug() << "CliBridge [" << method << "] exitCode:" << exitCode
                 << "stdout:" << outPreview << "stderr:" << errPreview;

        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            // Process error or non-zero exit code
            // Try to parse stdout as JSON first (Bitcoin Core RPC errors come as JSON)
            QString errorMsg = err.isEmpty() ? out : err;
            
            // Check if stdout is JSON error with code -35 "already loaded" for loadwallet
            if (!out.isEmpty() && (out.startsWith('{') || out.startsWith('['))) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);
                
                if (parseError.error == QJsonParseError::NoError) {
                    QVariantMap errorObj = doc.toVariant().value<QVariantMap>();
                    int errorCode = errorObj.value("code").toInt();
                    QString errorMessage = errorObj.value("message").toString();
                    
                    // Suppress false error -35 "Wallet <name> is already loaded" for loadwallet
                    if (method == "loadwallet" && errorCode == -35 && 
                        errorMessage.contains("already loaded", Qt::CaseInsensitive)) {
                        // Treat as success - wallet is already loaded
                        qDebug() << "CliBridge: Suppressing error -35 'already loaded' for loadwallet, treating as success";
                        // Extract wallet name from error message (format: "Wallet <name> is already loaded")
                        QString walletName;
                        QRegularExpression nameRegex("Wallet\\s+([^\\s]+)\\s+is\\s+already\\s+loaded", QRegularExpression::CaseInsensitiveOption);
                        QRegularExpressionMatch match = nameRegex.match(errorMessage);
                        if (match.hasMatch()) {
                            walletName = match.captured(1);
                        } else {
                            // Fallback: try to extract from command arguments
                            // Command format: [qbitx-cli, -datadir=..., loadwallet, <walletName>]
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
                    
                    // Error -18 "Wallet file not found" - no global state to clear; caller handles
                    if (errorCode == -18 && errorMessage.contains("not found", Qt::CaseInsensitive)) {
                        qDebug() << "CliBridge: Error -18 wallet not found";
                    }
                }
            }
            
            // Detailed error for Logs/Dashboard: command, exit code, stderr
            if (errorMsg.isEmpty())
                errorMsg = QString("Process exited with code %1").arg(exitCode);
            QString detail = QString("command: %1\nexitCode: %2\nstderr: %3")
                .arg(command.join(" "), QString::number(exitCode), err);
            QString errMsgFinal = errorMsg + "\n" + detail;
            if (useTag)
                QTimer::singleShot(0, this, [this, errMsgFinal, tag]() { emit errorOccurredWithTag(errMsgFinal, tag); });
            else
                QTimer::singleShot(0, this, [this, errMsgFinal]() { emit errorOccurred(errMsgFinal); });
        } else {
            // Success (exitCode == 0)
            if (out.startsWith('{') || out.startsWith('[')) {
                // Try to parse as JSON
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);

                if (parseError.error == QJsonParseError::NoError) {
                    QVariant payload = doc.toVariant();
                    if (useTag)
                        QTimer::singleShot(0, this, [this, payload, tag]() { emit successWithTag(payload, tag); });
                    else
                        QTimer::singleShot(0, this, [this, payload]() { emit success(payload); });
                } else {
                    QString preview = out.length() > 200 ? out.left(200) + "..." : out;
                    QString parseErrMsg = QString("JSON parse error: %1. Output: %2")
                                      .arg(parseError.errorString())
                                      .arg(preview);
                    if (useTag)
                        QTimer::singleShot(0, this, [this, parseErrMsg, tag]() { emit errorOccurredWithTag(parseErrMsg, tag); });
                    else
                        QTimer::singleShot(0, this, [this, parseErrMsg]() { emit errorOccurred(parseErrMsg); });
                }
            } else {
                QVariant payload = QVariant(out);
                if (useTag)
                    QTimer::singleShot(0, this, [this, payload, tag]() { emit successWithTag(payload, tag); });
                else
                    QTimer::singleShot(0, this, [this, payload]() { emit success(payload); });
            }
        }

        process->deleteLater();
    });
    
    // Connect error signal
    connect(process, &QProcess::errorOccurred,
            this, [this, process, useTag, tag](QProcess::ProcessError error) {
        QByteArray stdoutBytes = process->readAllStandardOutput();
        QByteArray stderrBytes = process->readAllStandardError();
        
        QString out = QString::fromUtf8(stdoutBytes).trimmed();
        QString err = QString::fromUtf8(stderrBytes).trimmed();
        
        QString errorMsg;
        switch (error) {
        case QProcess::FailedToStart:
            errorMsg = "Failed to start qbitx-cli process";
            break;
        case QProcess::Crashed:
            errorMsg = "qbitx-cli process crashed";
            break;
        case QProcess::Timedout:
            errorMsg = "qbitx-cli process timed out";
            break;
        case QProcess::WriteError:
            errorMsg = "Write error to qbitx-cli process";
            break;
        case QProcess::ReadError:
            errorMsg = "Read error from qbitx-cli process";
            break;
        default:
            errorMsg = "Unknown process error";
        }
        
        // Include stderr/stdout if available
        if (!err.isEmpty()) {
            errorMsg += ": " + (err.length() > 200 ? err.left(200) + "..." : err);
        } else if (!out.isEmpty()) {
            errorMsg += ": " + (out.length() > 200 ? out.left(200) + "..." : out);
        }
        
        qDebug() << "CliBridge QProcess error:" << errorMsg;
        QString errMsg = errorMsg;
        if (useTag)
            QTimer::singleShot(0, this, [this, errMsg, tag]() { emit errorOccurredWithTag(errMsg, tag); });
        else
            QTimer::singleShot(0, this, [this, errMsg]() { emit errorOccurred(errMsg); });
        process->deleteLater();
    });

    if (m_logManager)
        m_logManager->append("INFO", "CMD: " + command.join(" "));

    process->start(command.first(), command.mid(1));
    if (!process->waitForStarted(5000)) {
        QString msg = QString("Failed to start qbitx-cli process\ncommand: %1").arg(command.join(" "));
        if (useTag)
            QTimer::singleShot(0, this, [this, msg, tag]() { emit errorOccurredWithTag(msg, tag); });
        else
            QTimer::singleShot(0, this, [this, msg]() { emit errorOccurred(msg); });
        process->deleteLater();
        return;
    }
}

QString CliBridge::effectiveDatadirForCli()
{
    QString datadir = m_settings->datadir();
    if (datadir.isEmpty()) {
        return QString();
    }
    if (!QDir(datadir).exists()) {
        qWarning() << "CliBridge: datadir does not exist, falling back to default qbitx-cli behavior:" << datadir;
        m_settings->setDatadir(QString());
        return QString();
    }
    return QDir::toNativeSeparators(datadir);
}

QStringList CliBridge::buildCommand(const QString &method, const QStringList &params, const QString &wallet)
{
    QStringList args;

    QString datadir = effectiveDatadirForCli();
    if (!datadir.isEmpty()) {
        args << "-datadir=" + datadir;
    }

    // Add network flag if specified (regtest/testnet/signet)
    QString network = m_settings->network();
    if (!network.isEmpty() && network != "main") {
        args << "-" + network;
    }

    // Add RPC credentials ONLY if BOTH are set (otherwise use cookie auth)
    QString rpcuser = m_settings->rpcuser();
    QString rpcpassword = m_settings->rpcpassword();
    if (!rpcuser.isEmpty() && !rpcpassword.isEmpty()) {
        args << "-rpcuser=" + rpcuser;
        args << "-rpcpassword=" + rpcpassword;
    }

    // Global wallet operations should NOT use -rpcwallet
    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet", "restorewallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    if (!isGlobalOperation)
        args << baseArgsForWallet(wallet);

    // Add method and params
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

    QString datadir = effectiveDatadirForCli();
    if (!datadir.isEmpty()) {
        args << "-datadir=" + datadir;
    }

    // Add network flag if specified (regtest/testnet/signet)
    QString network = m_settings->network();
    if (!network.isEmpty() && network != "main") {
        args << "-" + network;
    }

    // Add RPC credentials ONLY if BOTH are set (otherwise use cookie auth)
    QString rpcuser = m_settings->rpcuser();
    QString rpcpassword = m_settings->rpcpassword();
    if (!rpcuser.isEmpty() && !rpcpassword.isEmpty()) {
        args << "-rpcuser=" + rpcuser;
        args << "-rpcpassword=" + rpcpassword;
    }

    // Global wallet operations should NOT use -rpcwallet
    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet", "restorewallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    if (!isGlobalOperation)
        args << baseArgsForWallet(wallet);

    // Add -named flag and method
    args << "-named";
    args << method;

    // Add named parameters as key=value pairs
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
