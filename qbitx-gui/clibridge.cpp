#include "clibridge.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QVariantMap>
#include <QRegularExpression>

CliBridge::CliBridge(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_pathErrorShown(false)
{
}

void CliBridge::call(const QString &method, const QStringList &params, const QString &wallet)
{
    if (m_settings->qbitxCliPath().isEmpty()) {
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
    executeCommand(command, method);
}

void CliBridge::callNamed(const QString &method, const QVariantMap &namedParams, const QString &wallet)
{
    if (m_settings->qbitxCliPath().isEmpty()) {
        if (!m_pathErrorShown) {
            emit errorOccurred("qbitx-cli path not configured");
            m_pathErrorShown = true;
        }
        return;
    }

    m_pathErrorShown = false;
    QStringList command = buildNamedCommand(method, namedParams, wallet);
    qDebug() << "Executing named command:" << command;
    executeCommand(command, method);
}

void CliBridge::executeCommand(const QStringList &command, const QString &method)
{
    // Create a new QProcess for this call
    QProcess *process = new QProcess(this);
    
    // Connect finished signal with lambda that captures the process, method name, and command
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, method, command](int exitCode, QProcess::ExitStatus exitStatus) {
        QByteArray stdoutBytes = process->readAllStandardOutput();
        QByteArray stderrBytes = process->readAllStandardError();
        
        // Trim outputs
        QString out = QString::fromUtf8(stdoutBytes).trimmed();
        QString err = QString::fromUtf8(stderrBytes).trimmed();
        
        // Log exit code and first 120 chars of output
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
                    QVariantMap errorObj = doc.toVariant().toMap();
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
                            emit success(successResult);
                        } else {
                            emit success(QVariant());
                        }
                        process->deleteLater();
                        return;
                    }
                    
                    // Handle error -18 "Wallet file not found" - clear active wallet
                    if (errorCode == -18 && errorMessage.contains("not found", Qt::CaseInsensitive)) {
                        qDebug() << "CliBridge: Error -18 wallet not found, clearing active wallet";
                        // Extract wallet name from command if it was a -rpcwallet call
                        QString walletName;
                        for (int i = 0; i < command.size(); ++i) {
                            if (command[i].startsWith("-rpcwallet=")) {
                                walletName = command[i].mid(11); // Remove "-rpcwallet=" prefix
                                break;
                            }
                        }
                        if (!walletName.isEmpty() && m_settings->activeWallet() == walletName) {
                            m_settings->setActiveWallet("");
                            qDebug() << "CliBridge: Cleared active wallet:" << walletName;
                        }
                    }
                }
            }
            
            // Regular error handling
            if (errorMsg.isEmpty()) {
                errorMsg = QString("Process exited with code %1").arg(exitCode);
            }
            emit errorOccurred(errorMsg);
        } else {
            // Success (exitCode == 0)
            if (out.startsWith('{') || out.startsWith('[')) {
                // Try to parse as JSON
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);
                
                if (parseError.error == QJsonParseError::NoError) {
                    // JSON parse successful
                    emit success(doc.toVariant());
                } else {
                    // JSON parse failed
                    QString preview = out.length() > 200 ? out.left(200) + "..." : out;
                    QString errorMsg = QString("JSON parse error: %1. Output: %2")
                                      .arg(parseError.errorString())
                                      .arg(preview);
                    emit errorOccurred(errorMsg);
                }
            } else {
                // Not JSON - emit success with raw string (not an error!)
                emit success(QVariant(out));
            }
        }
        
        // Clean up the process
        process->deleteLater();
    });
    
    // Connect error signal
    connect(process, &QProcess::errorOccurred,
            this, [this, process](QProcess::ProcessError error) {
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
        emit errorOccurred(errorMsg);
        process->deleteLater();
    });

    // Start the process
    process->start(command.first(), command.mid(1));
    if (!process->waitForStarted(5000)) {
        emit errorOccurred("Failed to start qbitx-cli process");
        process->deleteLater();
        return;
    }
}

QStringList CliBridge::buildCommand(const QString &method, const QStringList &params, const QString &wallet)
{
    QStringList args;

    // Add datadir if specified
    if (!m_settings->datadir().isEmpty()) {
        args << "-datadir=" + m_settings->datadir();
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
    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    // Add wallet if specified (use provided wallet or active wallet) - but NOT for global operations
    if (!isGlobalOperation) {
        QString walletToUse = wallet.isEmpty() ? m_settings->activeWallet() : wallet;
        if (!walletToUse.isEmpty()) {
            args << "-rpcwallet=" + walletToUse;
        }
    }

    // Add method and params
    args << method;
    args << params;

    QStringList fullCommand;
    fullCommand << m_settings->qbitxCliPath();
    fullCommand << args;

    return fullCommand;
}

QStringList CliBridge::buildNamedCommand(const QString &method, const QVariantMap &namedParams, const QString &wallet)
{
    QStringList args;

    // Add datadir if specified
    if (!m_settings->datadir().isEmpty()) {
        args << "-datadir=" + m_settings->datadir();
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
    QStringList globalWalletMethods = {"listwallets", "listwalletdir", "createwallet", "loadwallet", "unloadwallet"};
    bool isGlobalOperation = globalWalletMethods.contains(method);

    // Add wallet if specified (use provided wallet or active wallet) - but NOT for global operations
    if (!isGlobalOperation) {
        QString walletToUse = wallet.isEmpty() ? m_settings->activeWallet() : wallet;
        if (!walletToUse.isEmpty()) {
            args << "-rpcwallet=" + walletToUse;
        }
    }

    // Add -named flag and method
    args << "-named";
    args << method;

    // Add named parameters as key=value pairs
    for (auto it = namedParams.begin(); it != namedParams.end(); ++it) {
        QString key = it.key();
        QVariant value = it.value();
        
        QString valueStr;
        if (value.type() == QVariant::Bool) {
            valueStr = value.toBool() ? "true" : "false";
        } else if (value.type() == QVariant::Int || value.type() == QVariant::LongLong) {
            valueStr = QString::number(value.toLongLong());
        } else {
            valueStr = value.toString();
        }
        
        args << key + "=" + valueStr;
    }

    QStringList fullCommand;
    fullCommand << m_settings->qbitxCliPath();
    fullCommand << args;

    return fullCommand;
}
