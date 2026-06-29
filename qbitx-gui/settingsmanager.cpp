#include "settingsmanager.h"
#include "clipathutil.h"
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_settings("qbitx", "qbitx-gui")
{
    loadSettings();
}

void SettingsManager::loadSettings()
{
    m_qbitxCliPath = m_settings.value("qbitx_cli_path", "").toString().trimmed();
    m_datadir = m_settings.value("datadir", "").toString();
    m_rpcuser = m_settings.value("rpcuser", "").toString();
    m_rpcpassword = m_settings.value("rpcpassword", "").toString();
    m_network = m_settings.value("network", "main").toString();
    m_useAutoDetectCli = m_settings.value("use_auto_detect_cli", true).toBool();
}

void SettingsManager::setQbitxCliPath(const QString &path)
{
    if (m_qbitxCliPath != path) {
        m_qbitxCliPath = path;
        m_settings.setValue("qbitx_cli_path", path);
        emit qbitxCliPathChanged();
    }
}

void SettingsManager::setDatadir(const QString &dir)
{
    if (m_datadir != dir) {
        m_datadir = dir;
        m_settings.setValue("datadir", dir);
        emit datadirChanged();
    }
}

void SettingsManager::setRpcuser(const QString &user)
{
    if (m_rpcuser != user) {
        m_rpcuser = user;
        m_settings.setValue("rpcuser", user);
        emit rpcuserChanged();
    }
}

void SettingsManager::setRpcpassword(const QString &password)
{
    if (m_rpcpassword != password) {
        m_rpcpassword = password;
        m_settings.setValue("rpcpassword", password);
        emit rpcpasswordChanged();
    }
}

void SettingsManager::setNetwork(const QString &net)
{
    if (m_network != net) {
        m_network = net;
        m_settings.setValue("network", net);
        emit networkChanged();
    }
}

void SettingsManager::setUseAutoDetectCli(bool on)
{
    if (m_useAutoDetectCli != on) {
        m_useAutoDetectCli = on;
        m_settings.setValue("use_auto_detect_cli", on);
        emit useAutoDetectCliChanged();
    }
}

void SettingsManager::setWalletOrigin(const QString &walletName, const QString &origin)
{
    m_settings.beginGroup("wallet_origins");
    m_settings.setValue(walletName, origin);
    m_settings.endGroup();
}

QString SettingsManager::getWalletOrigin(const QString &walletName)
{
    m_settings.beginGroup("wallet_origins");
    QString origin = m_settings.value(walletName, "local").toString();
    m_settings.endGroup();
    return origin;
}

QString SettingsManager::effectiveQbitxCliPath() const
{
    QByteArray env = qgetenv("QBITX_CLI_PATH");
    QString path;
    if (!env.isEmpty())
        path = QString::fromUtf8(env).trimmed();
    if (path.isEmpty())
        path = m_qbitxCliPath;
    if (path.isEmpty())
        path = discoverQbitxCliExecutable();
    if (QDir::isRelativePath(path))
        path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path);
    return QDir::cleanPath(path);
}

void SettingsManager::setLastCliCheckError(const QString &err)
{
    if (m_lastCliCheckError != err) {
        m_lastCliCheckError = err;
        emit lastCliCheckErrorChanged();
    }
}

bool SettingsManager::checkCliAvailable(QString *error)
{
    QString path = effectiveQbitxCliPath();
    QFileInfo fi(path);
    if (!fi.exists()) {
        QString msg = QString("qbitx-cli not found: %1").arg(path);
        setLastCliCheckError(msg);
        if (error) *error = msg;
        return false;
    }
    if (!fi.isExecutable()) {
        QString msg = QString("qbitx-cli is not executable: %1").arg(path);
        setLastCliCheckError(msg);
        if (error) *error = msg;
        return false;
    }
    setLastCliCheckError(QString());
    if (error) *error = QString();
    return true;
}

void SettingsManager::autoDetectCliPath()
{
    const QString found = discoverQbitxCliExecutable();
    if (!found.isEmpty())
        setQbitxCliPath(found);
}

