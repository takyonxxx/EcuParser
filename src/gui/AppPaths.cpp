#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace EcuParser {

QString AppPaths::dataDir()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    const QString exeDir = QCoreApplication::applicationDirPath();
    // Candidate paths cover the common layouts the user might end up
    // in. The pro file no longer forces a "build/" subdirectory so the
    // exe sits directly in OUT_PWD, but Qt Creator typically points
    // OUT_PWD at a per-configuration sub-folder (e.g.
    // build-EcuParser-.../release/) so the data dir is still up the
    // tree somewhere. We try a handful of common relative paths first
    // for cheap hits, then fall back to a sibling search.
    //
    // Layouts handled:
    //   exe sits next to data:           <X>/EcuParser              -> data
    //   build.sh out-of-source:          <X>/EcuParser-build/EcuParser
    //                                                         -> ../EcuParser/data
    //   Qt Creator with shadow build:    <X>/build-EcuParser-.../release/EcuParser
    //                                                         -> ../../EcuParser/data
    //   Qt Creator nested deeper:        <X>/build-.../release/release/EcuParser
    //                                                         -> ../../../EcuParser/data
    const QStringList candidates {
        exeDir + QStringLiteral("/data"),
        exeDir + QStringLiteral("/../data"),
        exeDir + QStringLiteral("/../../data"),
        exeDir + QStringLiteral("/../../../data"),
        exeDir + QStringLiteral("/../EcuParser/data"),
        exeDir + QStringLiteral("/../../EcuParser/data"),
        exeDir + QStringLiteral("/../../../EcuParser/data"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo(c).isDir()) {
            cached = QDir(c).absolutePath();
            return cached;
        }
    }

    // Last-resort sibling search: walk up to 5 levels above the exe
    // and look for any directory whose name contains "EcuParser" (case
    // sensitive on Linux, insensitive on Windows) that has a data/
    // sub-folder. This rescues unusual layouts the explicit candidate
    // list above doesn't cover.
    QDir up(exeDir);
    for (int i = 0; i < 6; ++i) {
        const QStringList siblings = up.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &s : siblings) {
            // Skip the parent's own build directory to avoid recursion
            // into Qt Creator's "build-EcuParser-..." folders.
            if (s.startsWith(QStringLiteral("build-"),
                             Qt::CaseInsensitive))
                continue;
            if (!s.contains(QStringLiteral("EcuParser"),
                            Qt::CaseInsensitive))
                continue;
            const QString candidate =
                up.absoluteFilePath(s) + QStringLiteral("/data");
            if (QFileInfo(candidate).isDir()) {
                cached = QDir(candidate).absolutePath();
                return cached;
            }
        }
        if (!up.cdUp())
            break;
    }
    return QString();
}

QStringList AppPaths::listDrivers()
{
    const QString dir = dataDir();
    if (dir.isEmpty())
        return {};
    QDir d(dir);
    // Both DRT (our reverse-engineered format) and XDF (TunerPro's
    // open format) describe the same thing - cell addresses, dims,
    // and axes - so list them together. Recurse into a "xdf/" subdir
    // when present so users can keep many community-contributed XDFs
    // alongside the small handful of DRTs.
    const QStringList patterns {
        QStringLiteral("*.drt"),
        QStringLiteral("*.xdf"),
    };
    QStringList out;
    QStringList names = d.entryList(patterns,
                                    QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &n : names)
        out.append(d.absoluteFilePath(n));

    QDir xdfDir(dir + QStringLiteral("/xdf"));
    if (xdfDir.exists()) {
        QStringList xdfNames = xdfDir.entryList(
            QStringList() << QStringLiteral("*.xdf"),
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &n : xdfNames)
            out.append(xdfDir.absoluteFilePath(n));
    }
    return out;
}

QStringList AppPaths::listBins()
{
    const QString dir = dataDir();
    if (dir.isEmpty())
        return {};
    QDir d(dir);
    QStringList names = d.entryList(QStringList() << QStringLiteral("*.bin"),
                                    QDir::Files | QDir::Readable, QDir::Name);
    QStringList out;
    out.reserve(names.size());
    for (const QString &n : names)
        out.append(d.absoluteFilePath(n));
    return out;
}

} // namespace EcuParser
