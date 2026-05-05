#ifndef DRIVERNAMES_H
#define DRIVERNAMES_H

#include "MapDefinition.h"
#include "MapCategory.h"

#include <QString>

namespace EcuParser {

// Per-driver overrides for the reference tool-style display.
//
// The .drt files we ship ship report the right addresses but their
// dim hints don't always match what the reference tool actually displays - e.g.
// J293_822's "rail pressure" map at 0x07ADD2 has dimX=16, dimY=20 in the
// .drt file but the reference tool only renders 16x16 of that and the trailing 4 cells of
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
    // synthesised. the reference tool sometimes embeds the axis values in the
    // driver itself (instead of pointing at a bin location); when that's
    // the case we list them here so our display matches the reference tool exactly.
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

    // Maximum instances the reference tool displays for this map. 0 = no limit
    // (default; show all addresses listed in MapDefinition::addresses).
    // 1 = clamp to first address only (e.g. (Boost x RPM) maps which the
    // .drt over-lists). Returns 0 for unknown drivers/maps so they keep
    // default behaviour.
    //
    // When "expert mode" is enabled via setExpertMode(true), this method
    // returns 0 unconditionally - so the user sees every instance the
    // .drt records, including shadow addresses the reference tool hides.
    static int maxInstances(const QString &schemaId,
                            const MapDefinition &map);

    // Toggle whether maxInstances() honours the per-map cap. Off by
    // default (matches the reference tool's display). Persists for the
    // lifetime of the process.
    static void setExpertMode(bool on);
    static bool expertMode();

    // Optional linear unit conversion for display: physical = raw*scale + offset.
    // Returns scale=1, offset=0, unit="" when no override is set so callers
    // can apply unconditionally. Editing always operates on raw u16; this is
    // a display-time conversion only. Verified physical mappings for the
    // J293_822 schema (Jeep WJ 2.7 CRD EDC15C, Mercedes OM612 engine):
    //   rail pressure      : raw / 10            -> bar      (0x07ADD2 max ~1350 bar)
    //   turbo pressure     : raw                 -> mbar     (factory range 1000..2250)
    //   torque limiter     : raw * 400 / 7500    -> Nm proxy (peak 7500 -> 400 Nm OEM)
    //   phase of injection : raw / 100           -> degCA    (typical EDC15 scaling)
    static double scaleFor (const QString &schemaId, const MapDefinition &map);
    static double offsetFor(const QString &schemaId, const MapDefinition &map);
    static QString unitFor (const QString &schemaId, const MapDefinition &map);

    // Mutates map's scale/offset/unit fields in-place from the override
    // table. Used right after a parser fills addresses+typecode so the rest
    // of the app sees physical-unit-aware MapDefinition objects.
    static void applyUnitOverride(const QString &schemaId, MapDefinition *map);
};

} // namespace EcuParser

#endif // DRIVERNAMES_H
