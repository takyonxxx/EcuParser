#ifndef DRIVERMODEL_H
#define DRIVERMODEL_H

#include "MapDefinition.h"

#include <QString>
#include <QList>

namespace Titanium {

// Parsed content of a .drt driver file.
//
// File layout (records separated by 0x84, fields by 0xBB):
//   Record 0..1: empty (file starts with two 0x84 bytes)
//   Record 2:   header   - schemaId (e.g. "28F0_100"), mapCount
//   Record 3:   ECU info - ECU type code (e.g. "IP"), default dimX, default dimY, ?, ?
//   Record 4..N: one record per map definition (see MapDefinition.h)
//
// schemaId format observed:
//   "28F0_100" - EDC15C family, schema version 100
//   "STD0_100" - older simpler driver
//   We don't yet know the full enumeration; treat schemaId as opaque metadata.
struct DriverModel {
    QString  schemaId;            // e.g. "28F0_100"
    int      mapCount = 0;        // declared count from header (record 2)
    QString  ecuTypeCode;         // e.g. "IP" (from record 3)
    int      defaultDimX = 0;
    int      defaultDimY = 0;
    QList<MapDefinition> maps;

    // Source file path, useful for error messages.
    QString  sourcePath;

    bool isValid() const { return !schemaId.isEmpty() && !maps.isEmpty(); }

    // Group maps by category for tree display (matches ECM Titanium UI).
    // Returns categories in a stable order: INJECTION, TURBO, LIMITERS, TIMING, OTHER.
    QList<QPair<MapCategory, QList<const MapDefinition*>>> mapsByCategory() const;
};

} // namespace Titanium

#endif // DRIVERMODEL_H
