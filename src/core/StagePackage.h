#ifndef STAGEPACKAGE_H
#define STAGEPACKAGE_H

// Stage package data structures and apply function.
//
// Why is applyStage() an inline definition in this header rather than a
// out-of-line definition in StagePackage.cpp? Because some build setups
// (Qt Creator with stale Makefiles, IDEs that miss .pro changes) end up
// linking MainWindow.o without compiling StagePackage.cpp - producing
// LNK2019 unresolved symbol errors at link time. Putting applyStage()
// inline-in-header sidesteps that class of problem entirely: any
// translation unit that includes StagePackage.h gets the definition for
// free, and the One Definition Rule is satisfied through `inline`.

#include "BinFile.h"
#include "../model/DriverModel.h"
#include "../model/MapDefinition.h"
#include "../model/DriverNames.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <algorithm>

namespace EcuParser {

// One edit instruction inside a stage package.
//
// The edit applies to every cell whose (row, col) falls inside the
// optional [row_min..row_max] x [col_min..col_max] window. Three modes
// of operation are supported, picked in this order of priority:
//
//   1. setToMapMax = true  -> every cell in the window is set to the
//      MAXIMUM value present in the map's original layout. This is the
//      EGR-off trick on EDC15C: saturating the phase-of-injection map
//      to its own factory ceiling makes the ECU treat the table as a
//      flat reference and disengages EGR closed-loop control without
//      needing a hardware blanking plate.
//   2. setValue >= 0  -> every cell in the window is set to setValue
//      directly (raw u16). Useful for hard-coded ceilings or specific
//      calibration values.
//   3. otherwise  -> pctChange is applied as a multiplicative scaling
//      with optional clamp to maxValue. This is the normal "raise
//      injection by 18%" mode.
struct StageEdit
{
    QString mapName;     // canonical name from DriverNames::displayName
    double  pctChange = 0.0;

    // Window bounds (-1 = no constraint). row/col are 0-based, into
    // the EFFECTIVE dimensions (DriverNames overrides apply).
    int rowMin = -1;
    int rowMax = -1;
    int colMin = -1;
    int colMax = -1;

    // Optional cap on the modified cell value (raw u16). Useful for
    // hardware safety limits (e.g. torque ceiling, rail pressure cap).
    int maxValue = -1;

    // Alternative to pctChange: write a fixed value into every cell.
    // -1 means "ignore, use pctChange instead".
    int setValue = -1;

    // Alternative to pctChange/setValue: fill the window with the
    // map's own original maximum value. EGR off works this way.
    bool setToMapMax = false;

    QString comment;     // free-form comment for human reading
};

// A togglable bundle of edits inside a stage. The user gets a checkbox
// in the Apply Stage dialog for each option; checked options have their
// edits appended to the main edit list at apply time. Use this for
// switches like "EGR off" that the user might or might not want to take
// alongside the core tune.
struct StageOption
{
    QString          id;            // stable identifier (used for "remember last choice")
    QString          label;         // short text for the checkbox
    QString          description;   // long explanation shown alongside
    bool             defaultOn = true;
    QList<StageEdit> edits;         // edits applied when this option is on
};

// A stage package: a list of edits plus metadata. Schemas the package
// is valid for.
struct StagePackage
{
    QString            name;          // user-visible display name
    QString            description;
    QStringList        schemas;       // schemaId list this stage applies to
    QList<StageEdit>   edits;         // core edits, always applied
    QList<StageOption> options;       // optional togglable bundles

    // Load a stage from a JSON file on disk. Returns false and fills
    // *errorOut on failure (file missing, JSON syntax, missing fields).
    static bool loadFromJson(const QString &path,
                             StagePackage *out,
                             QString *errorOut = nullptr);

