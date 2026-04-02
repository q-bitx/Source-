#ifndef WALLETMANAGER_H
#define WALLETMANAGER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class SettingsManager;
class CliBridge;

class WalletManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool walletBusy READ walletBusy NOTIFY walletBusyChanged)
    Q_PROPERTY(QStringList loadedWallets READ loadedWallets NOTIFY loadedWalletsChanged)
    Q_PROPERTY(QStringList wallets READ wallets NOTIFY walletsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastInfo READ lastInfo NOTIFY lastInfoChanged)

public:
    explicit WalletManager(SettingsManager *settings, CliBridge *cliBridge, QObject *parent = nullptr);

    bool walletBusy() const { return m_walletBusy; }
    QStringList loadedWallets() const { return m_loadedWallets; }
    QStringList wallets() const { return m_loadedWallets; }
    QString lastError() const { return m_lastError; }
    QString lastInfo() const { return m_lastInfo; }

    Q_INVOKABLE void refreshWallets();
    Q_INVOKABLE void createWallet(const QString &name);
    Q_INVOKABLE void loadWallet(const QString &name);
    Q_INVOKABLE void backupWallet(const QString &walletName);
    Q_INVOKABLE void restoreWallet(const QString &name, const QString &folderPath);
    Q_INVOKABLE void restoreWalletFromFolder(const QString &folderPath);

    Q_INVOKABLE void importWalletFromPath(const QString &sourcePath, const QString &walletName = QString());
    Q_INVOKABLE QVariantMap backupWalletToPath(const QString &backupDir, const QString &walletName);
    Q_INVOKABLE QVariantMap normalizeUserPath(const QString &path, bool mustExist = false, bool mustBeWritable = false, bool allowCreate = false);
    Q_INVOKABLE QString deriveWalletNameFromFolder(const QString &folderPath);
    Q_INVOKABLE void exitWallet(bool deleteLocalCopy, const QString &walletName);
    Q_INVOKABLE void deleteWalletByName(const QString &walletName);
    Q_INVOKABLE QString getWalletPathForName(const QString &walletName) const;
    
    // Single-file wallet.dat operations
    Q_INVOKABLE QString getDefaultBackupDirectory();
    Q_INVOKABLE QString createWalletDatBackup(const QString &walletName);
    Q_INVOKABLE QVariantMap importWalletDirectory(const QString &directoryPath);
    Q_INVOKABLE void cleanupFailedImport(const QString &walletName);
    Q_INVOKABLE QVariantMap autoBackupCurrentWallet(const QString &walletName);
    Q_INVOKABLE QString getWalletDatPath(const QString &walletName);
    Q_INVOKABLE QVariantMap backupWalletToFile(const QString &destinationPath, const QString &walletName);

signals:
    void walletBusyChanged();
    void loadedWalletsChanged();
    void walletsChanged();
    void lastErrorChanged();
    void lastInfoChanged();
    void walletImported(const QString &walletName);
    void walletBackedUp(const QString &backupPath);
    void walletExited();
    void errorOccurred(const QString &message);
    void successMessage(const QString &message);
    void walletDatImported(const QString &walletName);
    void walletDatBackedUp(const QString &backupPath);

private:
    QPointer<SettingsManager> m_settings;
    QPointer<CliBridge> m_cliBridge;
    bool m_walletBusy;
    QStringList m_loadedWallets;
    QString m_lastError;
    QString m_lastInfo;
    QString m_pendingMethod;
    QString m_pendingBackupWalletName;
    QString m_lastBackupPath;

    void setLastError(const QString &err);
    void setLastInfo(const QString &info);
    void onCliSuccess(const QVariant &result);
    void onCliError(const QString &errorMessage);
    QString backupBasePath() const;

    void setWalletBusy(bool busy);
    QString resolveWalletName(const QString &walletDir, const QString &datadir);
    QString getWalletBasePath() const; // Returns datadir or datadir/wallets
    bool copyDirectory(const QString &src, const QString &dst);
    bool deleteDirectory(const QString &path);
    QString getWalletPath(const QString &walletName);
    
    // Helper functions for wallet management
    QString deriveWalletNameFromFolderInternal(const QString &folderPath);
    QString uniqueWalletDirName(const QString &datadir, const QString &desiredName);
    QString sanitizeWalletName(const QString &name);
    
    // Backup methods
    QVariantMap backupWalletToDefaultDirectory(const QString &walletName);
    bool isRunningUnderWSL() const;
    QVariantMap performBackup(const QString &srcDir, const QString &dstDir);
    QVariantMap validateBackupDestination(const QString &backupRoot);
    int copyDirectoryRecursive(const QString &srcDir, const QString &dstDir, QString &errorMsg);
};

#endif // WALLETMANAGER_H
