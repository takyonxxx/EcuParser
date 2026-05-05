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

QString AppPaths::binsDir()
{
    const QString base = dataDir();
    if (base.isEmpty())
        return QString();
    const QString sub = base + QStringLiteral("/bin");
    if (QFileInfo(sub).isDir())
        return QDir(sub).absolutePath();
    // Backwards-compat: project layouts that pre-date the bin/ subdir
    // had .bin files at the root of data/. Fall back to that so old
    // checkouts still work.
    return base;
}

QString AppPaths::driversDir()
{
    const QString base = dataDir();
    if (base.isEmpty())
        return QString();
    const QString sub = base + QStringLiteral("/drivers");
    if (QFileInfo(sub).isDir())
        return QDir(sub).absolutePath();
    return base;
}

QStringList AppPaths::listDrivers()
{
    const QString base = dataDir();
    if (base.isEmpty())
        return {};

    // Canonical layout is data/drivers/ holding both .drt (our
    // reverse-engineered format) and .xdf (TunerPro's open format)
    // since they describe the same thing - cell addresses, dims, and
    // axes. Older checkouts kept .drt at the root of data/ and .xdf in
    // data/xdf/, so probe all three locations and merge the results.
    const QStringList patterns {
        QStringLiteral("*.drt"),
        QStringLiteral("*.xdf"),
    };
    QStringList out;

    auto appendFrom = [&](const QString &dirPath) {
        QDir d(dirPath);
        if (!d.exists())
            return;
        const QStringList names = d.entryList(
            patterns, QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &n : names) {
            const QString full = d.absoluteFilePath(n);
            // Avoid duplicates if both legacy and new layouts contain
            // the same filename.
            if (!out.contains(full))
                out.append(full);
        }
    };

    // Preferred layout first so canonical copies sort to the top.
    appendFrom(base + QStringLiteral("/drivers"));
    // Legacy fallbacks.
    appendFrom(base);
    appendFrom(base + QStringLiteral("/xdf"));
    return out;
}

QStringList AppPaths::listBins()
{
    const QString base = dataDir();
    if (base.isEmpty())
        return {};

    QStringList out;
    auto appendFrom = [&](const QString &dirPath) {
        QDir d(dirPath);
        if (!d.exists())
            return;
        const QStringList names = d.entryList(
            QStringList() << QStringLiteral("*.bin"),
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &n : names) {
            const QString full = d.absoluteFilePath(n);
            if (!out.contains(full))
                out.append(full);
        }
    };

    // Canonical layout: data/bin/. Legacy: bins at the root of data/.
    appendFrom(base + QStringLiteral("/bin"));
    appendFrom(base);
    return out;
}

} // namespace EcuParser
