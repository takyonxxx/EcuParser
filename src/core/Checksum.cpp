#include "Checksum.h"

#include <QDebug>

namespace EcuParser {

namespace {

// 28F0_100 profile: three Bosch EDC15C calibration blocks, each
// followed by the marker "C3 C3 C3 13 C8" then a 4-byte checksum
// word. See Checksum.h header comments for derivation.
ChecksumProfile build28F0Profile()
{
    ChecksumProfile p;
    p.schemaId = QStringLiteral("28F0_100");
    p.name     = QStringLiteral("EDC15C 28F0_100 (3-block opaque CRC)");

    auto addBlock = [&](quint32 s, quint32 e, quint32 store, const char *desc) {
        ChecksumRange r;
        r.startOffset = s;
        r.endOffset   = e;
        r.storeOffset = store;
        r.storeLength = 4;
        r.algorithm   = ChecksumAlgorithm::OpaqueCrc32;
        r.description = QString::fromLatin1(desc);
        p.ranges.append(r);
    };

    // Block A: bootloader / startup code. The marker C3 C3 C3 13 C8
    // ends at 0x013FFB and the 4-byte checksum word lives at
    // 0x013FFC. Almost no tunes touch this block (it is not
    // calibration data) so its checksum is normally KeepOriginal.
    addBlock(0x000000, 0x013FFB, 0x013FFC,
             "Block A: 0x000000..0x013FFB checksum @ 0x013FFC");

    // Block B: main calibration block. Holds every map (injection,
    // rail pressure, turbo pressure, torque limiter, smoke limiter,
    // EGR, etc.). 99% of stages touch only this block. Marker ends
    // at 0x07BD7B, checksum at 0x07BD7C.
    addBlock(0x014000, 0x07BD7B, 0x07BD7C,
             "Block B: 0x014000..0x07BD7B checksum @ 0x07BD7C (main calibration)");

    // Block C: trailing module ID block + version stamp + hardware
    // tables. Marker ends at 0x07FCFB, checksum at 0x07FCFC. Stages
    // never edit this block; if it changes, the user has done
    // something custom and likely needs a real checksum tool.
    addBlock(0x07BD80, 0x07FCFB, 0x07FCFC,
             "Block C: 0x07BD80..0x07FCFB checksum @ 0x07FCFC");

    return p;
}

} // namespace

ChecksumProfile Checksum::profileForSchema(const QString &schemaId)
{
    if (schemaId == QStringLiteral("28F0_100"))
        return build28F0Profile();
    return ChecksumProfile{};
}

quint32 Checksum::readStored(const BinFile &bin, const ChecksumRange &r)
{
    bool ok = false;
    switch (r.algorithm) {
    case ChecksumAlgorithm::Sum32BE:
        return bin.readU32BE(r.storeOffset, &ok);
    case ChecksumAlgorithm::Sum32LE:
    case ChecksumAlgorithm::OpaqueCrc32: {
        // For display we treat OpaqueCrc32 the same as Sum32LE: the
        // 4 bytes are read as a little-endian u32 (so byte order
        // matches what a hex viewer shows for the 4 bytes in
        // ascending address). This is purely cosmetic; we never
        // compute a value to match it.
        const quint16 lo = bin.readU16LE(r.storeOffset, &ok);
        const quint16 hi = bin.readU16LE(r.storeOffset + 2, &ok);
        return (quint32(hi) << 16) | quint32(lo);
    }
    case ChecksumAlgorithm::Sum16BE:
        return bin.readU16BE(r.storeOffset, &ok);
    case ChecksumAlgorithm::XorBytes:
        return bin.readU8(r.storeOffset, &ok);
    }
    return 0;
}

void Checksum::writeStored(BinFile *bin, const ChecksumRange &r, quint32 value)
{
    if (!bin) return;
    switch (r.algorithm) {
    case ChecksumAlgorithm::Sum32BE:
        bin->writeU16BE(r.storeOffset,     quint16(value >> 16));
        bin->writeU16BE(r.storeOffset + 2, quint16(value & 0xFFFFu));
        break;
    case ChecksumAlgorithm::Sum32LE:
    case ChecksumAlgorithm::OpaqueCrc32:
        bin->writeU16LE(r.storeOffset,     quint16(value & 0xFFFFu));
        bin->writeU16LE(r.storeOffset + 2, quint16(value >> 16));
        break;
    case ChecksumAlgorithm::Sum16BE:
        bin->writeU16BE(r.storeOffset, quint16(value & 0xFFFFu));
        break;
    case ChecksumAlgorithm::XorBytes: {
        QByteArray b(1, char(quint8(value & 0xFFu)));
        bin->writeBytes(r.storeOffset, b);
        break;
    }
    }
}

bool Checksum::blockBytesMatch(const QByteArray &a, const QByteArray &b,
                               quint32 start, quint32 endIncl)
{
    if (endIncl < start) return false;
    const qsizetype len = qsizetype(endIncl - start + 1);
    if (qsizetype(start) + len > a.size() || qsizetype(start) + len > b.size())
        return false;
    // memcmp via QByteArray; fast path.
    return std::memcmp(a.constData() + qsizetype(start),
                       b.constData() + qsizetype(start),
                       size_t(len)) == 0;
}

ChecksumStatus Checksum::evaluate(const BinFile &modBin,
                                  const BinFile &origBin,
                                  const BinFile *refBin,
                                  const ChecksumProfile &p)
{
    ChecksumStatus s;
    if (p.ranges.isEmpty()) {
        s.warnings.append(QStringLiteral(
            "No checksum profile available for this schema."));
        return s;
    }

    const QByteArray modRaw  = modBin.raw();
    const QByteArray origRaw = origBin.raw();
    const QByteArray refRaw  = refBin ? refBin->raw() : QByteArray();

    // Sanity: original must cover every protected range. If it
    // doesn't, we can't provide the KeepOriginal fallback.
    if (origRaw.size() != modRaw.size()) {
        s.warnings.append(QStringLiteral(
            "Original bin size (%1) differs from modified bin size (%2). "
            "Cannot evaluate checksums.")
                .arg(origRaw.size()).arg(modRaw.size()));
        return s;
    }
    const bool refUsable = refBin && refRaw.size() == modRaw.size();
    if (refBin && !refUsable) {
        s.warnings.append(QStringLiteral(
            "Reference bin size (%1) differs from modified bin size (%2). "
            "Reference will be ignored.")
                .arg(refRaw.size()).arg(modRaw.size()));
    }

    for (const ChecksumRange &r : p.ranges) {
        ChecksumBlockStatus b;
        b.description = r.description;
        b.startOffset = r.startOffset;
        b.endOffset   = r.endOffset;
        b.storeOffset = r.storeOffset;

        // OOR guard: store offset and end offset must fit inside bin.
        if (qsizetype(r.storeOffset) + r.storeLength > modRaw.size()
            || qsizetype(r.endOffset) >= modRaw.size()) {
            s.warnings.append(QStringLiteral(
                "Range out of bin size: %1").arg(r.description));
            b.strategy = ChecksumStrategy::Unresolvable;
            s.blocks.append(b);
            continue;
        }

        b.storedValue   = readStored(modBin,  r);
        b.originalValue = readStored(origBin, r);
        if (refUsable) {
            BinFile tmp(refRaw);
            b.referenceValue = readStored(tmp, r);
            b.hasReference = true;
        }

        b.dataMatchesOriginal = blockBytesMatch(
            modRaw, origRaw, r.startOffset, r.endOffset);
        if (refUsable) {
            b.dataMatchesReference = blockBytesMatch(
                modRaw, refRaw, r.startOffset, r.endOffset);
        }

        // Strategy selection: KeepOriginal wins when possible (it's
        // the safest - the ECU was running on this bin, so the
        // original checksum is by definition valid for these
        // bytes). Reference is only consulted when the bytes
        // diverge from original.
        if (b.dataMatchesOriginal) {
            b.strategy = ChecksumStrategy::KeepOriginal;
        } else if (b.dataMatchesReference) {
            b.strategy = ChecksumStrategy::CopyFromReference;
        } else {
            b.strategy = ChecksumStrategy::Unresolvable;
        }
        s.blocks.append(b);
    }
    return s;
}

bool Checksum::applyStrategies(BinFile *modBin,
                               const BinFile &origBin,
                               const BinFile *refBin,
                               const ChecksumProfile &p,
                               QStringList *log)
{
    if (!modBin) return false;
    const ChecksumStatus st = evaluate(*modBin, origBin, refBin, p);
    if (st.blocks.isEmpty()) {
        if (log) log->append(QStringLiteral("No checksum blocks to apply."));
        return false;
    }

    bool allOk = true;
    for (int i = 0; i < st.blocks.size(); ++i) {
        const ChecksumBlockStatus &b = st.blocks.at(i);
        const ChecksumRange &r = p.ranges.at(i);
        switch (b.strategy) {
        case ChecksumStrategy::KeepOriginal: {
            if (b.storedValue != b.originalValue) {
                writeStored(modBin, r, b.originalValue);
                if (log) log->append(QStringLiteral(
                    "%1: KeepOriginal - wrote 0x%2 (was 0x%3)")
                    .arg(r.description)
                    .arg(b.originalValue, 8, 16, QLatin1Char('0')).toUpper()
                    .arg(b.storedValue,   8, 16, QLatin1Char('0')).toUpper());
            } else {
                if (log) log->append(QStringLiteral(
                    "%1: KeepOriginal - already 0x%2 (no write)")
                    .arg(r.description)
                    .arg(b.originalValue, 8, 16, QLatin1Char('0')).toUpper());
            }
            break;
        }
        case ChecksumStrategy::CopyFromReference: {
            if (b.storedValue != b.referenceValue) {
                writeStored(modBin, r, b.referenceValue);
                if (log) log->append(QStringLiteral(
                    "%1: CopyFromReference - wrote 0x%2 (was 0x%3)")
                    .arg(r.description)
                    .arg(b.referenceValue, 8, 16, QLatin1Char('0')).toUpper()
                    .arg(b.storedValue,    8, 16, QLatin1Char('0')).toUpper());
            } else {
                if (log) log->append(QStringLiteral(
                    "%1: CopyFromReference - already 0x%2 (no write)")
                    .arg(r.description)
                    .arg(b.referenceValue, 8, 16, QLatin1Char('0')).toUpper());
            }
            break;
        }
        case ChecksumStrategy::Unresolvable: {
            allOk = false;
            if (log) log->append(QStringLiteral(
                "%1: UNRESOLVABLE - block bytes differ from both original "
                "and reference. Send the modified bin to a commercial "
                "checksum corrector (WinOLS / ECM Titanium / MPPS / "
                "ecu.design online) before flashing the ECU.")
                .arg(r.description));
            break;
        }
        }
    }
    return allOk;
}

PartialMetrics computePartialMetrics(const BinFile &bin,
                                     quint32 startOffset,
                                     quint32 endOffsetIncl)
{
    PartialMetrics m;
    m.startOffset = startOffset;
    m.endOffset   = endOffsetIncl;

    const QByteArray &raw = bin.raw();
    if (endOffsetIncl < startOffset
        || qsizetype(endOffsetIncl) >= raw.size()) {
        return m;
    }

    // Single pass over bytes for byte_sum, even_sum, odd_sum.
    quint64 byteTotal = 0;
    quint64 evenTotal = 0;
    quint64 oddTotal  = 0;
    const quint8 *p = reinterpret_cast<const quint8 *>(raw.constData());
    for (quint32 i = startOffset; i <= endOffsetIncl; ++i) {
        byteTotal += p[i];
        if ((i & 1u) == 0) evenTotal += p[i];
        else               oddTotal  += p[i];
    }

    // Word pass (16-bit aligned).
    quint64 wordSumLE = 0;
    quint64 wordSumBE = 0;
    for (quint32 i = startOffset; i + 1 <= endOffsetIncl; i += 2) {
        const quint8 a = p[i];
        const quint8 b = p[i + 1];
        wordSumLE += quint16(quint16(a) | (quint16(b) << 8));
        wordSumBE += quint16(quint16(b) | (quint16(a) << 8));
    }

    // Dword pass (32-bit aligned). Four sums via different byte
    // orderings - matches ECM Titanium 1.61's "32 bit #1..#4" labels.
    quint64 dwBE      = 0;     // 0,1,2,3 -> #1
    quint64 dwSwapHL  = 0;     // 1,0,3,2 -> #2
    quint64 dwLE      = 0;     // 3,2,1,0 -> #3
    quint64 dwSwapWord = 0;    // 2,3,0,1 -> #4
    for (quint32 i = startOffset; i + 3 <= endOffsetIncl; i += 4) {
        const quint8 b0 = p[i];
        const quint8 b1 = p[i + 1];
        const quint8 b2 = p[i + 2];
        const quint8 b3 = p[i + 3];
        dwBE       += (quint32(b0) << 24) | (quint32(b1) << 16)
                    | (quint32(b2) << 8)  |  quint32(b3);
        dwSwapHL   += (quint32(b1) << 24) | (quint32(b0) << 16)
                    | (quint32(b3) << 8)  |  quint32(b2);
        dwLE       += (quint32(b3) << 24) | (quint32(b2) << 16)
                    | (quint32(b1) << 8)  |  quint32(b0);
        dwSwapWord += (quint32(b2) << 24) | (quint32(b3) << 16)
                    | (quint32(b0) << 8)  |  quint32(b1);
    }

    m.checksum16       = quint16(byteTotal & 0xFFFFu);
    m.complement16     = quint16((~byteTotal) & 0xFFFFu);
    m.even16           = quint16(evenTotal & 0xFFFFu);
    m.odd16            = quint16(oddTotal  & 0xFFFFu);
    m.dword32          = quint32(byteTotal & 0xFFFFFFFFu);
    m.sumWordLE        = quint32(wordSumLE & 0xFFFFFFFFu);
    m.sumWordBE        = quint32(wordSumBE & 0xFFFFFFFFu);
    m.sumDwordBE       = quint32(dwBE      & 0xFFFFFFFFu);
    m.sumDwordSwapHL   = quint32(dwSwapHL  & 0xFFFFFFFFu);
    m.sumDwordLE       = quint32(dwLE      & 0xFFFFFFFFu);
    m.sumDwordSwapWord = quint32(dwSwapWord & 0xFFFFFFFFu);
    return m;
}

} // namespace EcuParser
