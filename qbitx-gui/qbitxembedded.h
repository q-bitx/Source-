#ifndef QBITXEMBEDDED_H
#define QBITXEMBEDDED_H

#include <QString>
#include <QStringList>

class SettingsManager;

namespace QBitXEmbedded {

QString defaultDataDir();
QString effectiveDatadir(const SettingsManager *settings);

/// Appends -datadir=... -rpcuser=... -rpcpassword=... matching the embedded NodeManager node.
void appendConnectionCliArgs(const SettingsManager *settings, QStringList *outArgs);

/// Same RPC/datadir args when only an explicit datadir is known (e.g. CliRunner tests).
void appendConnectionCliArgsForDatadir(const QString &datadir, QStringList *outArgs);

QStringList redactCliCommandForLog(const QStringList &command);

} // namespace QBitXEmbedded

#endif
