#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class LogManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

public:
    explicit LogManager(QObject *parent = nullptr);

    QString logText() const { return m_logText; }

    Q_INVOKABLE void append(const QString &level, const QString &msg);
    /** Called only from the Qt message handler; must never emit or call qDebug/qWarning. */
    void appendFromMessageHandler(const QString &level, const QString &msg);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString logFilePath();
    Q_INVOKABLE QString logsDirectory();
    Q_INVOKABLE void openLogsFolder();
    Q_INVOKABLE void copyToClipboard();

    static void setInstance(LogManager *instance) { s_instance = instance; }
    static LogManager *instance() { return s_instance; }

signals:
    void logTextChanged();

private:
    static LogManager *s_instance;
    static const int MAX_LINES = 5000;

    QStringList m_lines;
    QString m_logText;
    QString m_currentDateStr;
    QString m_logsDir;

    void appendLine(const QString &level, const QString &msg);
    void flushToFile(const QString &level, const QString &msg);
    void rebuildLogText();
};

#endif // LOGMANAGER_H
