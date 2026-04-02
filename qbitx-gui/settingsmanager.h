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
    Q_PROPERTY(bool useAutoDetectCli READ useAutoDetectCli WRITE setUseAutoDetectCli NOTIFY useAutoDetectCliChanged)
    Q_PROPERTY(QString lastCliCheckError READ lastCliCheckError NOTIFY lastCliCheckErrorChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    QString qbitxCliPath() const { return m_qbitxCliPath; }
    void setQbitxCliPath(const QString &path);
    /** Effective path: QBITX_CLI_PATH env, else stored path, else default build_wallet2 path; relative resolved against app dir. */
    Q_INVOKABLE QString effectiveQbitxCliPath() const;
    /** Returns true if effective path exists and is executable; if error is non-null, sets it. Also updates lastCliCheckError. QML calls with no args and reads lastCliCheckError. */
    Q_INVOKABLE bool checkCliAvailable(QString *error = nullptr);
    QString lastCliCheckError() const { return m_lastCliCheckError; }

    QString datadir() const { return m_datadir; }
    void setDatadir(const QString &dir);

    QString rpcuser() const { return m_rpcuser; }
    void setRpcuser(const QString &user);

    QString rpcpassword() const { return m_rpcpassword; }
    void setRpcpassword(const QString &password);

    QString network() const { return m_network; }
    void setNetwork(const QString &net);

    bool useAutoDetectCli() const { return m_useAutoDetectCli; }
    void setUseAutoDetectCli(bool on);

    Q_INVOKABLE void autoDetectCliPath();
    Q_INVOKABLE void setWalletOrigin(const QString &walletName, const QString &origin); // "imported" or "local"
    Q_INVOKABLE QString getWalletOrigin(const QString &walletName);

signals:
    void qbitxCliPathChanged();
    void datadirChanged();
    void rpcuserChanged();
    void rpcpasswordChanged();
    void networkChanged();
    void useAutoDetectCliChanged();
    void lastCliCheckErrorChanged();

private:
    QSettings m_settings;
    QString m_qbitxCliPath;
    QString m_datadir;
    QString m_rpcuser;
    QString m_rpcpassword;
    QString m_network;
    bool m_useAutoDetectCli;
    mutable QString m_lastCliCheckError;

    void loadSettings();
    void setLastCliCheckError(const QString &err);
};

#endif // SETTINGSMANAGER_H
