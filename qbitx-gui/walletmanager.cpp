#include "walletmanager.h"
#include "settingsmanager.h"
#include "pathutil.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QDirIterator>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonParseError>

WalletManager::WalletManager(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_walletBusy(false)
{
}

void WalletManager::setWalletBusy(bool busy)
{
    if (m_walletBusy != busy) {
        m_walletBusy = busy;
        emit walletBusyChanged();
    }
}

QString WalletManager::getWalletBasePath() const
{
    QString datadir = m_settings->datadir();
    if (datadir.isEmpty()) {
        return QString();
    }
    
    // Check if wallets/ subdirectory exists
    QDir walletsDir(datadir + "/wallets");
    if (walletsDir.exists()) {
        return datadir + "/wallets";
    }
    
    // Otherwise use datadir directly
    return datadir;
}

QString WalletManager::deriveWalletNameFromFolder(const QString &folderPath)
{
    return deriveWalletNameFromFolderInternal(folderPath);
}

QString WalletManager::deriveWalletNameFromFolderInternal(const QString &folderPath)
{
    if (folderPath.isEmpty()) {
        return QString();
    }
    
    // Get basename of the folder path
    QFileInfo info(folderPath);
    QString basename = info.baseName();
    
    // Check if basename matches pattern like "12-backup-20260124-071840"
    QRegularExpression backupPattern("^(\\d+)-");
    QRegularExpressionMatch match = backupPattern.match(basename);
    
    if (match.hasMatch()) {
        // Return the numeric prefix (e.g., "12")
        return sanitizeWalletName(match.captured(1));
    }
    
    // Otherwise return sanitized full basename
    return sanitizeWalletName(basename);
}

QString WalletManager::sanitizeWalletName(const QString &name)
{
    // Only allow [A-Za-z0-9_-]
    QString sanitized = name;
    sanitized.replace(QRegularExpression("[^A-Za-z0-9_-]"), "");
    
    // Ensure it's not empty
    if (sanitized.isEmpty()) {
        sanitized = "wallet";
    }
    
    return sanitized;
}

QString WalletManager::uniqueWalletDirName(const QString &datadir, const QString &desiredName)
{
    QString candidate = desiredName;
    int suffix = 2;
    
    while (QDir(datadir + "/" + candidate).exists()) {
        candidate = desiredName + "_" + QString::number(suffix);
        suffix++;
    }
    
    return candidate;
}

void WalletManager::importWalletFromPath(const QString &sourcePath, const QString &walletName)
{
    if (m_walletBusy) {
        emit errorOccurred("Wallet operation in progress");
        return;
    }

    // Step 1: Normalize user-entered path
    QString resolvedSourcePath = PathUtil::resolveUserPath(sourcePath);
    qDebug() << "Import wallet - Original path:" << sourcePath << "Resolved path:" << resolvedSourcePath;

    if (resolvedSourcePath.isEmpty()) {
        emit errorOccurred("Source path is empty");
        return;
    }

    QFileInfo sourceInfo(resolvedSourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        emit errorOccurred("Source path does not exist or is not a directory: " + resolvedSourcePath);
        return;
    }

    // Step 2: Verify wallet.dat exists
    QFileInfo walletDat(resolvedSourcePath + "/wallet.dat");
    if (!walletDat.exists()) {
        emit errorOccurred("wallet.dat not found");
        return;
    }

    // Step 3: Determine wallet name
    QString finalWalletName;
    if (walletName.isEmpty()) {
        // Auto-derive from folder name
        finalWalletName = deriveWalletNameFromFolderInternal(resolvedSourcePath);
    } else {
        // Use provided name (sanitized)
        finalWalletName = sanitizeWalletName(walletName);
    }

    if (finalWalletName.isEmpty()) {
        emit errorOccurred("Could not determine wallet name");
        return;
    }

    QString basePath = getWalletBasePath();
    if (basePath.isEmpty()) {
        emit errorOccurred("Data directory not configured in Settings");
        return;
    }

    // Step 4: Handle name collisions
    finalWalletName = uniqueWalletDirName(basePath, finalWalletName);
    QString targetPath = basePath + "/" + finalWalletName;

    qDebug() << "Import wallet - Final wallet name:" << finalWalletName << "Target path:" << targetPath;

    setWalletBusy(true);
    
    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        if (!baseDir.mkpath(".")) {
            setWalletBusy(false);
            emit errorOccurred("Failed to create wallet base directory: " + basePath);
            return;
        }
    }

    // Step 6: Copy directory recursively
    QString errorMsg;
    int filesCopied = copyDirectoryRecursive(resolvedSourcePath, targetPath, errorMsg);
    
    if (filesCopied < 0) {
        setWalletBusy(false);
        emit errorOccurred("Failed to copy wallet directory: " + errorMsg);
        return;
    }

    if (filesCopied == 0) {
        setWalletBusy(false);
        emit errorOccurred("Failed to copy wallet directory");
        return;
    }

    // Verify wallet.dat was copied
    QFileInfo copiedWalletDat(targetPath + "/wallet.dat");
    if (!copiedWalletDat.exists()) {
        setWalletBusy(false);
        emit errorOccurred("Failed to copy wallet directory: wallet.dat missing in destination");
        return;
    }

    setWalletBusy(false);
    
    qDebug() << "Import wallet completed - Files copied:" << filesCopied << "Final wallet name:" << finalWalletName;
    emit walletImported(finalWalletName);
}

