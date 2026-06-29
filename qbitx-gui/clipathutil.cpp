#include "clipathutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

QString discoverQbitxCliExecutable()
{
    QStringList candidates;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString home = QDir::homePath();

    const auto push = [&candidates](const QString &p) {
        const QString c = QDir::cleanPath(p);
        if (!c.isEmpty() && !candidates.contains(c))
            candidates.append(c);
    };

    push(QDir(appDir).filePath(QStringLiteral("qbitx-cli")));
    push(QDir(appDir).filePath(QStringLiteral("qbitx-cli.exe")));
    push(QDir(appDir).filePath(QStringLiteral("bin/qbitx-cli")));
    push(QDir(appDir).filePath(QStringLiteral("bin/qbitx-cli.exe")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/qbitx-cli")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/src/qbitx-cli")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/bin/qbitx-cli")));
    push(home + QStringLiteral("/pesok/qbx/build/qbitx-cli"));
    push(home + QStringLiteral("/pesok/qbx/build/src/qbitx-cli"));
    push(home + QStringLiteral("/pesok/qbx/build/bin/qbitx-cli"));
    push(home + QStringLiteral("/qbitx_restore/build_wallet2/qbitx-cli"));

#if defined(Q_OS_WIN)
    push(QDir(appDir).filePath(QStringLiteral("../../build/qbitx-cli.exe")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/src/qbitx-cli.exe")));
    push(QDir(appDir).filePath(QStringLiteral("../../build/bin/qbitx-cli.exe")));
    push(home + QStringLiteral("/pesok/qbx/build/qbitx-cli.exe"));
    push(home + QStringLiteral("/pesok/qbx/build/src/qbitx-cli.exe"));
    push(home + QStringLiteral("/pesok/qbx/build/bin/qbitx-cli.exe"));
    push(home + QStringLiteral("/qbitx_restore/build_wallet2/qbitx-cli.exe"));
#endif

    for (const QString &candidate : candidates) {
        QFileInfo fi(candidate);
        if (!fi.isFile())
            continue;
#if defined(Q_OS_WIN)
        if (fi.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0)
            continue;
#else
        if (!fi.isExecutable())
            continue;
#endif
        return fi.absoluteFilePath();
    }
    return QString();
}
