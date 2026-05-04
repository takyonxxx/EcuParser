#include "MapData.h"
#include "BinFile.h"

#include <QDebug>
#include <algorithm>
#include <numeric>

namespace EcuParser {

qint32 MapData::minValue() const
{
    if (cells.isEmpty()) return 0;
    return *std::min_element(cells.constBegin(), cells.constEnd());
}

qint32 MapData::maxValue() const
{
    if (cells.isEmpty()) return 0;
    return *std::max_element(cells.constBegin(), cells.constEnd());
}

double MapData::meanValue() const
{
    if (cells.isEmpty()) return 0.0;
    const qint64 sum = std::accumulate(cells.constBegin(), cells.constEnd(), qint64(0));
    return double(sum) / double(cells.size());
}

MapData readMapInstance(const BinFile &bin,
                        const MapDefinition &def,
                        int instanceIndex,
                        int dimXOverride,
                        int dimYOverride)
{
    MapData data;
    const int dx = (dimXOverride > 0) ? dimXOverride : def.dimX;
    const int dy = (dimYOverride > 0) ? dimYOverride : def.dimY;

    // dx and dy are the effective dimensions. axisX is the OUTER index
    // (rows, e.g. RPM), axisY is the INNER index (columns, e.g. Load).
    data.rows = dx;
    data.cols = dy;

    if (dx <= 0 || dy <= 0 || instanceIndex < 0
        || instanceIndex >= def.addresses.size()) {
        qWarning("MapData: invalid def or instance index");
        return data;
    }

    const quint32 base = def.addresses.at(instanceIndex);
    data.cells.reserve(dx * dy);

    for (int r = 0; r < dx; ++r) {
        for (int c = 0; c < dy; ++c) {
            const quint32 off = base + quint32((r * dy + c) * def.cellSize);
            qint32 v = 0;
            switch (def.cellSize) {
            case 1:
                v = bin.readU8(off);
                break;
            case 2:
                // EDC15C cells are little-endian. Verified against
                // J293_822 + 293-822.bin against the reference tool.
                v = bin.readU16LE(off);
                break;
            case 4:
                v = qint32(bin.readU32BE(off));
                break;
            default:
                v = 0;
                break;
            }
            data.cells.append(v);
        }
    }
    return data;
}

} // namespace EcuParser
