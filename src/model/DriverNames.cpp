#include "DriverNames.h"

#include <QHash>

namespace EcuParser {

namespace {

// Composite key uses just (schemaId, primary address). dimX/dimY in the
// .drt are not always trustworthy (see e.g. 0x07ADD2 which DRT says is
// 16x20 but the reference tool displays as 16x16) so we don't include them in
// the key.
struct MapKey {
    QString schema;
    quint32 addr;

    bool operator==(const MapKey &o) const {
        return addr == o.addr && schema == o.schema;
    }
};

size_t qHash(const MapKey &k, size_t seed = 0) noexcept
{
    return ::qHash(k.schema, seed) ^ ::qHash(k.addr);
}

struct NameEntry {
    QString name;
    int     order;        // the reference tool tree order
    int     dimXOverride; // 0 = use map.dimX
    int     dimYOverride; // 0 = use map.dimY
    QList<int> axisXOverride;  // empty = no override
    QList<int> axisYOverride;  // empty = no override
    MapCategory category {MapCategory::Other};
    // Maximum instances to expose. 0 = no limit (show all addresses
    // listed in MapDefinition::addresses). >=1 = clamp to that many.
    // the reference tool sometimes only displays the first address even when
    // the .drt lists multiple, e.g. J293_822's "(Boost x RPM)" maps -
    // the .drt has 2 addresses each but the reference tool only shows the first.
    int     maxInstances {0};
    // Linear unit conversion for display: physical = raw*scale + offset.
    // Default scale=1, offset=0, unit="" so unrecognised maps stay raw.
    double  scale  {1.0};
    double  offset {0.0};
    QString unit;
};

// Build the override table once. Address + name + dimensions catalogued
// directly from the reference tool 1.61 (J293_822 driver) by the user. The list
// below is the authoritative source; the .drt file's own dim hints are
// often wrong (e.g. 0x71F52 says 12x10 but the reference tool renders 12x16, and 0x7753E
// says 10x10 but the reference tool renders 12x10).
//
// dimXOverride / dimYOverride are populated even when they happen to match
// the .drt: this future-proofs against accidental DRT churn and documents
// the verified-correct shape per map.
const QHash<MapKey, NameEntry> &table()
{
    static const QHash<MapKey, NameEntry> t = []() {
        QHash<MapKey, NameEntry> m;
        const QString s28 = QStringLiteral("28F0_100");

        // Six 16-row injection maps share the same hand-picked RPM axis
        // in the reference tool - it's embedded in the driver, not in the bin.
        // We hard-code the values from the reference tool Image 1 for all six:
        //   0x076F52 (injection at part throttle)
        //   0x07ADD2 (rail pressure)
        //   0x072CF0 (Map 1)
        //   0x072FC0 (Map 2)
        //   0x078C5A (Map 1 Boost x RPM)
        //   0x0791FA (Map 2 Boost x RPM)
        // Without the override, XDFs that don't carry an explicit X-axis
        // address fall back to showing raw row indices (0..15) in the
        // RPM column header, which confuses users reading the table.
        const QList<int> sharedInjectionRpm {
            700, 800, 900, 1000, 1100, 1300, 1500, 1700,
            1900, 2100, 2400, 2700, 3100, 3500, 4000, 4500
        };

        //  #   Address    Size    Name
        //  1   0x76F52   16x16   injection at part throttle
        //  2   0x7ADD2   16x16   rail pressure                   (RPM axis embedded in driver)
        //  3   0x72CF0   16x20   injection at part throttle (Map 1)
        //  4   0x72FC0   16x20   injection at part throttle (Map 2)
        //  5   0x78C5A   16x20   "" (Map 1) (Boost x RPM)        (2 instances at 0x78C5A,0x78F2A)
        //  6   0x791FA   16x20   "" (Map 2) (Boost x RPM)        (2 instances at 0x791FA,0x794CA)
        //  7   0x71F52   12x16   phase of injection
        //  8   0x7753E   12x10   fuel during acceleration
        //  9   0x7765E   10x10   fuel during acceleration (Map 2)
        // 10   0x75EA0   12x12   turbo pressure
        // 11   0x76D82   19x1    torque limiter

        m.insert({s28, 0x076F52},
                 {QStringLiteral("injection at part throttle"), 1, 16, 16,
                  sharedInjectionRpm, {}, MapCategory::Injection, 0,
                  // No clean physical unit for raw injector pulse counts; leave raw.
                  1.0, 0.0, QString()});
        m.insert({s28, 0x07ADD2},
                 {QStringLiteral("rail pressure"), 2, 16, 16, sharedInjectionRpm, {},
                  MapCategory::Injection, 0,
                  // 0x07ADD2 ranges 2266..13500; OEM rail max ~1350 bar so 10:1 scale
                  // gives a calibrated 226..1350 bar range. Verified against typical
                  // EDC15C diesel rail layouts.
                  0.1, 0.0, QStringLiteral("bar")});
        m.insert({s28, 0x072CF0},
                 {QStringLiteral("injection at part throttle (Map 1)"), 3, 16, 20,
                  sharedInjectionRpm, {}, MapCategory::Injection, 0,
                  1.0, 0.0, QString()});
        m.insert({s28, 0x072FC0},
                 {QStringLiteral("injection at part throttle (Map 2)"), 4, 16, 20,
                  sharedInjectionRpm, {}, MapCategory::Injection, 0,
                  1.0, 0.0, QString()});

        // (Map 1) (Boost x RPM): the .drt lists 2 addresses but the reference tool
        // reference only displays the first - we honour that by clamping
        // maxInstances to 1. The address at 0x78F2A is still in the .drt
        // file and bin so a future "expert mode" could expose it.
        m.insert({s28, 0x078C5A},
                 {QStringLiteral("injection at part throttle (Map 1) (Boost x RPM)"), 5,
                  16, 20, sharedInjectionRpm, {}, MapCategory::Injection, 1,
                  1.0, 0.0, QString()});
        m.insert({s28, 0x078F2A},
                 {QStringLiteral("injection at part throttle (Map 1) (Boost x RPM)"), 5,
                  16, 20, sharedInjectionRpm, {}, MapCategory::Injection, 1,
                  1.0, 0.0, QString()});

        // (Map 2) (Boost x RPM): same situation - first address only.
        m.insert({s28, 0x0791FA},
                 {QStringLiteral("injection at part throttle (Map 2) (Boost x RPM)"), 6,
                  16, 20, sharedInjectionRpm, {}, MapCategory::Injection, 1,
                  1.0, 0.0, QString()});
        m.insert({s28, 0x0794CA},
                 {QStringLiteral("injection at part throttle (Map 2) (Boost x RPM)"), 6,
                  16, 20, sharedInjectionRpm, {}, MapCategory::Injection, 1,
                  1.0, 0.0, QString()});

        m.insert({s28, 0x071F52},
                 {QStringLiteral("phase of injection"), 7, 12, 16, {}, {},
                  MapCategory::Injection, 0,
                  // EDC15C SOI map convention: raw / 100 -> deg before TDC.
                  0.01, 0.0, QStringLiteral("degCA")});
        m.insert({s28, 0x07753E},
                 {QStringLiteral("fuel during acceleration"), 8, 12, 10, {}, {},
                  MapCategory::Injection, 0,
                  1.0, 0.0, QString()});
        m.insert({s28, 0x07765E},
                 {QStringLiteral("fuel during acceleration (Map 2)"), 9, 10, 10, {}, {},
                  MapCategory::Injection, 0,
                  1.0, 0.0, QString()});
        m.insert({s28, 0x075EA0},
                 {QStringLiteral("turbo pressure"), 10, 12, 12, {}, {},
                  MapCategory::Turbo, 0,
                  // Boost target: raw value IS the absolute pressure in mbar.
                  // Factory range 1000..2250 -> 1.0..2.25 bar absolute (matches
                  // typical 0.0..1.25 bar gauge on a 2.7 CRD).
                  1.0, 0.0, QStringLiteral("mbar")});
        m.insert({s28, 0x076D82},
                 {QStringLiteral("torque limiter"), 11, 19, 1, {}, {},
                  MapCategory::Limiters, 0,
                  // Mercedes OM612 (Jeep WJ 2.7 CRD) factory peak torque
                  // is 400 Nm at 1800-2400 rpm, corresponding to factory
                  // peak limiter raw value 7500 (verified across stock
                  // bins 293-822 and 409-438 at rows 6-7). Linear proxy
                  // 400/7500 = 0.05333 Nm per raw count.
                  400.0 / 7500.0, 0.0, QStringLiteral("Nm")});

        return m;
    }();
    return t;
}

const NameEntry *lookup(const QString &schemaId, const MapDefinition &map)
{
    if (map.addresses.isEmpty())
        return nullptr;
    const MapKey k { schemaId, map.addresses.first() };
    auto it = table().constFind(k);
    if (it == table().constEnd())
        return nullptr;
    return &it.value();
}

} // namespace

QString DriverNames::canonicalName(const QString &schemaId,
                                   const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->name;
    return QString();
}

QString DriverNames::displayName(const QString &schemaId,
                                 const MapDefinition &map)
{
    const QString c = canonicalName(schemaId, map);
    if (!c.isEmpty())
        return c;
    return map.displayName();
}

int DriverNames::sortKey(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->order;
    return 9999;
}

int DriverNames::effectiveDimX(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map); e && e->dimXOverride > 0)
        return e->dimXOverride;
    return map.dimX;
}

