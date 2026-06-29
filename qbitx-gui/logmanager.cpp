#include "logmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStringConverter>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>

LogManager *LogManager::s_instance = nullptr;

LogManager::LogManager(QObject *parent)
    : QObject(parent)
{
    m_logsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(m_logsDir);
    m_currentDateStr = QDateTime::currentDateTime().toString("yyyyMMdd");

    m_uiTimer = new QTimer(this);
    m_uiTimer->setSingleShot(true);
    m_uiTimer->setInterval(UI_UPDATE_MS);
    connect(m_uiTimer, &QTimer::timeout, this, &LogManager::flushUiUpdate);

    m_fileTimer = new QTimer(this);
    m_fileTimer->setSingleShot(true);
    m_fileTimer->setInterval(FILE_FLUSH_MS);
    connect(m_fileTimer, &QTimer::timeout, this, &LogManager::flushFilePending);
}

LogManager::~LogManager()
{
    flushAll();
}

QString LogManager::logFilePathForToday() const
{
    return m_logsDir + "/qbitx-gui_" + m_currentDateStr + ".log";
}

QString LogManager::logFilePath()
{
    return logFilePathForToday();
}

QString LogManager::logsDirectory()
{
    return m_logsDir;
}

void LogManager::openLogsFolder()
{
    QDir().mkpath(m_logsDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_logsDir));
}

void LogManager::copyToClipboard()
{
    if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        if (QClipboard *cb = app->clipboard())
            cb->setText(m_logText);
}

void LogManager::pushLine(const QString &level, const QString &msg)
{
    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    const QString line = QString("[%1] [%2] %3").arg(timestamp, level, msg);

    m_lines.append(line);
    if (m_lines.size() > MAX_LINES)
        m_lines.removeFirst();

    m_logTextDirty = true;
    m_pendingFileLines.append(line);

    if (!m_uiTimer->isActive())
        m_uiTimer->start();
    if (!m_fileTimer->isActive())
        m_fileTimer->start();
}

void LogManager::append(const QString &level, const QString &msg)
{
    pushLine(level, msg);
}

void LogManager::appendFromMessageHandler(const QString &level, const QString &msg)
{
    // Must never call qDebug/qWarning/qInfo or emit signals (would re-enter message handler).
    pushLine(level, msg);
}

void LogManager::flushUiUpdate()
{
    if (!m_logTextDirty)
        return;
    m_logText = m_lines.join("\n");
    m_logTextDirty = false;
    emit logTextChanged();
}

void LogManager::flushFilePending()
{
    if (m_pendingFileLines.isEmpty())
        return;

    const QString today = QDateTime::currentDateTime().toString("yyyyMMdd");
    if (today != m_currentDateStr)
        m_currentDateStr = today;

    QFile f(logFilePathForToday());
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString &line : m_pendingFileLines)
        out << line << "\n";
    out.flush();
    f.close();
    m_pendingFileLines.clear();
}

void LogManager::flushAll()
{
    if (m_uiTimer)
        m_uiTimer->stop();
    if (m_fileTimer)
        m_fileTimer->stop();
    flushUiUpdate();
    flushFilePending();
}

void LogManager::clear()
{
    if (m_uiTimer)
        m_uiTimer->stop();
    if (m_fileTimer)
        m_fileTimer->stop();
    m_lines.clear();
    m_pendingFileLines.clear();
    m_logText.clear();
    m_logTextDirty = false;
    emit logTextChanged();
}
