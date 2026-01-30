#ifndef WALLETMANAGER_H
#define WALLETMANAGER_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class SettingsManager;

class WalletManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool walletBusy READ walletBusy NOTIFY walletBusyChanged)

public:
    explicit WalletManager(SettingsManager *settings, QObject *parent = nullptr);

    bool walletBusy() const { return m_walletBusy; }

    Q_INVOKABLE void importWalletFromPath(const QString &sourcePath, const QString &walletName = QString());
    Q_INVOKABLE QVariantMap backupWalletToPath(const QString &backupDir);
    Q_INVOKABLE QVariantMap normalizeUserPath(const QString &path, bool mustExist = false, bool mustBeWritable = false, bool allowCreate = false);
    Q_INVOKABLE QString deriveWalletNameFromFolder(const QString &folderPath);
    Q_INVOKABLE void exitWallet(bool deleteLocalCopy);
    Q_INVOKABLE void deleteWalletByName(const QString &walletName);
    Q_INVOKABLE QString getWalletPathForName(const QString &walletName) const;
    
    // Single-file wallet.dat operations
    Q_INVOKABLE QString getDefaultBackupDirectory();
    Q_INVOKABLE QString createWalletDatBackup(const QString &walletName);
    Q_INVOKABLE QVariantMap importWalletDirectory(const QString &directoryPath);
    Q_INVOKABLE void cleanupFailedImport(const QString &walletName);
    Q_INVOKABLE QVariantMap autoBackupCurrentWallet();
    Q_INVOKABLE QString getWalletDatPath(const QString &walletName);
    Q_INVOKABLE QVariantMap backupWalletToFile(const QString &destinationPath);
    Q_INVOKABLE QVariantMap backupWallet(const QString &walletName);

signals:
    void walletBusyChanged();
    void walletImported(const QString &walletName);
    void walletBackedUp(const QString &backupPath);
    void walletExited();
    void errorOccurred(const QString &message);
    void successMessage(const QString &message);
    void walletDatImported(const QString &walletName);
    void walletDatBackedUp(const QString &backupPath);

private:
    SettingsManager *m_settings;
    bool m_walletBusy;

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
    bool isRunningUnderWSL() const;
    QVariantMap performBackup(const QString &srcDir, const QString &dstDir);
    QVariantMap validateBackupDestination(const QString &backupRoot);
    int copyDirectoryRecursive(const QString &srcDir, const QString &dstDir, QString &errorMsg);
};

#endif // WALLETMANAGER_H
