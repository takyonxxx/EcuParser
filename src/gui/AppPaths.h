#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>
#include <QStringList>

namespace EcuParser {

// Locates the bundled data/ directory (containing .drt drivers and bin files).
// During development the build sits in <project>/build/ next to <project>/data/,
// so we check several candidate locations relative to the executable.
class AppPaths
{
public:
    // Returns the absolute path to the data/ directory, or empty string if
    // not found. Cached after first call.
    static QString dataDir();

    // Returns the absolute path to the data/bin/ directory if it exists,
    // otherwise falls back to dataDir(). Used as the suggested directory
    // for bin file open dialogs.
    static QString binsDir();

    // Returns the absolute path to the data/drivers/ directory if it
    // exists, otherwise falls back to dataDir(). Used as the suggested
    // directory for driver file open dialogs.
    static QString driversDir();

    // Returns absolute paths to all .drt and .xdf files under
    // data/drivers/ (the canonical layout). For backwards compatibility
    // with old installs we also pick up .drt/.xdf left at the root of
    // data/ and inside data/xdf/.
    static QStringList listDrivers();

    // Returns absolute paths to all .bin files under data/bin/ (the
    // canonical layout). For backwards compatibility with old installs
    // we also pick up .bin files left at the root of data/.
    static QStringList listBins();
};

} // namespace EcuParser

#endif // APPPATHS_H
