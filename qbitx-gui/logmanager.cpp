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

LogManager *LogManager::s_instance = nullptr;

LogManager::LogManager(QObject *parent)
    : QObject(parent)
{
    m_logsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
    QDir().mkpath(m_logsDir);
    m_currentDateStr = QDateTime::currentDateTime().toString("yyyyMMdd");
}

QString LogManager::logFilePath()
{
    QString today = QDateTime::currentDateTime().toString("yyyyMMdd");
    return m_logsDir + "/qbitx-gui_" + today + ".log";
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

void LogManager::append(const QString &level, const QString &msg)
{
    appendLine(level, msg);
}

void LogManager::appendFromMessageHandler(const QString &level, const QString &msg)
{
    // Must never call qDebug/qWarning/qInfo or emit signals (would re-enter message handler).
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString line = QString("[%1] [%2] %3").arg(timestamp, level, msg);
    m_lines.append(line);
    if (m_lines.size() > MAX_LINES)
        m_lines.removeFirst();
    m_logText = m_lines.join("\n");
    flushToFile(level, msg);
}

void LogManager::appendLine(const QString &level, const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString line = QString("[%1] [%2] %3").arg(timestamp, level, msg);

    m_lines.append(line);
    if (m_lines.size() > MAX_LINES)
        m_lines.removeFirst();
    rebuildLogText();
    flushToFile(level, msg);
    emit logTextChanged();
}

void LogManager::flushToFile(const QString &level, const QString &msg)
{
    QString today = QDateTime::currentDateTime().toString("yyyyMMdd");
    if (today != m_currentDateStr) {
        m_currentDateStr = today;
    }
    QString path = m_logsDir + "/qbitx-gui_" + m_currentDateStr + ".log";
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        out << "[" << timestamp << "] [" << level << "] " << msg << "\n";
        out.flush();
        f.close();
    }
}

void LogManager::rebuildLogText()
{
    m_logText = m_lines.join("\n");
}

void LogManager::clear()
{
    m_lines.clear();
    m_logText.clear();
    rebuildLogText();
    emit logTextChanged();
}
