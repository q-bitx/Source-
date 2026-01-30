#ifndef CLIBRIDGE_H
#define CLIBRIDGE_H

#include <QObject>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonParseError>
#include "settingsmanager.h"

class CliBridge : public QObject
{
    Q_OBJECT

public:
    explicit CliBridge(SettingsManager *settings, QObject *parent = nullptr);

    Q_INVOKABLE void call(const QString &method, const QStringList &params = QStringList(), const QString &wallet = QString());
    Q_INVOKABLE void callNamed(const QString &method, const QVariantMap &namedParams, const QString &wallet = QString());

signals:
    void success(const QVariant &result);
    void errorOccurred(const QString &errorMessage);

private:
    SettingsManager *m_settings;
    bool m_pathErrorShown;

    void executeCommand(const QStringList &command, const QString &method);
    QStringList buildCommand(const QString &method, const QStringList &params, const QString &wallet);
    QStringList buildNamedCommand(const QString &method, const QVariantMap &namedParams, const QString &wallet);
};

#endif // CLIBRIDGE_H
