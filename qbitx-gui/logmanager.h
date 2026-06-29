#ifndef LOGMANAGER_H
#define LOGMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

class LogManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

public:
    explicit LogManager(QObject *parent = nullptr);
    ~LogManager() override;

    QString logText() const { return m_logText; }

    Q_INVOKABLE void append(const QString &level, const QString &msg);
    /** Called only from the Qt message handler; must never emit or call qDebug/qWarning. */
    void appendFromMessageHandler(const QString &level, const QString &msg);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString logFilePath();
    Q_INVOKABLE QString logsDirectory();
    Q_INVOKABLE void openLogsFolder();
    Q_INVOKABLE void copyToClipboard();

    /** Flush pending UI and file writes (e.g. on shutdown). */
    void flushAll();

    static void setInstance(LogManager *instance) { s_instance = instance; }
    static LogManager *instance() { return s_instance; }

signals:
    void logTextChanged();

private slots:
    void flushUiUpdate();
    void flushFilePending();

private:
    static LogManager *s_instance;
    static const int MAX_LINES = 5000;
    static const int UI_UPDATE_MS = 200;
    static const int FILE_FLUSH_MS = 750;

    QStringList m_lines;
    QString m_logText;
    QString m_currentDateStr;
    QString m_logsDir;
    QStringList m_pendingFileLines;
    bool m_logTextDirty = false;

    QTimer *m_uiTimer = nullptr;
    QTimer *m_fileTimer = nullptr;

    void pushLine(const QString &level, const QString &msg);
    QString logFilePathForToday() const;
};

#endif // LOGMANAGER_H
