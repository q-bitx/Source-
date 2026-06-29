#include "qbitxembedded.h"
#include "settingsmanager.h"

#include <QDir>
#include <QStandardPaths>

namespace QBitXEmbedded {

static QString embeddedRpcUser()
{
    return QStringLiteral("qbx");
}

static QString embeddedRpcPassword()
{
    return QStringLiteral("qbx_local_wallet");
}

QString defaultDataDir()
{
#if defined(Q_OS_WIN)
    QString base = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("QBitX")));
#elif defined(Q_OS_MACOS)
    return QDir::cleanPath(
        QDir(QDir::homePath()).filePath(QStringLiteral("Library/Application Support/QBitX")));
#else
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.qbitx"));
#endif
}

QString effectiveDatadir(const SettingsManager *settings)
{
    if (!settings)
        return defaultDataDir();
    const QString d = settings->datadir().trimmed();
    if (d.isEmpty())
        return defaultDataDir();
    return QDir::cleanPath(d);
}

void appendConnectionCliArgs(const SettingsManager *settings, QStringList *outArgs)
{
    if (!outArgs)
        return;
    const QString dd = QDir::toNativeSeparators(effectiveDatadir(settings));
    outArgs->append(QStringLiteral("-datadir=") + dd);
    outArgs->append(QStringLiteral("-rpcuser=") + embeddedRpcUser());
    outArgs->append(QStringLiteral("-rpcpassword=") + embeddedRpcPassword());
}

void appendConnectionCliArgsForDatadir(const QString &datadir, QStringList *outArgs)
{
    if (!outArgs)
        return;
    QString dd = datadir.trimmed();
    if (dd.isEmpty())
        dd = defaultDataDir();
    outArgs->append(QStringLiteral("-datadir=") + QDir::toNativeSeparators(QDir::cleanPath(dd)));
    outArgs->append(QStringLiteral("-rpcuser=") + embeddedRpcUser());
    outArgs->append(QStringLiteral("-rpcpassword=") + embeddedRpcPassword());
}

QStringList redactCliCommandForLog(const QStringList &command)
{
    QStringList out;
    out.reserve(command.size());
    for (QString p : command) {
        if (p.startsWith(QStringLiteral("-rpcpassword="), Qt::CaseInsensitive))
            p = QStringLiteral("-rpcpassword=<hidden>");
        out.append(p);
    }
    return out;
}

} // namespace QBitXEmbedded
