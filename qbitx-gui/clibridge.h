#ifndef CLIBRIDGE_H
#define CLIBRIDGE_H

#include <QObject>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonParseError>
#include "settingsmanager.h"

class LogManager;

class CliBridge : public QObject
{
    Q_OBJECT

public:
    explicit CliBridge(SettingsManager *settings, QObject *parent = nullptr);
    void setLogManager(LogManager *logManager) { m_logManager = logManager; }

    Q_INVOKABLE void call(const QString &method, const QStringList &params = QStringList(), const QString &wallet = QString());
    Q_INVOKABLE void callNamed(const QString &method, const QVariantMap &namedParams, const QString &wallet = QString());
    /// Call with a tag; result is emitted via successWithTag(result, tag) so caller can match response to request (e.g. wallet name).
    Q_INVOKABLE void callNamedWithTag(const QString &method, const QVariantMap &namedParams, const QString &wallet, const QString &tag);

signals:
    void success(const QVariant &result);
    void successWithTag(const QVariant &result, const QString &tag);
    void errorOccurred(const QString &errorMessage);
    void errorOccurredWithTag(const QString &errorMessage, const QString &tag);

private:
    SettingsManager *m_settings;
    LogManager *m_logManager;
    bool m_pathErrorShown;

    QStringList baseArgsForWallet(const QString &wallet) const;
    QString effectiveDatadirForCli();
    void executeCommand(const QStringList &command, const QString &method, const QString &tag = QString());
    QStringList buildCommand(const QString &method, const QStringList &params, const QString &wallet);
    QStringList buildNamedCommand(const QString &method, const QVariantMap &namedParams, const QString &wallet);
};

#endif // CLIBRIDGE_H