QVariantMap WalletManager::backupWalletToPath(const QString &backupDir)
{
    QVariantMap result;
    result["ok"] = false;
    result["dstDir"] = QString();
    result["filesCopied"] = 0;
    result["error"] = QString();

    if (m_walletBusy) {
        result["error"] = "Wallet operation in progress";
        emit errorOccurred("Wallet operation in progress");
        return result;
    }

    QString activeWallet = m_settings->activeWallet();
    if (activeWallet.isEmpty()) {
        result["error"] = "No active wallet to backup";
        emit errorOccurred("No active wallet to backup");
        return result;
    }

    QString walletPath = getWalletPath(activeWallet);
    if (walletPath.isEmpty() || !QDir(walletPath).exists()) {
        result["error"] = "Wallet directory not found: " + activeWallet;
        emit errorOccurred("Wallet directory not found: " + activeWallet);
        return result;
    }

    // Validate source directory
    QFileInfo srcInfo(walletPath);
    if (!srcInfo.exists() || !srcInfo.isDir()) {
        result["error"] = "Source wallet directory is invalid";
        emit errorOccurred("Source wallet directory is invalid");
        return result;
    }

    // Check if source contains at least one file
    QDir srcDir(walletPath);
    QFileInfo walletDat(walletPath + "/wallet.dat");
    QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty() && !walletDat.exists()) {
        result["error"] = "Source wallet directory is empty";
        emit errorOccurred("Source wallet directory is empty");
        return result;
    }

    // Step 1: Normalize user-entered backup directory path
    QString resolvedBackupDir = PathUtil::resolveUserPath(backupDir);
    qDebug() << "Backup wallet - Original path:" << backupDir << "Resolved path:" << resolvedBackupDir;
    
    if (resolvedBackupDir.isEmpty()) {
        result["error"] = "Backup directory path is empty";
        emit errorOccurred(result["error"].toString());
        return result;
    }
    
    // Step 2: Ensure backup root exists and is writable
    QFileInfo backupRootInfo(resolvedBackupDir);
    if (!backupRootInfo.exists()) {
        // Try to create it using resolved path
        QDir dir;
        if (!dir.mkpath(resolvedBackupDir)) {
            result["error"] = QString("Cannot create backup directory: %1").arg(resolvedBackupDir);
            emit errorOccurred(result["error"].toString());
            return result;
        }
        backupRootInfo.refresh();
    }
    
    if (!backupRootInfo.isDir()) {
        result["error"] = QString("Backup path is not a directory: %1").arg(resolvedBackupDir);
        emit errorOccurred(result["error"].toString());
        return result;
    }
    
    // Check writability
    QDir backupRootDir(resolvedBackupDir);
    QString testFilePath = backupRootDir.absoluteFilePath(".qbitx_backup_test");
    QFile testFile(testFilePath);
    if (!testFile.open(QIODevice::WriteOnly)) {
        result["error"] = QString("Backup directory is not writable: %1. Check permissions.").arg(resolvedBackupDir);
        emit errorOccurred(result["error"].toString());
        return result;
    }
    testFile.close();
    testFile.remove();

    // Step 3: Create timestamped backup directory name
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString backupPath = resolvedBackupDir + "/" + activeWallet + "-backup-" + timestamp;

    qDebug() << "Backup wallet - Source:" << walletPath << "Destination:" << backupPath;

    setWalletBusy(true);

    // Step 5: Copy directory recursively
    QString errorMsg;
    int filesCopied = copyDirectoryRecursive(walletPath, backupPath, errorMsg);
    
    setWalletBusy(false);
    
    if (filesCopied < 0) {
        result["error"] = errorMsg.isEmpty() ? "Failed to copy wallet directory" : errorMsg;
        emit errorOccurred(result["error"].toString());
        return result;
    }

    if (filesCopied == 0) {
        result["error"] = "Backup produced no files. Source directory may be empty or inaccessible.";
        emit errorOccurred(result["error"].toString());
        return result;
    }

    // Step 6: Verify backup success - check that target contains wallet.dat
    QFileInfo backupWalletDat(backupPath + "/wallet.dat");
    if (!backupWalletDat.exists()) {
        result["error"] = QString("Backup verification failed: wallet.dat missing in backup directory: %1").arg(backupPath);
        emit errorOccurred(result["error"].toString());
        return result;
    }

    // Step 7: Success
    result["ok"] = true;
    result["dstDir"] = backupPath;
    result["filesCopied"] = filesCopied;
    result["error"] = QString();
    result["resolvedPath"] = backupPath;

    qDebug() << "Backup completed successfully - Files copied:" << filesCopied << "Destination:" << backupPath;

    emit walletBackedUp(backupPath);
    emit successMessage(QString("Wallet backed up to: %1").arg(backupPath));

    return result;
}

