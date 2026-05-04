#ifndef MAPDATA_H
#define MAPDATA_H

#include "../model/MapDefinition.h"

#include <QVector>
#include <cstdint>

namespace EcuParser {

class BinFile;

// Pulls a 2D grid of cells out of a BinFile using a MapDefinition. One map
// definition can have multiple addresses (reference replicates a single map
// at several offsets); MapData represents one instance at one address.
struct MapData {
    int rows  = 0;        // == def.dimY
    int cols  = 0;        // == def.dimX
    QVector<qint32> cells; // row-major, one entry per cell, widened to 32-bit signed

    // Convenience accessors.
    qint32 at(int r, int c) const { return cells.at(r * cols + c); }
    void   set(int r, int c, qint32 v) { cells[r * cols + c] = v; }

    // Statistics for quick inspection.
    qint32 minValue() const;
    qint32 maxValue() const;
    double meanValue() const;
};

// Read one map instance. dimXOverride / dimYOverride > 0 replace the
// dimensions reported by the MapDefinition; this is needed when the .drt
// file's dim hints don't reflect the actual bin layout (verified for
// J293_822 0x07ADD2 which DRT says is 16x20 but is really 16x16). The
// override values are used both for iteration and for cells.rows/cols.
MapData readMapInstance(const BinFile &bin,
                        const MapDefinition &def,
                        int instanceIndex = 0,
                        int dimXOverride = 0,
                        int dimYOverride = 0);

} // namespace EcuParser

#endif // MAPDATA_H
