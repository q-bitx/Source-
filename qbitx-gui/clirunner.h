#ifndef CLIRUNNER_H
#define CLIRUNNER_H

#include <QObject>
#include <QString>
#include <QProcess>

class LogManager;

class CliRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastStdout READ lastStdout NOTIFY lastStdoutChanged)
    Q_PROPERTY(QString lastStderr READ lastStderr NOTIFY lastStderrChanged)
    Q_PROPERTY(int lastExitCode READ lastExitCode NOTIFY lastExitCodeChanged)
    Q_PROPERTY(QString networkInfoJson READ networkInfoJson NOTIFY networkInfoJsonChanged)

public:
    explicit CliRunner(QObject *parent = nullptr);
    void setLogManager(LogManager *logManager) { m_logManager = logManager; }

    QString lastStdout() const { return m_lastStdout; }
    QString lastStderr() const { return m_lastStderr; }
    int lastExitCode() const { return m_lastExitCode; }
    QString networkInfoJson() const { return m_networkInfoJson; }

    Q_INVOKABLE QString detectCliPath();
    Q_INVOKABLE void testConnection(const QString &cliPath, const QString &datadir);

signals:
    void lastStdoutChanged();
    void lastStderrChanged();
    void lastExitCodeChanged();
    void networkInfoJsonChanged();
    void testFinished(bool success, const QString &message);

private:
    QProcess *m_process;
    LogManager *m_logManager;
    QString m_lastStdout;
    QString m_lastStderr;
    int m_lastExitCode;
    QString m_networkInfoJson;

    void setLastStdout(const QString &s);
    void setLastStderr(const QString &s);
    void setLastExitCode(int code);
    void setNetworkInfoJson(const QString &s);
};

#endif
