#ifndef MAPCATEGORY_H
#define MAPCATEGORY_H

#include <QString>

namespace EcuParser {

// Categories displayed as top-level groups in the map tree (matches the reference tool UI).
enum class MapCategory {
    Injection,   // I*, P*, PR (rail pressure also lives under INJECTION in the reference tool)
    Turbo,       // B*, BS (boost / turbo pressure)
    Limiters,    // L* (1D torque/RPM/speed limiters)
    Timing,      // T* (phase / advance) - kept separate even though reference nests under INJECTION
    Other        // Unknown / unclassified
};

// Lookup the category for a reference type code (e.g. "PR", "I0", "BS", "L0").
// Implementation uses the first character of the code; PR is special-cased.
MapCategory categoryForTypeCode(const QString &typeCode);

// Human-readable category name (matches the reference tool tree headers).
QString categoryDisplayName(MapCategory cat);

} // namespace EcuParser

#endif // MAPCATEGORY_H
