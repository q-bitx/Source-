#include "clirunner.h"
#include "logmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

CliRunner::CliRunner(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_logManager(nullptr)
    , m_lastExitCode(-1)
{
}

QString CliRunner::detectCliPath()
{
    QString appDir = QCoreApplication::applicationDirPath();

    // 1) Next to GUI executable
    QString pathExe = appDir + "/qbitx-cli";
    QFileInfo infoExe(pathExe);
    if (infoExe.exists() && infoExe.isExecutable())
        return infoExe.absoluteFilePath();

    // 2) ~/qbitx_restore/build_wallet2/qbitx-cli
    QString home = QDir::homePath();
    QString pathBw2 = home + "/qbitx_restore/build_wallet2/qbitx-cli";
    QFileInfo infoBw2(pathBw2);
    if (infoBw2.exists() && infoBw2.isExecutable())
        return infoBw2.absoluteFilePath();

    // 3) ~/qbitx_restore/build_wallet/qbitx-cli
    QString pathBw = home + "/qbitx_restore/build_wallet/qbitx-cli";
    QFileInfo infoBw(pathBw);
    if (infoBw.exists() && infoBw.isExecutable())
        return infoBw.absoluteFilePath();

    // 4) ./qbitx-cli (working dir)
    QString pathCwd = QDir::currentPath() + "/qbitx-cli";
    QFileInfo infoCwd(pathCwd);
    if (infoCwd.exists() && infoCwd.isExecutable())
        return infoCwd.absoluteFilePath();

    // 5) PATH lookup
    QString foundPath = QStandardPaths::findExecutable("qbitx-cli");
    if (!foundPath.isEmpty())
        return foundPath;

    return QString();
}

void CliRunner::setLastStdout(const QString &s)
{
    if (m_lastStdout != s) {
        m_lastStdout = s;
        emit lastStdoutChanged();
    }
}

void CliRunner::setLastStderr(const QString &s)
{
    if (m_lastStderr != s) {
        m_lastStderr = s;
        emit lastStderrChanged();
    }
}

void CliRunner::setLastExitCode(int code)
{
    if (m_lastExitCode != code) {
        m_lastExitCode = code;
        emit lastExitCodeChanged();
    }
}

void CliRunner::setNetworkInfoJson(const QString &s)
{
    if (m_networkInfoJson != s) {
        m_networkInfoJson = s;
        emit networkInfoJsonChanged();
    }
}

void CliRunner::testConnection(const QString &cliPath, const QString &datadir)
{
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }

    setLastStdout(QString());
    setLastStderr(QString());
    setLastExitCode(-1);
    setNetworkInfoJson(QString());

    QString path = cliPath.trimmed();
    if (path.isEmpty()) {
        emit testFinished(false, "qbitx-cli path is empty.");
        return;
    }

    QFileInfo fi(path);
    if (!fi.exists()) {
        emit testFinished(false, "qbitx-cli not found or not executable.");
        return;
    }
    if (!fi.isExecutable()) {
        emit testFinished(false, "qbitx-cli is not executable.");
        return;
    }

    QStringList args;
    if (!datadir.trimmed().isEmpty())
        args << "-datadir=" + datadir.trimmed();
    args << "getnetworkinfo";

    QString fullCmd = path + " " + args.join(" ");
    if (m_logManager)
        m_logManager->append("INFO", "CMD: " + fullCmd);

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        QByteArray outBa = m_process->readAllStandardOutput();
        QByteArray errBa = m_process->readAllStandardError();
        QString out = QString::fromUtf8(outBa.constData(), outBa.size()).trimmed();
        QString err = QString::fromUtf8(errBa.constData(), errBa.size()).trimmed();
        setLastStdout(out);
        setLastStderr(err);
        setLastExitCode(exitCode);

        if (m_logManager) {
            if (!out.isEmpty())
                m_logManager->append("INFO", "stdout: " + out);
            if (!err.isEmpty())
                m_logManager->append(exitCode != 0 ? "ERROR" : "WARN", "stderr: " + err);
            m_logManager->append("INFO", "EXIT: " + QString::number(exitCode));
        }

        if (exitCode == 0 && !out.isEmpty()) {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                int version = obj.value("version").toInt(0);
                QString subversion = obj.value("subversion").toString();
                setNetworkInfoJson(out);
                QString msg = QString("Connected successfully. Version: %1 / subversion: %2")
                    .arg(version).arg(subversion.isEmpty() ? "N/A" : subversion);
                QTimer::singleShot(0, this, [this, msg]() { emit testFinished(true, msg); });
            } else {
                if (out.contains("version") || out.contains("subversion"))
                    QTimer::singleShot(0, this, [this]() { emit testFinished(true, "Connected successfully."); });
                else
                    QTimer::singleShot(0, this, [this]() { emit testFinished(false, "Could not parse getnetworkinfo response."); });
            }
        } else {
            QString errMsg;
            if (err.contains("Could not connect", Qt::CaseInsensitive) || err.contains("connection refused", Qt::CaseInsensitive))
                errMsg = "Could not connect to server (is qbitx node running?)";
            else if (!err.isEmpty())
                errMsg = err;
            else if (!out.isEmpty() && out.startsWith('{'))
                errMsg = "Command failed. Check stderr.";
            else
                errMsg = "qbitx-cli failed. Exit code: " + QString::number(exitCode);
            QTimer::singleShot(0, this, [this, errMsg]() { emit testFinished(false, errMsg); });
        }

        m_process->deleteLater();
        m_process = nullptr;
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_process) {
            setLastStderr("Process failed to start.");
            if (m_logManager)
                m_logManager->append("ERROR", "stderr: Process failed to start.");
            QTimer::singleShot(0, this, [this]() { emit testFinished(false, "qbitx-cli not found or not executable."); });
            m_process->deleteLater();
            m_process = nullptr;
        }
    });

    m_process->start(path, args);
}