void WalletManager::exitWallet(bool deleteLocalCopy)
{
    QString activeWallet = m_settings->activeWallet();
    if (activeWallet.isEmpty()) {
        emit errorOccurred("No active wallet loaded");
        return;
    }

    // Note: unloadwallet RPC is called from QML before this function
    
    setWalletBusy(true);

    if (deleteLocalCopy) {
        // Use the same datadir that CliBridge uses (from settings, no fallbacks)
        QString datadir = m_settings->datadir();
        if (datadir.isEmpty()) {
            setWalletBusy(false);
            emit errorOccurred("Data directory not configured");
            return;
        }
        
        // Construct wallet path: check if wallets/ subdirectory exists
        QString walletPath;
        QDir walletsDir(datadir + "/wallets");
        if (walletsDir.exists()) {
            walletPath = datadir + "/wallets/" + activeWallet;
        } else {
            walletPath = datadir + "/" + activeWallet;
        }
        
        if (QDir(walletPath).exists()) {
            if (!deleteDirectory(walletPath)) {
                setWalletBusy(false);
                emit errorOccurred("Failed to delete wallet directory: " + walletPath);
                return;
            }
        }
    }

    setWalletBusy(false);
    emit walletExited();
}

void WalletManager::deleteWalletByName(const QString &walletName)
{
    if (m_walletBusy) {
        emit errorOccurred("Wallet operation in progress");
        return;
    }

    QString walletPath = getWalletPath(walletName);
    if (walletPath.isEmpty() || !QDir(walletPath).exists()) {
        emit errorOccurred("Wallet directory not found: " + walletName);
        return;
    }

    setWalletBusy(true);

    if (!deleteDirectory(walletPath)) {
        setWalletBusy(false);
        emit errorOccurred("Failed to delete wallet directory");
        return;
    }

    setWalletBusy(false);
    emit successMessage("Wallet deleted: " + walletName);
}

QString WalletManager::resolveWalletName(const QString &walletDir, const QString &datadir)
{
    QFileInfo dirInfo(walletDir);
    QString baseName = dirInfo.fileName();
    QString walletName = baseName;
    int suffix = 1;

    QString basePath = getWalletBasePath();
    if (basePath.isEmpty()) {
        basePath = datadir;
    }

    // Handle collisions
    while (QDir(basePath + "/" + walletName).exists()) {
        walletName = baseName + "_" + QString::number(suffix);
        suffix++;
    }

    return walletName;
}

bool WalletManager::copyDirectory(const QString &src, const QString &dst)
{
    QDir srcDir(src);
    if (!srcDir.exists()) {
        return false;
    }

    QDir dstDir;
    if (!dstDir.mkpath(dst)) {
        return false;
    }

    QStringList files = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &file : files) {
        QString srcPath = src + "/" + file;
        QString dstPath = dst + "/" + file;

        QFileInfo fileInfo(srcPath);
        if (fileInfo.isDir()) {
            if (!copyDirectory(srcPath, dstPath)) {
                return false;
            }
        } else {
            if (!QFile::copy(srcPath, dstPath)) {
                return false;
            }
        }
    }

    return true;
}

bool WalletManager::deleteDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return true; // Already deleted
    }

    QStringList files = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &file : files) {
        QString filePath = path + "/" + file;
        QFileInfo fileInfo(filePath);
        if (fileInfo.isDir()) {
            if (!deleteDirectory(filePath)) {
                return false;
            }
        } else {
            if (!QFile::remove(filePath)) {
                return false;
            }
        }
    }

    return dir.rmdir(path);
}

QString WalletManager::getWalletPath(const QString &walletName)
{
    QString basePath = getWalletBasePath();
    if (basePath.isEmpty()) {
        return QString();
    }
    return basePath + "/" + walletName;
}

QString WalletManager::getWalletPathForName(const QString &walletName) const
{
    QString basePath = const_cast<WalletManager*>(this)->getWalletBasePath();
    if (basePath.isEmpty()) {
        return QString();
    }
    return basePath + "/" + walletName;
}

bool WalletManager::isRunningUnderWSL() const
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Check /proc/version for WSL indicators
    QFile procVersion("/proc/version");
    if (procVersion.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&procVersion);
        QString content = in.readAll().toLower();
        if (content.contains("microsoft") || content.contains("wsl")) {
            return true;
        }
    }
    
    // Check WSL environment variable
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains("WSL_DISTRO_NAME") || env.contains("WSLENV")) {
        return true;
    }
    
    // Check if /mnt/c exists (common WSL mount point)
    if (QDir("/mnt/c").exists()) {
        return true;
    }
