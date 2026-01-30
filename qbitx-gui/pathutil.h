#ifndef PATHUTIL_H
#define PATHUTIL_H

#include <QString>

class PathUtil
{
public:
    // Resolve user-entered path to a normalized, platform-appropriate path
    // Handles Windows paths on WSL, file:// URLs, and normalizes paths
    static QString resolveUserPath(const QString &input);
    
private:
    // Helper to normalize duplicate slashes (// -> /)
    static QString normalizeSlashes(const QString &path);
    
    // Helper to check if running on Linux (for WSL detection)
    static bool isLinux();
};

#endif // PATHUTIL_H
