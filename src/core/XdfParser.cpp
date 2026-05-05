#include "XdfParser.h"

#include "../model/MapDefinition.h"
#include "../model/AxisDefinition.h"
#include "../model/MapCategory.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
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
    // MATH equation as written in the XDF, e.g. "X * 0.1" or "X/10+5".
    // Empty when the XDF doesn't supply one. Parsed by parseLinearMath()
    // below; anything more complex than a*X+b stays empty (the caller
    // falls back to raw u16 display).
    QString mathEquation;
};

// Parse a simple linear MATH equation into (scale, offset). Recognised
// forms (case-insensitive, whitespace-tolerant):
//   X
//   X * a            X*a              a * X            a*X
//   X / c            X/c
//   X + b            X-b              X * a + b
//   X / c + b        a * X + b        a*X+b
//
// Anything else returns false and leaves scale/offset untouched. We
// keep the parser deliberately small - real-world XDFs use linear
// scaling for ~95% of maps; the long tail (lookup tables, multi-axis
// math) doesn't fit a 1D linear scale anyway and stays raw.
bool parseLinearMath(const QString &eqIn, double *scaleOut, double *offsetOut)
{
    QString eq = eqIn.simplified();
    if (eq.isEmpty()) return false;
    // Normalise: strip all spaces, lowercase the variable.
    eq.remove(QLatin1Char(' '));
    eq.replace(QLatin1Char('x'), QLatin1Char('X'));

    // Bail on operators we don't handle (parens, ^, %, etc).
    static const char kForbidden[] = "()%^,";
    for (char c : kForbidden) {
        if (eq.contains(QLatin1Char(c))) return false;
    }

    // Find X. Must appear exactly once - "X*X" isn't linear.
    const int xCount = eq.count(QLatin1Char('X'));
    if (xCount != 1) return false;

    // Split eq at the X position into a "before X" prefix and an
    // "after X" suffix. Each may contain a leading '*' or '/' (for
    // before) or a trailing '*' or '/' / leading '+' or '-' for after.
    const int xPos = eq.indexOf(QLatin1Char('X'));
    QString before = eq.left(xPos);
    QString after  = eq.mid(xPos + 1);

    double scale  = 1.0;
    double offset = 0.0;

    // Process "before": acceptable patterns are "" or "a*" (constant
    // factor). We parse a leading optional sign, a number, then
    // require a '*' delimiter.
    if (!before.isEmpty()) {
        QChar last = before.back();
        if (last != QLatin1Char('*') && last != QLatin1Char('/')) return false;
        // The "before" token represents a multiplier (or a divisor if
        // future XDFs ever write "10/X" - we don't accept that since
        // it's nonlinear in X). Reject divisor-before-X.
        if (last == QLatin1Char('/')) return false;
        const QString numStr = before.chopped(1);
        bool ok = false;
        const double v = numStr.toDouble(&ok);
        if (!ok) return false;
        scale *= v;
    }

    // Process "after": acceptable patterns are "", "*a", "/c", "+b",
    // "-b", "*a+b", "*a-b", "/c+b", "/c-b". We tokenise greedily.
    while (!after.isEmpty()) {
        const QChar op = after.front();
        after.remove(0, 1);
        // Find the next op (or end of string).
        int nextOp = -1;
        for (int i = 0; i < after.size(); ++i) {
            const QChar c = after.at(i);
            if (c == QLatin1Char('+') || c == QLatin1Char('-')
                || c == QLatin1Char('*') || c == QLatin1Char('/')) {
                // Allow leading '-' / '+' as part of a number ONLY at i==0;
                // but at i==0 we've already consumed the lead op above.
                nextOp = i;
                break;
            }
        }
        const QString tokStr = (nextOp >= 0) ? after.left(nextOp) : after;
        after = (nextOp >= 0) ? after.mid(nextOp) : QString();

        bool ok = false;
        const double v = tokStr.toDouble(&ok);
        if (!ok) return false;

        if (op == QLatin1Char('*'))      scale  *= v;
        else if (op == QLatin1Char('/')) {
            if (v == 0.0) return false;
            scale /= v;
        }
        else if (op == QLatin1Char('+')) offset += v;
        else if (op == QLatin1Char('-')) offset -= v;
        else return false;
    }

    if (scaleOut)  *scaleOut  = scale;
    if (offsetOut) *offsetOut = offset;
    return true;
}

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
        } else if (en == QStringLiteral("MATH")) {
            // <MATH equation="X * 0.1" /> - the equation attribute is
            // the linear formula applied to raw cell values to produce
            // physical units. We capture it verbatim and parse later
            // (parseLinearMath) so the MapDefinition can carry scale +
            // offset for the table widget to display.
            a.mathEquation = xr.attributes()
                                 .value(QStringLiteral("equation")).toString();
            // MATH typically has a child <VAR id="X" /> tag; we don't
            // need it (we already assume X). readElementText would eat
            // it anyway. Using readNextStartElement loop instead would
            // be cleaner but readElementText() is fine since MATH has
            // no additional text payload we care about.
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

    // === MATH equation parsing ===
    // If the z-axis carries a <MATH equation="..."/> formula and it
    // fits the linear form a*X+b (which most XDFs use), record the
    // scale/offset/unit on the MapDefinition so the table widget can
    // show physical units. Editing always operates on raw u16 - the
    // MATH only affects display.
    if (!zAxis.mathEquation.isEmpty()) {
        double scale = 1.0;
        double offset = 0.0;
        if (parseLinearMath(zAxis.mathEquation, &scale, &offset)) {
            out->scale  = scale;
            out->offset = offset;
            if (!zAxis.units.isEmpty()) out->unit = zAxis.units;
        }
        // If the parse failed (non-linear formula) we leave scale=1,
        // offset=0, unit empty so the user sees raw u16 cells - the
        // safe degraded behaviour.
    } else if (!zAxis.units.isEmpty()) {
        // No MATH but units present: at least record the unit string
        // so the title and tooltips can show "(unit: bar)" even
        // without conversion.
        out->unit = zAxis.units;
    }

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

    // Detect non-XML XDF variants up front so we can give the user a
    // useful, specific error instead of the generic "incorrectly
    // encoded content" message that QXmlStreamReader produces when it
    // hits a binary file.
    //
    // TunerPro XDFs come in three flavours in the wild:
    //   1. Plain XML (what we support) - opens with "<?xml" or "<XDF"
    //   2. Legacy binary XDF format (early TunerPro versions, no
    //      published spec) - mostly-zero with sparse structure
    //   3. Encrypted XDF (TunerPro RT password-protected) - high-
    //      entropy random-looking bytes throughout
    //
    // The detection is heuristic but cheap and produces clear
    // diagnostics. We sample 1 KB starting at offset 0x100 (after
    // any small header) because both encrypted and legacy binary
    // XDFs commonly have a sparse 0..0xFF header before the actual
    // payload begins.
    QByteArray peek = f.peek(0x500);  // 1280 bytes
    f.seek(0);

    auto looksLikeXml = [](const QByteArray &b) {
        // Skip common BOMs, then look for '<' as the first non-
        // whitespace character.
        int i = 0;
        if (b.size() >= 3 &&
            quint8(b[0]) == 0xEF && quint8(b[1]) == 0xBB && quint8(b[2]) == 0xBF) {
            i = 3;
        } else if (b.size() >= 2 &&
            (quint8(b[0]) == 0xFF && quint8(b[1]) == 0xFE)) {
            // UTF-16 LE BOM - rare but valid for XML
            return true;
        } else if (b.size() >= 2 &&
            (quint8(b[0]) == 0xFE && quint8(b[1]) == 0xFF)) {
            // UTF-16 BE BOM
            return true;
        }
        while (i < b.size() &&
               (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n'))
            ++i;
        return i < b.size() && b[i] == '<';
    };

    if (!looksLikeXml(peek)) {
        // Telling encrypted XDF apart from legacy binary XDF: we
        // sample bytes at offset 0x100..0x200 (256 bytes deep into
        // the file, past any small length-prefix header) and count
        // unique byte values. Encrypted output is essentially
        // uniform random - in 256 bytes we expect ~150-200 distinct
        // values. Legacy binary XDF has heavy zero-padding and lots
        // of repeated small integers - usually <100 distinct values
        // in any 256-byte window.
        const int sampleStart = qMin(0x100, peek.size());
        const int sampleEnd   = qMin(0x200, peek.size());
        QSet<quint8> distinct;
        int nonZero = 0;
        for (int i = sampleStart; i < sampleEnd; ++i) {
            const quint8 b = quint8(peek[i]);
            distinct.insert(b);
            if (b != 0) ++nonZero;
        }
        const int windowSize = sampleEnd - sampleStart;
        const double nonZeroRatio = windowSize > 0
            ? double(nonZero) / double(windowSize) : 0.0;
        const bool highEntropy = (distinct.size() >= 100)
                              && (nonZeroRatio >= 0.85);

        if (highEntropy) {
            return fail(QStringLiteral(
                "This XDF file appears to be encrypted "
                "(TunerPro RT password-protected). EcuParser cannot "
                "read encrypted XDFs - the format is closed and "
                "requires the original password. Open the file in "
                "TunerPro RT, use 'Save As' to export an unprotected "
                "copy, then load that copy here."));
        }
        return fail(QStringLiteral(
            "This XDF file is in TunerPro's legacy binary format, "
            "not the XML format EcuParser supports. Open it in a "
            "recent version of TunerPro RT and save it as an XML "
            "XDF (the default for new files), then load that here."));
    }

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