int DriverNames::effectiveDimY(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map); e && e->dimYOverride > 0)
        return e->dimYOverride;
    return map.dimY;
}

QList<int> DriverNames::axisXOverride(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->axisXOverride;
    return {};
}

QList<int> DriverNames::axisYOverride(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->axisYOverride;
    return {};
}

MapCategory DriverNames::effectiveCategory(const QString &schemaId,
                                           const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->category;
    return categoryForTypeCode(map.typeCode);
}

int DriverNames::maxInstances(const QString &schemaId, const MapDefinition &map)
{
    if (expertMode())
        return 0;  // expose every shadow instance
    if (auto *e = lookup(schemaId, map))
        return e->maxInstances;
    return 0;
}

namespace {
// File-static so toggle survives across calls without bloating the
// public API. Atomic-trivial type, no synchronisation needed (Qt GUI
// is single-threaded; we don't read this from worker threads).
bool g_expertMode = false;
}

void DriverNames::setExpertMode(bool on) { g_expertMode = on; }
bool DriverNames::expertMode()           { return g_expertMode; }

double DriverNames::scaleFor(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->scale;
    return 1.0;
}

double DriverNames::offsetFor(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->offset;
    return 0.0;
}

QString DriverNames::unitFor(const QString &schemaId, const MapDefinition &map)
{
    if (auto *e = lookup(schemaId, map))
        return e->unit;
    return QString();
}

void DriverNames::applyUnitOverride(const QString &schemaId, MapDefinition *map)
{
    if (!map) return;
    if (auto *e = lookup(schemaId, *map)) {
        // Only override when the entry actually carries unit information,
        // so XDF parsers that already populated scale/offset from <MATH>
        // don't get clobbered by a default 1.0/0.0/"" entry.
        if (!e->unit.isEmpty() || e->scale != 1.0 || e->offset != 0.0) {
            map->scale  = e->scale;
            map->offset = e->offset;
            map->unit   = e->unit;
        }
    }
}

} // namespace EcuParser
