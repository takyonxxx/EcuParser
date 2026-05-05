#ifndef MAPDEFINITION_H
#define MAPDEFINITION_H

#include "AxisDefinition.h"
#include "MapCategory.h"

#include <QString>
#include <QList>
#include <cstdint>

namespace EcuParser {

// One map descriptor as stored in a .drt record.
//
// Raw record layout (13 fields, separated by 0xBB):
//   [0]  enabled flag (always '1' in samples)
//   [1]  X axis 4-tuple
//   [2]  Y axis 4-tuple
//   [3]  number of data addresses that follow in field [4]
//   [4]  comma-separated data addresses (one map can be replicated at N offsets)
//   [5]  ?  (sign / format flag - need more samples)
//   [6]  cell size in bytes (1, 2 or 4)
//   [7]  ?
//   [8]  type code (e.g. "PR", "I0", "BS", "L0")
//   [9]  dimX (columns)
//   [10] dimY (rows)
//   [11] ?
//   [12] ?
struct MapDefinition {
    QString  name;                // human-readable title (XDF), or empty (DRT)
    QString  typeCode;            // "PR", "I0", ...
    int      dimX     = 0;        // columns
    int      dimY     = 0;        // rows (1 for L*-style 1D maps)
    int      cellSize = 2;        // bytes per cell (1, 2, 4)
    QList<quint32> addresses;     // one or more bin offsets where this map lives
    AxisDefinition axisX;
    AxisDefinition axisY;
    bool     enabled  = true;

    // Optional linear unit conversion: physical = raw * scale + offset.
    // Defaults to scale=1, offset=0 (i.e. raw value pass-through). Set by
    // DriverNames overrides for known maps (rail pressure -> bar, torque
    // limiter -> Nm proxy, turbo pressure -> mbar). When XDF MATH parsing
    // is added, simple "X * a + b" expressions will populate these too.
    // Editing always operates on the raw u16 value; the table merely
    // displays a converted column to the right of the raw value when
    // unit is non-empty. This keeps stage/checksum semantics unchanged.
    double  scale  = 1.0;
    double  offset = 0.0;
    QString unit;                 // "bar", "Nm", "mbar", "deg", etc.

    // Embedded axis breakpoints. Populated by parsers or by
    // DriverNames override injection (see MainWindow::loadDriver).
    // When non-empty these win over reading axisX/axisY from the bin -
    // useful for XDF files where the axis values are documented
    // out-of-band (e.g. in the driver schema's hard-coded RPM table)
    // rather than embedded in the bin at a known address.
    // Size is expected to match dimX (xValues) and dimY (yValues) but
    // consumers should range-check.
    QList<int> xValues;           // RPM breakpoints (one per row)
    QList<int> yValues;           // Load breakpoints (one per column)

    // Optional category hint set by parsers that already know the
    // category (e.g. XdfParser uses XDF's <CATEGORY> tag, or derives it
    // from the title). When unset (Other), category() falls back to the
    // type-code prefix logic. DriverNames overrides win over both.
    MapCategory categoryHint = MapCategory::Other;

    // Total cells per map instance.
    int cellCount() const { return dimX * dimY; }
    // Bytes consumed by one map instance.
    int byteSize() const  { return cellCount() * cellSize; }

    // Category derived from typeCode prefix, OR from categoryHint if
    // the parser supplied one.
    MapCategory category() const {
        if (categoryHint != MapCategory::Other) return categoryHint;
        return categoryForTypeCode(typeCode);
    }

    // Best-effort human description for the screenshot tree. We don't have a
    // canonical mapping yet (the .drt file doesn't carry English names), so
    // this returns a sensible fallback. Phase 2 will load names from MapDesc/.
    QString displayName() const;
};

} // namespace EcuParser

#endif // MAPDEFINITION_H
