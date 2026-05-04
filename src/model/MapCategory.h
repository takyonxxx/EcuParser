#ifndef MAPCATEGORY_H
#define MAPCATEGORY_H

#include <QString>

namespace Titanium {

// Categories displayed as top-level groups in the map tree (matches ECM Titanium UI).
enum class MapCategory {
    Injection,   // I*, P*, PR (rail pressure also lives under INJECTION in ECM Titanium)
    Turbo,       // B*, BS (boost / turbo pressure)
    Limiters,    // L* (1D torque/RPM/speed limiters)
    Timing,      // T* (phase / advance) - kept separate even though Titanium nests under INJECTION
    Other        // Unknown / unclassified
};

// Lookup the category for a Titanium type code (e.g. "PR", "I0", "BS", "L0").
// Implementation uses the first character of the code; PR is special-cased.
MapCategory categoryForTypeCode(const QString &typeCode);

// Human-readable category name (matches ECM Titanium tree headers).
QString categoryDisplayName(MapCategory cat);

} // namespace Titanium

#endif // MAPCATEGORY_H
