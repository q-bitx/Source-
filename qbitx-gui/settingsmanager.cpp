#include "settingsmanager.h"
#include <QStandardPaths>
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
    m_qbitxCliPath = m_settings.value("qbitx_cli_path", "").toString();
    m_datadir = m_settings.value("datadir", "").toString();
    m_rpcuser = m_settings.value("rpcuser", "").toString();
    m_rpcpassword = m_settings.value("rpcpassword", "").toString();
    m_network = m_settings.value("network", "main").toString();
    m_activeWallet = m_settings.value("active_wallet", "").toString();
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

void SettingsManager::setActiveWallet(const QString &wallet)
{
    if (m_activeWallet != wallet) {
        m_activeWallet = wallet;
        m_settings.setValue("active_wallet", wallet);
        emit activeWalletChanged();
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

void SettingsManager::autoDetectCliPath()
{
    QString appDir = QCoreApplication::applicationDirPath();
    
    // Check in order:
    // a) applicationDirPath() + "/qbitx-cli"
    QString path1 = appDir + "/qbitx-cli";
    QFileInfo info1(path1);
    if (info1.exists() && info1.isExecutable()) {
        setQbitxCliPath(info1.absoluteFilePath());
        return;
    }
    
    // b) applicationDirPath() + "/../qbitx-cli"
    QString path2 = appDir + "/../qbitx-cli";
    QFileInfo info2(path2);
    if (info2.exists() && info2.isExecutable()) {
        setQbitxCliPath(info2.absoluteFilePath());
        return;
    }
    
    // c) QStandardPaths::findExecutable("qbitx-cli")
    QString foundPath = QStandardPaths::findExecutable("qbitx-cli");
    if (!foundPath.isEmpty()) {
        setQbitxCliPath(foundPath);
        return;
    }
    
    // Not found - leave path empty (don't set anything)
}