#endif
    return false;
}

QVariantMap WalletManager::normalizeUserPath(const QString &path, bool mustExist, bool mustBeWritable, bool allowCreate)
{
    QVariantMap result;
    result["ok"] = false;
    result["path"] = QString();
    result["error"] = QString();
    result["resolvedPath"] = QString();

    if (path.isEmpty()) {
        result["error"] = "Path is empty";
        return result;
    }

    // Use PathUtil to resolve the path
    QString resolved = PathUtil::resolveUserPath(path);
    if (resolved.isEmpty()) {
        result["error"] = "Resolved path is empty";
        return result;
    }

    QFileInfo pathInfo(resolved);
    
    if (mustExist) {
        if (!pathInfo.exists()) {
            if (allowCreate) {
                // Try to create the directory
                QDir dir;
                if (!dir.mkpath(resolved)) {
                    result["error"] = QString("Path does not exist and cannot be created: %1").arg(resolved);
                    return result;
                }
                // Re-check after creation
                pathInfo.refresh();
                if (!pathInfo.exists()) {
                    result["error"] = QString("Failed to create directory: %1").arg(resolved);
                    return result;
                }
            } else {
                result["error"] = QString("Path does not exist: %1").arg(resolved);
                return result;
            }
        }
        if (!pathInfo.isDir()) {
            result["error"] = QString("Path is not a directory: %1").arg(resolved);
            return result;
        }
        if (mustBeWritable) {
            // Check writability by attempting to create a test file
            QDir dir(resolved);
            QString testFilePath = dir.absoluteFilePath(".qbitx_backup_test");
            QFile testFile(testFilePath);
            if (!testFile.open(QIODevice::WriteOnly)) {
                result["error"] = QString("Directory is not writable: %1. Check permissions.").arg(resolved);
                return result;
            }
            testFile.close();
            testFile.remove();
        }
    } else {
        // For non-strict paths, allow creating parent directories if needed
        QDir parentDir = pathInfo.dir();
        if (!parentDir.exists() && allowCreate) {
            // Try to create parent directory
            if (!parentDir.mkpath(".")) {
                result["error"] = QString("Cannot create parent directory: %1").arg(parentDir.absolutePath());
                return result;
            }
        }
    }
    
    result["ok"] = true;
    result["path"] = resolved;
    result["resolvedPath"] = resolved; // Show resolved path to user
    return result;
}

QVariantMap WalletManager::validateBackupDestination(const QString &backupRoot)
{
    QVariantMap result;
    result["ok"] = false;
    result["path"] = QString();
    result["error"] = QString();

    // Normalize path (strict mode: must exist, must be writable)
    QVariantMap normResult = normalizeUserPath(backupRoot, true, true);
    if (!normResult["ok"].toBool()) {
        result["error"] = normResult["error"].toString();
        return result;
    }

    QString normalized = normResult["path"].toString();
    QFileInfo pathInfo(normalized);

    // Additional validation
    if (!pathInfo.exists()) {
        result["error"] = "Selected backup folder does not exist. Please choose an existing directory.";
        return result;
    }

    if (!pathInfo.isDir()) {
        result["error"] = "Selected path is not a directory: " + normalized;
        return result;
    }

    // Check writability by attempting to create a test file
    QDir dir(normalized);
    QString testFilePath = dir.absoluteFilePath(".qbitx_backup_test");
    QFile testFile(testFilePath);
    if (!testFile.open(QIODevice::WriteOnly)) {
        result["error"] = "Directory is not writable: " + normalized + ". Check permissions.";
        return result;
    }
    testFile.close();
    testFile.remove();

    result["ok"] = true;
    result["path"] = normalized;
    return result;
}