    // Convenience: list every "*.json" file under data/stages/ as
    // (display-name, file-path) pairs sorted by display-name.
    static QList<QPair<QString, QString>> listAvailable();
};

// Apply a stage package to one bin. Reads cells from `source` (typically
// the Original) and writes the percentage-shifted values into `target`
// (typically the Modified). Returns the number of cells written. Skipped
// edits (map not in driver, schema mismatch) accumulate into *warnings.
inline int applyStage(const StagePackage &stage,
                      const DriverModel  &driver,
                      const BinFile      &source,
                      BinFile            *target,
                      QStringList        *warnings = nullptr)
{
    if (!target) return 0;

    auto warn = [&](const QString &m) {
        if (warnings) warnings->append(m);
    };

    // Schema gate: if the package lists schemas, the driver's schemaId
    // must be in the list. An empty list means "any schema".
    if (!stage.schemas.isEmpty()
        && !stage.schemas.contains(driver.schemaId)) {
        warn(QStringLiteral("Schema mismatch: package supports %1, driver is %2")
                 .arg(stage.schemas.join(QStringLiteral(", ")),
                      driver.schemaId));
        return 0;
    }

    int totalCellsWritten = 0;

    for (const StageEdit &edit : stage.edits) {
        // Find the map by canonical (DriverNames) display name. The
        // .drt's raw `name` field isn't always meaningful, so we match
        // against the canonical name we expose in the UI.
        const MapDefinition *target_map = nullptr;
        for (const MapDefinition &m : driver.maps) {
            if (DriverNames::displayName(driver.schemaId, m) == edit.mapName) {
                target_map = &m;
                break;
            }
        }
        if (!target_map) {
            warn(QStringLiteral("Map not found in driver: '%1'").arg(edit.mapName));
            continue;
        }
        if (target_map->addresses.isEmpty()) {
            warn(QStringLiteral("Map has no addresses: '%1'").arg(edit.mapName));
            continue;
        }

        const int effDX = DriverNames::effectiveDimX(driver.schemaId, *target_map);
        const int effDY = DriverNames::effectiveDimY(driver.schemaId, *target_map);
        if (effDX <= 0 || effDY <= 0)
            continue;

        // Honour maxInstances cap so we don't accidentally edit a
        // shadow instance that the user doesn't even see in the tree.
        int instances = target_map->addresses.size();
        const int cap = DriverNames::maxInstances(driver.schemaId, *target_map);
        if (cap > 0)
            instances = std::min(instances, cap);

        // Resolve the [rowMin..rowMax] x [colMin..colMax] window. -1
        // means "unbounded", which we expand to 0..effDim-1.
        const int rMin = (edit.rowMin < 0) ? 0          : edit.rowMin;
        const int rMax = (edit.rowMax < 0) ? effDX - 1  : edit.rowMax;
        const int cMin = (edit.colMin < 0) ? 0          : edit.colMin;
        const int cMax = (edit.colMax < 0) ? effDY - 1  : edit.colMax;

        const double scale = 1.0 + edit.pctChange / 100.0;

        // Bin stride: cells are stored row-major, so the byte offset of
        // (r, c) is base + (r * stride + c) * cellSize. The stride must
        // be the row width as bytes are LAID OUT in the bin, not as
        // displayed. DriverNames' effDY is the source of truth here -
        // it's specifically meant to override the .drt's unreliable
        // dim fields with the real layout (e.g. torque limiter that
        // the .drt mis-parses as 0). Using effDY for both the iteration
        // bounds AND the stride keeps everything self-consistent.
        const int strideY = effDY;

        // If the edit asks for "set every cell to the map's own max",
        // we need to scan the map first to find that max. We use the
        // SOURCE bin (Original) as the reference, not the in-progress
        // target, so the value is stable regardless of what other
        // edits have already done.
        int mapMaxValue = 0;
        if (edit.setToMapMax) {
            for (int r = 0; r < effDX; ++r) {
                for (int c = 0; c < effDY; ++c) {
                    const int idx = r * strideY + c;
                    const quint32 off = target_map->addresses.first()
                                      + quint32(idx * target_map->cellSize);
                    bool ok = false;
                    const quint16 v = source.readU16LE(off, &ok);
                    if (ok && int(v) > mapMaxValue)
                        mapMaxValue = int(v);
                }
            }
        }

        for (int inst = 0; inst < instances; ++inst) {
            const quint32 base = target_map->addresses.at(inst);
            for (int r = std::max(0, rMin); r <= std::min(effDX - 1, rMax); ++r) {
                for (int c = std::max(0, cMin); c <= std::min(effDY - 1, cMax); ++c) {
                    const int idx = r * strideY + c;
                    const quint32 off = base + quint32(idx * target_map->cellSize);

                    // Compute the new cell value according to the edit
                    // mode (max-fill > absolute set > percentage scale).
                    int newV = 0;
                    if (edit.setToMapMax) {
                        newV = mapMaxValue;
                    } else if (edit.setValue >= 0) {
                        newV = edit.setValue;
                    } else {
                        bool ok = false;
                        const quint16 origV = source.readU16LE(off, &ok);
                        if (!ok) continue;
                        newV = int(double(origV) * scale + 0.5);
                    }
                    if (edit.maxValue > 0 && newV > edit.maxValue)
                        newV = edit.maxValue;
                    if (newV < 0) newV = 0;
                    if (newV > 65535) newV = 65535;
                    if (target->writeU16LE(off, quint16(newV)))
                        ++totalCellsWritten;
                }
            }
        }
    }

    return totalCellsWritten;
}

} // namespace EcuParser

#endif // STAGEPACKAGE_H
