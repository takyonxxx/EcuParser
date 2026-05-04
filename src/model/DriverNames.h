#ifndef DRIVERNAMES_H
#define DRIVERNAMES_H

#include "MapDefinition.h"
#include "MapCategory.h"

#include <QString>

namespace Titanium {

// Per-driver overrides for ECM Titanium-style display.
//
// The .drt files we ship ship report the right addresses but their
// dim hints don't always match what ECM Titanium actually displays - e.g.
// J293_822's "rail pressure" map at 0x07ADD2 has dimX=16, dimY=20 in the
// .drt file but ECM only renders 16x16 of that and the trailing 4 cells of
// each row are noise / padding from a neighbouring table. We therefore
// allow each entry to optionally override dimX/dimY for display.
//
// When a row has dimXOverride > 0 it replaces map.dimX for table layout
// and cell-index calculation; same for dimYOverride.
class DriverNames
{
public:
    static QString canonicalName(const QString &schemaId,
                                 const MapDefinition &map);

    static QString displayName(const QString &schemaId,
                               const MapDefinition &map);

    static int sortKey(const QString &schemaId,
                       const MapDefinition &map);

    static int effectiveDimX(const QString &schemaId, const MapDefinition &map);
    static int effectiveDimY(const QString &schemaId, const MapDefinition &map);

    // Hard-coded axis breakpoints overriding whatever is in the bin or
    // synthesised. ECM Titanium sometimes embeds the axis values in the
    // driver itself (instead of pointing at a bin location); when that's
    // the case we list them here so our display matches ECM exactly.
    // Returns an empty list when no override is set; caller falls back to
    // bin-read or synthesised axis.
    static QList<int> axisXOverride(const QString &schemaId, const MapDefinition &map);
    static QList<int> axisYOverride(const QString &schemaId, const MapDefinition &map);

    // Effective category: DRT type-code dispatch is unreliable since the
    // .drt may bucket maps oddly (e.g. J293_822 routes turbo pressure
    // under L*, torque limiter under "?"). When an override is set we use
    // it; otherwise we fall back to MapCategory::categoryForTypeCode.
    static MapCategory effectiveCategory(const QString &schemaId,
                                         const MapDefinition &map);

    // Maximum instances ECM Titanium displays for this map. 0 = no limit
    // (default; show all addresses listed in MapDefinition::addresses).
    // 1 = clamp to first address only (e.g. (Boost x RPM) maps which the
    // .drt over-lists). Returns 0 for unknown drivers/maps so they keep
    // default behaviour.
    static int maxInstances(const QString &schemaId,
                            const MapDefinition &map);
};

} // namespace Titanium

#endif // DRIVERNAMES_H