QVariantMap WalletManager::performBackup(const QString &srcDir, const QString &dstDir)
{
    QVariantMap result;
    result["ok"] = false;
    result["dstDir"] = dstDir;
    result["filesCopied"] = 0;
    result["error"] = QString();

    // Validate source directory
    QFileInfo srcInfo(srcDir);
    if (!srcInfo.exists() || !srcInfo.isDir()) {
        result["error"] = "Source directory does not exist or is not a directory: " + srcDir;
        return result;
    }

    // Check if source contains files
    QDir src(srcDir);
    QFileInfo walletDat(srcDir + "/wallet.dat");
    QStringList entries = src.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty() && !walletDat.exists()) {
        result["error"] = "Source directory is empty: " + srcDir;
        return result;
    }

    // Create destination directory (only the timestamped subdirectory)
    QDir dst;
    if (!dst.mkpath(dstDir)) {
        result["error"] = "Failed to create backup directory: " + dstDir + ". Check parent directory permissions.";
        return result;
    }

    // Verify destination was created
    QFileInfo dstInfo(dstDir);
    if (!dstInfo.exists() || !dstInfo.isDir()) {
        result["error"] = "Failed to create backup directory: " + dstDir;
        return result;
    }

    // Perform recursive copy
    QString errorMsg;
    int filesCopied = copyDirectoryRecursive(srcDir, dstDir, errorMsg);

    if (filesCopied < 0) {
        result["error"] = errorMsg.isEmpty() ? "Copy operation failed" : errorMsg;
        return result;
    }

    if (filesCopied == 0) {
        result["error"] = "Backup produced no files. Source directory may be empty or inaccessible.";
        return result;
    }

    // Verify copy success: check that target contains wallet.dat OR any expected wallet files
    QDir dstDirCheck(dstDir);
    QFileInfo backupWalletDat(dstDir + "/wallet.dat");
    QStringList backupEntries = dstDirCheck.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (!backupWalletDat.exists() && backupEntries.isEmpty()) {
        result["error"] = QString("Backup verification failed: Target directory is empty or missing wallet.dat: %1").arg(dstDir);
        return result;
    }
    
    // Log successful backup
    qDebug() << "Backup verification passed - dstDir:" << dstDir << "contains wallet.dat:" << backupWalletDat.exists() << "files:" << backupEntries.size();

    result["ok"] = true;
    result["filesCopied"] = filesCopied;
    return result;
}

int WalletManager::copyDirectoryRecursive(const QString &srcDir, const QString &dstDir, QString &errorMsg)
{
    QDir src(srcDir);
    if (!src.exists()) {
        errorMsg = "Source directory does not exist: " + srcDir;
        return -1;
    }

    int filesCopied = 0;
    QDirIterator it(srcDir, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString srcPath = it.next();
        QFileInfo srcInfo(srcPath);

        // Calculate relative path
        QString relPath = src.relativeFilePath(srcPath);
        QString dstPath = QDir(dstDir).filePath(relPath);

        if (srcInfo.isDir()) {
            // Create directory
            QDir dst;
            if (!dst.mkpath(dstPath)) {
                errorMsg = "Failed to create directory: " + dstPath;
                return -1;
            }
        } else if (srcInfo.isFile()) {
            // Ensure parent directory exists
            QFileInfo dstFileInfo(dstPath);
            QDir dstParent = dstFileInfo.dir();
            if (!dstParent.exists()) {
                if (!dstParent.mkpath(".")) {
                    errorMsg = "Failed to create parent directory: " + dstParent.absolutePath();
                    return -1;
                }
            }

            // Remove destination file if it exists (avoid partial overwrite)
            if (QFile::exists(dstPath)) {
                if (!QFile::remove(dstPath)) {
                    errorMsg = "Failed to remove existing file: " + dstPath;
                    return -1;
                }
            }

            // Copy file
            if (!QFile::copy(srcPath, dstPath)) {
                QFile file(srcPath);
                QString fileError = file.errorString();
                errorMsg = QString("Failed to copy file %1 to %2: %3").arg(srcPath).arg(dstPath).arg(fileError);
                return -1;
            }

            // Verify copy succeeded
            QFileInfo dstInfo(dstPath);
            if (!dstInfo.exists()) {
                errorMsg = "Copy verification failed: destination file does not exist: " + dstPath;
                return -1;
            }

            // Optional: verify file sizes match
            if (srcInfo.size() != dstInfo.size()) {
                errorMsg = QString("Copy verification failed: file size mismatch. Source: %1 bytes, Destination: %2 bytes")
                          .arg(srcInfo.size()).arg(dstInfo.size());
                return -1;
            }

            filesCopied++;
        }
        // Note: Symlinks are treated as files - if copy fails, we'll get an error above
    }

    return filesCopied;
}

QString WalletManager::getDefaultBackupDirectory()
{
    // Get default backup directory: Documents/QBitX/Backups
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documentsPath.isEmpty()) {
        // Fallback to home directory
        documentsPath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    
    QString backupDir = documentsPath + "/QBitX/Backups";
    
    // Ensure directory exists
    QDir dir;
    if (!dir.mkpath(backupDir)) {
        qWarning() << "Failed to create backup directory:" << backupDir;
        return QString(); // Return empty string on failure
    }
    
    return backupDir;
}

QString WalletManager::createWalletDatBackup(const QString &walletName)
{
    if (walletName.isEmpty()) {
        return QString();
    }
    
    QString backupDir = getDefaultBackupDirectory();
    if (backupDir.isEmpty()) {
        return QString();
    }
    
    // Create timestamped filename: walletname-backup-YYYYMMDD-HHMMSS-wallet.dat
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    QString filename = QString("%1-backup-%2-wallet.dat").arg(walletName).arg(timestamp);
    QString backupPath = backupDir + "/" + filename;
    
    qDebug() << "Generated backup path:" << backupPath;
    return backupPath;
}

