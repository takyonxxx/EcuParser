#include "XdfParser.h"

#include "../model/MapDefinition.h"
#include "../model/AxisDefinition.h"
#include "../model/MapCategory.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

namespace EcuParser {

namespace {

// Parse a hex-or-decimal string into a quint32. XDF numeric attributes
// are written as either "0x76F52" or "486226" - both are valid. Empty
// strings or unparsable input return 0 (callers treat that as "no
// embedded data" if combined with rowcount==0).
quint32 parseNum(const QString &s)
{
    if (s.isEmpty()) return 0;
    bool ok = false;
    if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        const quint32 v = s.mid(2).toUInt(&ok, 16);
        return ok ? v : 0;
    }
    const quint32 v = s.toUInt(&ok, 10);
    return ok ? v : 0;
}

// Pick a MapCategory from a free-text category name (XDF lets the
// author write arbitrary names). We accept the common ones; anything
// else falls into Other.
MapCategory categoryFromName(const QString &name)
{
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("inject")) || n.contains(QStringLiteral("fuel"))
        || n.contains(QStringLiteral("rail"))   || n.contains(QStringLiteral("pilot")))
        return MapCategory::Injection;
    if (n.contains(QStringLiteral("boost")) || n.contains(QStringLiteral("turbo"))
        || n.contains(QStringLiteral("vgt"))  || n.contains(QStringLiteral("map")))
        return MapCategory::Turbo;
    if (n.contains(QStringLiteral("limit")) || n.contains(QStringLiteral("max"))
        || n.contains(QStringLiteral("ceil"))  || n.contains(QStringLiteral("torque")))
        return MapCategory::Limiters;
    if (n.contains(QStringLiteral("timing")) || n.contains(QStringLiteral("phase"))
        || n.contains(QStringLiteral("advance")))
        return MapCategory::Timing;
    return MapCategory::Other;
}

// Read a single <XDFAXIS> element to learn about either an axis
// breakpoint table or the cell payload. Caller passes 'idAttr' = "x",
// "y", or "z". The reader is positioned at the start tag; this function
// consumes through the matching end tag.
struct AxisInfo {
    quint32 address  = 0;
    int     bitSize  = 16;
    int     rowCount = 0;
    int     colCount = 0;
    QString units;
};

AxisInfo readAxis(QXmlStreamReader &xr)
{
    AxisInfo a;
    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isEndElement() && xr.name() == QStringLiteral("XDFAXIS"))
            break;
        if (!xr.isStartElement())
            continue;

        const auto en = xr.name().toString();
        if (en == QStringLiteral("EMBEDDEDDATA")) {
            const auto attrs = xr.attributes();
            a.address  = parseNum(attrs.value(QStringLiteral("mmedaddress")).toString());
            a.bitSize  = attrs.value(QStringLiteral("mmedelementsizebits")).toInt();
            a.rowCount = attrs.value(QStringLiteral("mmedrowcount")).toInt();
            a.colCount = attrs.value(QStringLiteral("mmedcolcount")).toInt();
            if (a.bitSize == 0) a.bitSize = 16;
        } else if (en == QStringLiteral("units")) {
            a.units = xr.readElementText();
        } else if (en == QStringLiteral("indexcount")) {
            const int n = xr.readElementText().toInt();
            // indexcount can fill in a missing rowcount when EMBEDDEDDATA
            // is absent (e.g. the dummy y axis on a 1D table).
            if (a.rowCount == 0) a.rowCount = n;
        }
    }
    return a;
}

