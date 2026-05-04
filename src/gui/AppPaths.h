#ifndef APPPATHS_H
#define APPPATHS_H

#include <QString>
#include <QStringList>

namespace Titanium {

// Locates the bundled data/ directory (containing .drt drivers and bin files).
// During development the build sits in <project>/build/ next to <project>/data/,
// so we check several candidate locations relative to the executable.
class AppPaths
{
public:
    // Returns the absolute path to the data/ directory, or empty string if
    // not found. Cached after first call.
    static QString dataDir();

    // Returns absolute paths to all .drt files in data/. Empty list if none.
    static QStringList listDrivers();

    // Returns absolute paths to all .bin files in data/. Empty list if none.
    static QStringList listBins();
};

} // namespace Titanium

#endif // APPPATHS_H