QVariantMap WalletManager::importWalletDirectory(const QString &directoryPath)
{
    QVariantMap result;
    result["success"] = false;
    result["walletName"] = QString();
    result["error"] = QString();
    
    if (m_walletBusy) {
        result["error"] = "Wallet operation in progress";
        return result;
    }
    
    setWalletBusy(true);
    
    // Validate source - must be a directory
    QFileInfo sourceInfo(directoryPath);
    if (!sourceInfo.exists()) {
        result["error"] = "Source directory does not exist: " + directoryPath;
        setWalletBusy(false);
        return result;
    }
    
    if (!sourceInfo.isDir()) {
        result["error"] = "Source path is not a directory. Please select a directory containing wallet.dat";
        setWalletBusy(false);
        return result;
    }
    
    // Validate that directory contains wallet.dat
    QString walletDatPath = directoryPath + "/wallet.dat";
    QFileInfo walletDatInfo(walletDatPath);
    if (!walletDatInfo.exists()) {
        result["error"] = "wallet.dat not found in directory: " + directoryPath;
        setWalletBusy(false);
        return result;
    }
    
    // Wallet name MUST be derived strictly from directory name (never rename)
    QFileInfo dirInfo(directoryPath);
    QString walletName = dirInfo.fileName();
    if (walletName.isEmpty() || walletName == "." || walletName == "..") {
        result["error"] = "Invalid directory name for wallet";
        setWalletBusy(false);
        return result;
    }
    
    // Use the same datadir that CliBridge uses (from settings, no fallbacks)
    QString datadir = m_settings->datadir();
    if (datadir.isEmpty()) {
        result["error"] = "Data directory not configured";
        setWalletBusy(false);
        return result;
    }
    
    // Create wallet directory: <datadir>/wallets/<walletName>/
    QString walletDir = datadir + "/wallets/" + walletName;
    QDir dir;
    if (!dir.mkpath(walletDir)) {
        result["error"] = "Failed to create wallet directory: " + walletDir;
        setWalletBusy(false);
        return result;
    }
    
    // Check if wallet already exists
    if (QDir(walletDir).exists() && !QDir(walletDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
        result["error"] = QString("Wallet '%1' already exists at %2").arg(walletName).arg(walletDir);
        setWalletBusy(false);
        return result;
    }
    
    // Copy the entire directory recursively
    QString errorMsg;
    int filesCopied = copyDirectoryRecursive(directoryPath, walletDir, errorMsg);
    
    if (filesCopied < 0) {
        // Clean up on failure
        deleteDirectory(walletDir);
        result["error"] = errorMsg.isEmpty() ? "Failed to copy wallet directory" : errorMsg;
        setWalletBusy(false);
        return result;
    }
    
    if (filesCopied == 0) {
        // Clean up on failure
        deleteDirectory(walletDir);
        result["error"] = "Failed to copy wallet directory: no files copied";
        setWalletBusy(false);
        return result;
    }
    
    // Verify wallet.dat was copied
    QFileInfo copiedWalletDat(walletDir + "/wallet.dat");
    if (!copiedWalletDat.exists()) {
        // Clean up on failure
        deleteDirectory(walletDir);
        result["error"] = "Failed to copy wallet directory: wallet.dat missing in destination";
        setWalletBusy(false);
        return result;
    }
    
    qDebug() << "Successfully imported wallet directory from" << directoryPath << "to" << walletDir;
    
    result["success"] = true;
    result["walletName"] = walletName;
    setWalletBusy(false);
    
    emit walletDatImported(walletName);
    return result;
}

void WalletManager::cleanupFailedImport(const QString &walletName)
{
    if (walletName.isEmpty()) {
        return;
    }
    
    QString datadir = getWalletBasePath();
    if (datadir.isEmpty()) {
        return;
    }
    
    QString walletDir = datadir + "/" + walletName;
    QDir dir(walletDir);
    
    if (dir.exists()) {
        // Remove the wallet directory recursively
        if (dir.removeRecursively()) {
            qDebug() << "Cleaned up failed import wallet directory:" << walletDir;
        } else {
            qWarning() << "Failed to clean up wallet directory:" << walletDir;
        }
    }
}

QString WalletManager::getWalletDatPath(const QString &walletName)
{
    if (walletName.isEmpty()) {
        return QString();
    }
    
    // Get datadir (prefer configured, otherwise use default)
    QString datadir = m_settings->datadir();
    if (datadir.isEmpty()) {
        // Default datadir paths per platform
        QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        if (homePath.isEmpty()) {
            return QString();
        }
        
#ifdef Q_OS_WIN
        // Windows: %APPDATA%\QBitX
        QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appDataPath.isEmpty()) {
            // Remove the app name from AppDataLocation (it includes "qbitx-gui")
            QDir appDataDir(appDataPath);
            appDataDir.cdUp();
            datadir = appDataDir.absoluteFilePath("QBitX");
        } else {
            datadir = homePath + "/AppData/Roaming/QBitX";
        }
#elif defined(Q_OS_MACOS)
        // macOS: ~/Library/Application Support/QBitX
        datadir = homePath + "/Library/Application Support/QBitX";
#else
        // Linux: ~/.qbitx
        datadir = homePath + "/.qbitx";
#endif
    }
    
    // Check if wallets/ subdirectory exists
    QString walletPath = datadir + "/wallets/" + walletName + "/wallet.dat";
    QFileInfo walletInfo(walletPath);
    if (walletInfo.exists()) {
        return walletPath;
    }
    
    // Fallback to datadir directly
    walletPath = datadir + "/" + walletName + "/wallet.dat";
    walletInfo.setFile(walletPath);
    if (walletInfo.exists()) {
        return walletPath;
    }
    
    return QString();
}

