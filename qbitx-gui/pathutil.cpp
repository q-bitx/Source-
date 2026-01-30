#include "pathutil.h"
#include <QDir>
#include <QRegularExpression>
#include <QUrl>
#include <QFileInfo>

QString PathUtil::resolveUserPath(const QString &input)
{
    QString trimmed = input.trimmed();
    
    // If input is empty, return empty
    if (trimmed.isEmpty()) {
        return QString();
    }
    
    // A) If running on Linux and input matches Windows drive path
    if (isLinux()) {
        QRegularExpression winDriveRegex("^([A-Za-z]):[\\\\/](.*)$");
        QRegularExpressionMatch match = winDriveRegex.match(trimmed);
        
        if (match.hasMatch()) {
            QString drive = match.captured(1).toLower();
            QString rest = match.captured(2);
            
            // Replace backslashes with forward slashes
            rest.replace('\\', '/');
            
            // Build WSL path: /mnt/<drive>/<rest>
            // Note: rest already starts with the path after the drive letter and separator
            QString wslPath = "/mnt/" + drive + "/" + rest;
            
            // Normalize duplicate slashes
            wslPath = normalizeSlashes(wslPath);
            
            // Defensive fix: if path starts with "/mnt/<drive>Users/" (missing slash),
            // insert the missing slash: "/mnt/<drive>/Users/..."
            QRegularExpression missingSlashRegex("^/mnt/([a-z])Users/");
            QRegularExpressionMatch slashMatch = missingSlashRegex.match(wslPath);
            if (slashMatch.hasMatch()) {
                QString driveLetter = slashMatch.captured(1);
                wslPath = "/mnt/" + driveLetter + "/Users/" + wslPath.mid(5 + driveLetter.length() + 6); // Skip "/mnt/" + drive + "Users/"
            }
            
            return QDir::cleanPath(wslPath);
        }
    }
    
    // B) If input starts with "/mnt/" (already WSL) -> normalize
    if (trimmed.startsWith("/mnt/")) {
        QString normalized = normalizeSlashes(trimmed);
        
        // Defensive fix: check for missing slash after drive letter
        QRegularExpression missingSlashRegex("^/mnt/([a-z])Users/");
        QRegularExpressionMatch slashMatch = missingSlashRegex.match(normalized);
        if (slashMatch.hasMatch()) {
            QString driveLetter = slashMatch.captured(1);
            normalized = "/mnt/" + driveLetter + "/Users/" + normalized.mid(5 + driveLetter.length() + 6);
        }
        
        return QDir::cleanPath(normalized);
    }
    
    // C) If input starts with "file:///" -> decode to local path
    if (trimmed.startsWith("file://")) {
        QUrl url(trimmed);
        QString localPath = url.toLocalFile();
        if (!localPath.isEmpty()) {
            return QDir::cleanPath(localPath);
        }
    }
    
    // D) Otherwise return QDir::cleanPath(input) (works for native Linux/Mac/Windows paths)
    return QDir::cleanPath(trimmed);
}

QString PathUtil::normalizeSlashes(const QString &path)
{
    QString normalized = path;
    // Replace multiple consecutive slashes with single slash
    normalized.replace(QRegularExpression("/+"), "/");
    // Handle Windows paths: replace backslashes with forward slashes
    normalized.replace('\\', '/');
    return normalized;
}

bool PathUtil::isLinux()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}
