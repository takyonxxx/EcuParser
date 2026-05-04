#include "MapCategory.h"
#include "AxisDefinition.h"
#include "MapDefinition.h"
#include "DriverModel.h"

#include <QStringList>
#include <QMap>

namespace EcuParser {

// ============================================================
// AxisDefinition::parse
// ============================================================
bool AxisDefinition::parse(const QString &field, AxisDefinition &out, QString *errorOut)
{
    const QStringList parts = field.split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (parts.size() != 4) {
        if (errorOut)
            *errorOut = QStringLiteral("axis tuple needs 4 parts, got %1: '%2'")
                            .arg(parts.size()).arg(field);
        return false;
    }
    if (parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("axis group/kind empty");
        return false;
    }
    out.group   = parts.at(0).at(0).toLatin1();
    out.kind    = parts.at(1).at(0).toLatin1();
    out.formula = parts.at(2).toInt();

    bool ok = false;
    out.address = parts.at(3).toUInt(&ok, 16);
    if (!ok) out.address = 0;
    return true;
}

QString AxisDefinition::toDebugString() const
{
    return QStringLiteral("%1,%2,%3,%4")
        .arg(QChar::fromLatin1(group))
        .arg(QChar::fromLatin1(kind))
        .arg(formula)
        .arg(QStringLiteral("%1").arg(address, 6, 16, QLatin1Char('0')).toUpper());
}

// ============================================================
// MapDefinition::displayName  (best-effort fallback names)
// ============================================================
QString MapDefinition::displayName() const
{
    // If the parser supplied a human-readable name (XDF <title>),
    // use it directly. This takes priority over any type-code logic
    // because XDF authors write descriptive titles.
    if (!name.isEmpty())
        return name;

    // Without a canonical mapping table from MapDesc/ files, provide
    // sensible English labels based on type code prefix. Phase 2 will
    // override these from per-driver descriptions.
    static const QMap<QString, QString> table {
        { QStringLiteral("PR"), QStringLiteral("rail pressure") },
        { QStringLiteral("BS"), QStringLiteral("turbo pressure") },
        { QStringLiteral("BT"), QStringLiteral("boost target") },
        { QStringLiteral("L0"), QStringLiteral("torque limiter") },
        { QStringLiteral("L1"), QStringLiteral("RPM limiter") },
        { QStringLiteral("L2"), QStringLiteral("speed limiter") },
        { QStringLiteral("L3"), QStringLiteral("limiter (extra)") },
        { QStringLiteral("PT"), QStringLiteral("pilot timing") },
        { QStringLiteral("TB"), QStringLiteral("timing/boost") },
    };

    auto it = table.constFind(typeCode);
    if (it != table.constEnd())
        return it.value();

    // Default for I-family maps.
    if (typeCode.startsWith(QLatin1Char('I')))
        return QStringLiteral("injection (%1)").arg(typeCode);

    return QStringLiteral("unnamed (%1)").arg(typeCode);
}

// ============================================================
// DriverModel::mapsByCategory
// ============================================================
QList<QPair<MapCategory, QList<const MapDefinition*>>> DriverModel::mapsByCategory() const
{
    // Stable display order matching the reference tool UI.
    static const QList<MapCategory> order {
        MapCategory::Injection,
        MapCategory::Turbo,
        MapCategory::Limiters,
        MapCategory::Timing,
        MapCategory::Other,
    };

    QMap<MapCategory, QList<const MapDefinition*>> bucket;
    for (const MapDefinition &m : maps)
        bucket[m.category()].append(&m);

    QList<QPair<MapCategory, QList<const MapDefinition*>>> result;
    for (MapCategory c : order) {
        if (bucket.contains(c) && !bucket.value(c).isEmpty())
            result.append(qMakePair(c, bucket.value(c)));
    }
    return result;
}

// ============================================================
// (existing categoryForTypeCode / categoryDisplayName below)
// ============================================================

MapCategory categoryForTypeCode(const QString &typeCode)
{
    if (typeCode.isEmpty())
        return MapCategory::Other;

    // PR is rail pressure, lives under INJECTION in the reference tool UI
    if (typeCode == QStringLiteral("PR"))
        return MapCategory::Injection;

    // PT = pilot/timing - keep under Timing for now, may revise after seeing more drivers
    if (typeCode == QStringLiteral("PT"))
        return MapCategory::Timing;

    const QChar c = typeCode.at(0);
    switch (c.toLatin1()) {
    case 'I':                   // I0..I9, IA..IF
    case 'P':                   // P*  - pressure family (PR caught above)
        return MapCategory::Injection;
    case 'B':                   // B* / BS - boost
    case 'T':                   // TB / TBA-like - in some drivers timing/boost
        return MapCategory::Turbo;
    case 'L':                   // L0..L9 - 1D limiters
        return MapCategory::Limiters;
    default:
        return MapCategory::Other;
    }
}

QString categoryDisplayName(MapCategory cat)
{
    switch (cat) {
    case MapCategory::Injection: return QStringLiteral("INJECTION");
    case MapCategory::Turbo:     return QStringLiteral("TURBO");
    case MapCategory::Limiters:  return QStringLiteral("LIMITERS");
    case MapCategory::Timing:    return QStringLiteral("TIMING");
    case MapCategory::Other:     return QStringLiteral("OTHER");
    }
    return QStringLiteral("OTHER");
}

} // namespace EcuParser