QVariantMap WalletManager::backupWalletToFile(const QString &destinationPath)
{
    QVariantMap result;
    result["ok"] = false;
    result["backupPath"] = QString();
    result["error"] = QString();
    
    if (!m_settings) {
        result["error"] = "Settings manager not available";
        return result;
    }
    
    QString activeWallet = m_settings->activeWallet();
    if (activeWallet.isEmpty()) {
        result["error"] = "No active wallet selected";
        return result;
    }
    
    if (destinationPath.isEmpty()) {
        result["error"] = "Destination path is empty";
        return result;
    }
    
    // Get source wallet.dat path
    QString sourcePath = getWalletDatPath(activeWallet);
    if (sourcePath.isEmpty()) {
        // Construct expected path for error message
        QString datadir = m_settings->datadir();
        QString expectedPath;
        if (datadir.isEmpty()) {
            // Use default datadir path
            QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            if (!homePath.isEmpty()) {
#ifdef Q_OS_WIN
                QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                if (!appDataPath.isEmpty()) {
                    QDir appDataDir(appDataPath);
                    appDataDir.cdUp();
                    datadir = appDataDir.absoluteFilePath("QBitX");
                } else {
                    datadir = homePath + "/AppData/Roaming/QBitX";
                }
#elif defined(Q_OS_MACOS)
                datadir = homePath + "/Library/Application Support/QBitX";
#else
                datadir = homePath + "/.qbitx";
#endif
            }
        }
        if (!datadir.isEmpty()) {
            expectedPath = datadir + "/wallets/" + activeWallet + "/wallet.dat";
        } else {
            expectedPath = QString("<datadir>/wallets/%1/wallet.dat").arg(activeWallet);
        }
        result["error"] = QString("wallet.dat not found at %1").arg(expectedPath);
        return result;
    }
    
    // Validate source file exists
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result["error"] = QString("wallet.dat not found at %1").arg(sourcePath);
        return result;
    }
    
    // Copy wallet.dat to destination
    QFile sourceFile(sourcePath);
    if (!sourceFile.copy(destinationPath)) {
        QString errorMsg = sourceFile.errorString();
        if (errorMsg.isEmpty()) {
            errorMsg = QString("Failed to copy wallet.dat to %1").arg(destinationPath);
        } else {
            errorMsg = QString("Failed to copy wallet.dat: %1").arg(errorMsg);
        }
        result["error"] = errorMsg;
        return result;
    }
    
    // Verify backup file exists and has correct size
    QFileInfo destInfo(destinationPath);
    if (!destInfo.exists() || destInfo.size() != sourceInfo.size()) {
        QFile::remove(destinationPath);
        result["error"] = "Backup file verification failed: size mismatch";
        return result;
    }
    
    result["ok"] = true;
    result["backupPath"] = destinationPath;
    
    emit walletDatBackedUp(destinationPath);
    emit successMessage(QString("Wallet backed up to %1").arg(destinationPath));
    
    return result;
}

QVariantMap WalletManager::autoBackupCurrentWallet()
{
    // This function is kept for backward compatibility but now uses filesystem backup
    // It generates a default filename and calls backupWalletToFile
    QVariantMap result;
    result["ok"] = false;
    result["backupPath"] = QString();
    result["error"] = QString();
    
    if (!m_settings) {
        result["error"] = "Settings manager not available";
        return result;
    }
    
    QString activeWallet = m_settings->activeWallet();
    if (activeWallet.isEmpty()) {
        result["error"] = "No active wallet selected";
        return result;
    }
    
    // Get default backup directory
    QString backupDir = getDefaultBackupDirectory();
    if (backupDir.isEmpty()) {
        result["error"] = "Could not determine backup directory";
        return result;
    }
    
    // Generate default filename: qbitx_wallet_<walletName>_YYYYMMDD.dat
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString backupFileName = QString("qbitx_wallet_%1_%2.dat").arg(activeWallet).arg(timestamp);
    QString backupPath = backupDir + "/" + backupFileName;
    
    // Use filesystem backup
    return backupWalletToFile(backupPath);
}