// Read one <XDFTABLE> into a MapDefinition. Returns false if the table
// is unusable (missing z-axis cell address, zero size, etc).
bool readTable(QXmlStreamReader &xr,
               const QHash<int, QString> &categoryNames,
               MapDefinition *out)
{
    QString title;
    QString description;
    int catIdx = -1;
    AxisInfo xAxis, yAxis, zAxis;

    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isEndElement() && xr.name() == QStringLiteral("XDFTABLE"))
            break;
        if (!xr.isStartElement())
            continue;

        const auto en = xr.name().toString();
        if (en == QStringLiteral("title")) {
            title = xr.readElementText().trimmed();
        } else if (en == QStringLiteral("description")) {
            description = xr.readElementText().trimmed();
        } else if (en == QStringLiteral("CATEGORYMEM")) {
            catIdx = xr.attributes().value(QStringLiteral("category")).toInt();
        } else if (en == QStringLiteral("XDFAXIS")) {
            const QString id = xr.attributes()
                                   .value(QStringLiteral("id")).toString();
            const AxisInfo a = readAxis(xr);
            if (id == QStringLiteral("x"))      xAxis = a;
            else if (id == QStringLiteral("y")) yAxis = a;
            else if (id == QStringLiteral("z")) zAxis = a;
        }
    }

    if (zAxis.address == 0 || zAxis.rowCount == 0)
        return false;
    // colCount==0 means a 1D table; treat as 1 column.
    const int dy = (zAxis.colCount > 0) ? zAxis.colCount : 1;

    out->name      = title;
    out->typeCode  = QStringLiteral("XDF");
    out->dimX      = zAxis.rowCount;
    out->dimY      = dy;
    out->cellSize  = zAxis.bitSize / 8;
    if (out->cellSize == 0) out->cellSize = 2;
    out->addresses.clear();
    out->addresses.append(zAxis.address);

    // Axis breakpoints: stored in xAxis/yAxis EMBEDDEDDATA addresses
    // when present. We capture them as AxisDefinition pointers; the
    // app will read them lazily from the bin like any other axis.
    if (xAxis.address != 0 && xAxis.rowCount > 0) {
        out->axisX.group = 'G';
        out->axisX.kind = 'P';
        out->axisX.formula = 0;          // raw breakpoint table
        out->axisX.parameter = xAxis.units;
        out->axisX.address = xAxis.address;
    }
    if (yAxis.address != 0 && yAxis.rowCount > 0) {
        out->axisY.group = 'G';
        out->axisY.kind = 'P';
        out->axisY.formula = 0;
        out->axisY.parameter = yAxis.units;
        out->axisY.address = yAxis.address;
    }

    // Map category: prefer the explicit XDF index; if missing, derive
    // from the table title.
    if (catIdx >= 0 && categoryNames.contains(catIdx))
        out->categoryHint = categoryFromName(categoryNames.value(catIdx));
    else
        out->categoryHint = categoryFromName(title);

    Q_UNUSED(description);
    return true;
}

} // namespace

std::optional<DriverModel> XdfParser::parseFile(const QString &path,
                                                QString *errorOut)
{
    auto fail = [&](const QString &m) -> std::optional<DriverModel> {
        if (errorOut) *errorOut = m;
        return std::nullopt;
    };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot open: %1").arg(f.errorString()));

    QXmlStreamReader xr(&f);

    DriverModel model;
    model.schemaId = QFileInfo(path).baseName(); // unique per XDF file
    QHash<int, QString> categoryNames;
    QString headerDescription;

    while (!xr.atEnd()) {
        xr.readNext();
        if (!xr.isStartElement()) continue;
        const auto en = xr.name().toString();

        if (en == QStringLiteral("CATEGORY")) {
            const auto attrs = xr.attributes();
            const int idx = parseNum(attrs.value(QStringLiteral("index")).toString());
            const QString nm = attrs.value(QStringLiteral("name")).toString();
            categoryNames.insert(idx, nm);
        } else if (en == QStringLiteral("description")) {
            // Header-level description (not inside a table).
            headerDescription = xr.readElementText().trimmed();
        } else if (en == QStringLiteral("XDFTABLE")) {
            MapDefinition def;
            if (readTable(xr, categoryNames, &def))
                model.maps.append(def);
        }
    }

    if (xr.hasError())
        return fail(QStringLiteral("XML parse: %1").arg(xr.errorString()));
    if (model.maps.isEmpty())
        return fail(QStringLiteral("No usable <XDFTABLE> entries in %1")
                        .arg(QFileInfo(path).fileName()));

    return model;
}

} // namespace EcuParser
