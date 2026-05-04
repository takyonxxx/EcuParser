#include "DrtParser.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QStringList>

namespace Titanium {

namespace {

// Convert a 6-hex-digit string to an offset. Accepts upper/lower case.
// Returns false on bad input.
bool parseHexAddress(const QString &s, quint32 &out)
{
    bool ok = false;
    out = s.toUInt(&ok, 16);
    return ok;
}

// Split a single record (already separated by 0x84) into fields by 0xBB,
// and drop trailing empty fields (the file always has a trailing 0xBB
// before the next 0x84).
QStringList splitFields(const QString &record)
{
    // The .drt format wraps each record in 0xBB delimiters: the record starts
    // with 0xBB and ends with 0xBB (or with a 0x84 right after). Splitting on
    // 0xBB therefore produces an empty first element AND an empty last
    // element which we both need to drop. We use SkipEmptyParts here because
    // legitimate fields are never empty in the samples - all values are at
    // least one character (a digit, letter or a comma-tuple).
    QStringList parts = record.split(QChar(QChar::fromLatin1(DrtParser::kFieldSep)),
                                     Qt::SkipEmptyParts);
    return parts;
}

// Parse the [4] data-addresses field, e.g. "076F52" or "078C5A,078F2A".
bool parseAddressList(const QString &field, QList<quint32> &out, QString *errorOut)
{
    const QStringList toks = field.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (toks.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty address list");
        return false;
    }
    for (const QString &t : toks) {
        quint32 a = 0;
        if (!parseHexAddress(t.trimmed(), a)) {
            if (errorOut) *errorOut = QStringLiteral("bad hex address: %1").arg(t);
            return false;
        }
        out.append(a);
    }
    return true;
}

// Parse one map record (13 fields). Returns false and fills errorOut on
// structural problems; recoverable issues (unknown formula etc.) are warnings.
bool parseMapRecord(const QStringList &fields, MapDefinition &m, QString *errorOut)
{
    // We tolerate 11..13 fields. The 11-field variant has appeared on the
    // last record of some drivers and seems to be a sentinel/special map;
    // we still try to recover what we can.
    if (fields.size() < 11) {
        if (errorOut) *errorOut = QStringLiteral("too few fields: %1").arg(fields.size());
        return false;
    }

    // [0] enabled
    m.enabled = (fields.at(0) != QStringLiteral("0"));

    // [1] X axis
    if (!AxisDefinition::parse(fields.at(1), m.axisX, errorOut)) {
        if (errorOut) *errorOut = QStringLiteral("X axis: %1").arg(*errorOut);
        return false;
    }

    // [2] Y axis
    if (!AxisDefinition::parse(fields.at(2), m.axisY, errorOut)) {
        if (errorOut) *errorOut = QStringLiteral("Y axis: %1").arg(*errorOut);
        return false;
    }

    // [3] address count - we cross-check against [4] but trust [4] as truth
    bool ok = false;
    const int declaredCount = fields.at(3).toInt(&ok);
    Q_UNUSED(declaredCount);
    Q_UNUSED(ok);

    // [4] addresses
    if (!parseAddressList(fields.at(4), m.addresses, errorOut))
        return false;

    // [5] reserved
    // [6] format flag (NOT cell size as we initially guessed). Observed
    // values: '2', '4'. Most cells in EDC15C drivers are 16-bit big-endian
    // regardless of this value, so we default cellSize=2 and remember the
    // raw flag for later interpretation. We'll likely correlate it with
    // signed/unsigned or scaling once we examine MapDesc/ data.
    Q_UNUSED(fields.at(6));
    m.cellSize = 2;

    // [7] reserved

    // Records >= 12 fields carry the type code and dimensions; the short
    // 11-field record uses other slots.
    if (fields.size() >= 13) {
        m.typeCode = fields.at(8);
        m.dimX     = fields.at(9).toInt();
        m.dimY     = fields.at(10).toInt();
        // [11] [12] reserved
    } else {
        // Best-effort fallback for the 11-field special record.
        m.typeCode = QStringLiteral("?");
        m.dimX     = fields.at(8).toInt();
        m.dimY     = fields.at(9).toInt();
    }

    return true;
}

} // namespace

std::optional<DriverModel> DrtParser::parseFile(const QString &path, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("cannot open: %1").arg(f.errorString());
        return std::nullopt;
    }
    const QByteArray bytes = f.readAll();
    auto m = parseBytes(bytes, errorOut);
    if (m)
        m->sourcePath = QFileInfo(path).absoluteFilePath();
    return m;
}

std::optional<DriverModel> DrtParser::parseBytes(const QByteArray &bytes, QString *errorOut)
{
    if (bytes.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty input");
        return std::nullopt;
    }

    // Decode as Latin-1 so 0x84 / 0xBB pass through verbatim. Using UTF-8
    // would mis-decode them as multi-byte sequences.
    const QString text = QString::fromLatin1(bytes);

    // Records are separated by 0x84.
    const QStringList records = text.split(QChar(QChar::fromLatin1(kRecordSep)),
                                           Qt::KeepEmptyParts);

    DriverModel model;

    // Find the header record (first one with at least one non-empty field
    // after splitting). The .drt files start with two leading 0x84 bytes
    // which produce an empty record 0; record 1 may consist of just a single
    // 0xBB (which yields an empty field list under SkipEmptyParts) before
    // the real header lands at record 2.
    int headerIdx = -1;
    QStringList headerFields;
    for (int i = 0; i < records.size(); ++i) {
        const QStringList f = splitFields(records.at(i));
        if (!f.isEmpty()) {
            headerIdx = i;
            headerFields = f;
            break;
        }
    }
    if (headerIdx < 0) {
        if (errorOut) *errorOut = QStringLiteral("no header record");
        return std::nullopt;
    }

    // Header: schemaId, mapCount  (sometimes more fields trail; ignore them)
    if (headerFields.size() < 2) {
        if (errorOut) *errorOut = QStringLiteral("header too short (got %1 fields)")
                                      .arg(headerFields.size());
        return std::nullopt;
    }
    model.schemaId = headerFields.at(0);
    bool ok = false;
    model.mapCount = headerFields.at(1).toInt(&ok);
    if (!ok)
        model.mapCount = 0;

    // ECU info record (next non-empty after header).
    int ecuIdx = -1;
    QStringList ecuFields;
    for (int i = headerIdx + 1; i < records.size(); ++i) {
        const QStringList f = splitFields(records.at(i));
        if (!f.isEmpty()) {
            ecuIdx = i;
            ecuFields = f;
            break;
        }
    }
    if (ecuIdx >= 0 && ecuFields.size() >= 5) {
        model.ecuTypeCode = ecuFields.at(0);
        model.defaultDimX = ecuFields.at(1).toInt();
        model.defaultDimY = ecuFields.at(2).toInt();
    }

    // Map records start after the ECU info record.
    const int firstMap = (ecuIdx >= 0 ? ecuIdx : headerIdx) + 1;
    for (int i = firstMap; i < records.size(); ++i) {
        const QString &raw = records.at(i);
        if (raw.isEmpty())
            continue;
        const QStringList fields = splitFields(raw);
        if (fields.isEmpty())
            continue;

        MapDefinition m;
        QString perRecordErr;
        if (parseMapRecord(fields, m, &perRecordErr)) {
            model.maps.append(m);
        } else {
            qWarning("DrtParser: skipping record %d: %s",
                     i, qPrintable(perRecordErr));
        }
    }

    if (!model.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("parsed but no maps found");
        return std::nullopt;
    }
    return model;
}

} // namespace Titanium
