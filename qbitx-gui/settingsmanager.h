#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>

class SettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString qbitxCliPath READ qbitxCliPath WRITE setQbitxCliPath NOTIFY qbitxCliPathChanged)
    Q_PROPERTY(QString datadir READ datadir WRITE setDatadir NOTIFY datadirChanged)
    Q_PROPERTY(QString rpcuser READ rpcuser WRITE setRpcuser NOTIFY rpcuserChanged)
    Q_PROPERTY(QString rpcpassword READ rpcpassword WRITE setRpcpassword NOTIFY rpcpasswordChanged)
    Q_PROPERTY(QString network READ network WRITE setNetwork NOTIFY networkChanged)
    Q_PROPERTY(QString activeWallet READ activeWallet WRITE setActiveWallet NOTIFY activeWalletChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    QString qbitxCliPath() const { return m_qbitxCliPath; }
    void setQbitxCliPath(const QString &path);

    QString datadir() const { return m_datadir; }
    void setDatadir(const QString &dir);

    QString rpcuser() const { return m_rpcuser; }
    void setRpcuser(const QString &user);

    QString rpcpassword() const { return m_rpcpassword; }
    void setRpcpassword(const QString &password);

    QString network() const { return m_network; }
    void setNetwork(const QString &net);

    QString activeWallet() const { return m_activeWallet; }
    void setActiveWallet(const QString &wallet);

    Q_INVOKABLE void autoDetectCliPath();
    Q_INVOKABLE void setWalletOrigin(const QString &walletName, const QString &origin); // "imported" or "local"
    Q_INVOKABLE QString getWalletOrigin(const QString &walletName);

signals:
    void qbitxCliPathChanged();
    void datadirChanged();
    void rpcuserChanged();
    void rpcpasswordChanged();
    void networkChanged();
    void activeWalletChanged();

private:
    QSettings m_settings;
    QString m_qbitxCliPath;
    QString m_datadir;
    QString m_rpcuser;
    QString m_rpcpassword;
    QString m_network;
    QString m_activeWallet;

    void loadSettings();
};

#endif // SETTINGSMANAGER_H
