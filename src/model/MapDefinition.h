#ifndef MAPDEFINITION_H
#define MAPDEFINITION_H

#include "AxisDefinition.h"
#include "MapCategory.h"

#include <QString>
#include <QList>
#include <cstdint>

namespace Titanium {

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
    QString  typeCode;            // "PR", "I0", ...
    int      dimX     = 0;        // columns
    int      dimY     = 0;        // rows (1 for L*-style 1D maps)
    int      cellSize = 2;        // bytes per cell (1, 2, 4)
    QList<quint32> addresses;     // one or more bin offsets where this map lives
    AxisDefinition axisX;
    AxisDefinition axisY;
    bool     enabled  = true;

    // Total cells per map instance.
    int cellCount() const { return dimX * dimY; }
    // Bytes consumed by one map instance.
    int byteSize() const  { return cellCount() * cellSize; }

    // Category derived from typeCode prefix.
    MapCategory category() const { return categoryForTypeCode(typeCode); }

    // Best-effort human description for the screenshot tree. We don't have a
    // canonical mapping yet (the .drt file doesn't carry English names), so
    // this returns a sensible fallback. Phase 2 will load names from MapDesc/.
    QString displayName() const;
};

} // namespace Titanium

#endif // MAPDEFINITION_H