QVariantMap WalletManager::backupWallet(const QString &walletName)
{
    QVariantMap result;
    result["ok"] = false;
    result["backupPath"] = QString();
    result["error"] = QString();
    
    if (!m_settings) {
        result["error"] = "Settings manager not available";
        return result;
    }
    
    if (walletName.isEmpty()) {
        result["error"] = "Wallet name is empty";
        return result;
    }
    
    QString qbitxCliPath = m_settings->qbitxCliPath();
    if (qbitxCliPath.isEmpty()) {
        result["error"] = "qbitx-cli path not configured";
        return result;
    }
    
    // Use datadir for backup root: <datadir>/backups
    QString datadir = m_settings->datadir();
    if (datadir.isEmpty()) {
        result["error"] = "Data directory not configured";
        return result;
    }
    
    QString backupRoot = datadir + "/backups";
    QDir dir;
    if (!dir.mkpath(backupRoot)) {
        result["error"] = QString("Failed to create backup directory: %1").arg(backupRoot);
        return result;
    }

    // Create deterministic destination:
    // <datadir>/backups/<wallet>-backup-YYYYMMDD-HHMMSS/<wallet>/wallet.dat
    // Ensure uniqueness if called multiple times within the same second by bumping the timestamp by +1s.
    QDateTime dt = QDateTime::currentDateTime();
    QString timestamp = dt.toString("yyyyMMdd-HHmmss");
    QString backupBaseDir = backupRoot + "/" + walletName + "-backup-" + timestamp;
    while (QDir(backupBaseDir).exists()) {
        dt = dt.addSecs(1);
        timestamp = dt.toString("yyyyMMdd-HHmmss");
        backupBaseDir = backupRoot + "/" + walletName + "-backup-" + timestamp;
    }

    QString walletBackupDir = backupBaseDir + "/" + walletName;
    if (!dir.mkpath(walletBackupDir)) {
        result["error"] = QString("Failed to create wallet backup directory: %1").arg(walletBackupDir);
        return result;
    }

    QString walletDatDestPath = walletBackupDir + "/wallet.dat";

    // Execute: qbitx-cli -datadir=<datadir> -rpcwallet=<walletName> backupwallet <full_path>/wallet.dat
    QProcess process;
    QStringList arguments;
    
    // Add datadir
    arguments << "-datadir=" + datadir;
    
    // Add network flag if specified
    QString network = m_settings->network();
    if (!network.isEmpty() && network != "main") {
        arguments << "-" + network;
    }
    
    // Add RPC credentials ONLY if BOTH are set
    QString rpcuser = m_settings->rpcuser();
    QString rpcpassword = m_settings->rpcpassword();
    if (!rpcuser.isEmpty() && !rpcpassword.isEmpty()) {
        arguments << "-rpcuser=" + rpcuser;
        arguments << "-rpcpassword=" + rpcpassword;
    }
    
    // Add -rpcwallet
    arguments << "-rpcwallet=" + walletName;
    
    // Add backupwallet method and destination parameter
    arguments << "backupwallet" << walletDatDestPath;
    
    process.start(qbitxCliPath, arguments);
    if (!process.waitForStarted(5000)) {
        result["error"] = "Failed to start qbitx-cli process";
        return result;
    }
    
    if (!process.waitForFinished(30000)) {
        process.kill();
        result["error"] = "qbitx-cli backupwallet command timed out";
        return result;
    }
    
    int exitCode = process.exitCode();
    QByteArray stdoutBytes = process.readAllStandardOutput();
    QByteArray stderrBytes = process.readAllStandardError();
    QString stderr = QString::fromUtf8(stderrBytes).trimmed();
    
    // Success = process exitCode == 0
    if (exitCode == 0) {
        // Verify backup file exists
        QFileInfo backupInfo(walletDatDestPath);
        if (!backupInfo.exists() || !backupInfo.isFile()) {
            result["error"] = QString("Backup was not created at: %1").arg(walletDatDestPath);
            return result;
        }
        
        result["ok"] = true;
        // Return the backup base directory (folder the user can restore from)
        result["backupPath"] = backupBaseDir;
        
        emit walletDatBackedUp(backupBaseDir);
        emit successMessage(QString("Wallet backed up to:\n%1").arg(backupBaseDir));
        
        return result;
    } else {
        // On failure show stderr / error message only
        QString errorMsg = stderr.isEmpty() ? QString("qbitx-cli backupwallet failed with exit code %1").arg(exitCode) : stderr;
        result["error"] = errorMsg;
        emit errorOccurred(errorMsg);
        return result;
    }
}
