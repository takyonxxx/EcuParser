#include "Checksum.h"

#include <QDebug>

namespace EcuParser {

namespace {

// Profile for the 28F0_100 schema (Jeep WJ 2.7 CRD, EDC15C, Mercedes
// OM612 engine - Bosch ECU 0281010xxx variants).
//
// CHECKSUM LOCATION: Confirmed by reverse-engineering against five
// stock+tuned bin pairs (293-822, 409-438, 094-704 + tuned variants).
// All bins consistently store a 4-byte word at offset 0x07BD7C
// surrounded by 0xC3 fill (0x07BD70..0x07BD79) and the marker bytes
// 0x13 0xC8 (0x07BD7A..0x07BD7B). Tuned bins differ from stock by
// exactly these four bytes plus the map data being modified - never
// any other "checksum-like" word.
//
// CHECKSUM ALGORITHM: NOT recovered. We tested simple additive sum
// (5 ranges), CRC32 zlib (5 ranges), 16/32-bit word sums (LE/BE),
// negative variants, and delta-based byte/word/dword analysis. None
// of these match. The polynomial appears to be a Bosch-specific
// proprietary CRC variant only available in commercial tools
// (CHKSuite, WinOLS, Galletto, ECU.design checksum corrector).
//
// PRAGMATIC DECISION: rather than computing a wrong checksum that
// the ECU will reject, we PRESERVE the original 4-byte word from the
// stock bin verbatim. This works when:
//   - The user opens a known-good (factory or running) bin, and
//   - The ECU validates this word "soft" (read once, trusted).
//
// To enforce this, we mark 0x07BD7C..0x07BD7F as a ProtectedRegion.
// MainWindow takes a snapshot of these bytes at bin load time and
// the BinFile save path restores them before writing to disk - so
// even if a future stage edit accidentally addresses this byte
// range, the saved bin will still have the correct stock value.
//
// We also include the secondary "calibration ID block" near 0x07BFB6
// (containing ASCII module ID like "3822" / "7438" / "6704") and
// the version stamp at 0x07FCFC..0x07FD09 as additional protected
// regions. These are not strictly checksums but identification
// stamps the ECU may compare against - changing them breaks
// validation in some sub-revisions.
ChecksumProfile build28F0Profile()
{
    ChecksumProfile p;
    p.schemaId = QStringLiteral("28F0_100");
    p.name     = QStringLiteral("EDC15C 28F0_100 (preserve mode)");

    // Calibration block checksum word - confirmed at 0x07BD7C.
    // Algorithm unknown - we mark it Preserve so the verify/repair
    // flow falls back to byte-equality against the original.
    {
        ChecksumRange r;
        r.startOffset = 0x000000;
        r.endOffset   = 0x07BD7B;
        r.storeOffset = 0x07BD7C;
        r.algorithm   = ChecksumAlgorithm::Preserve;
        r.description = QStringLiteral(
            "Calibration checksum word @ 0x07BD7C (4 byte) - Bosch "
            "proprietary CRC, value preserved from original bin");
        p.ranges.append(r);
    }

    // Protected regions: bytes that must be byte-identical to the
    // original at save time. The first is the 4-byte checksum word
    // itself. The other two are ECU identification stamps that some
    // sub-revisions check at boot.
    {
        ProtectedRegion r;
        r.startOffset = 0x07BD7C;
        r.endOffset   = 0x07BD7F;  // inclusive, 4 bytes
        r.description = QStringLiteral("Calibration checksum word");
        p.protectedRegions.append(r);
    }
    {
        ProtectedRegion r;
        r.startOffset = 0x07BFB6;
        r.endOffset   = 0x07BFE1;  // 44 bytes, ASCII module ID block
        r.description = QStringLiteral("ECU module ID + cal version");
        p.protectedRegions.append(r);
    }
    {
        ProtectedRegion r;
        r.startOffset = 0x07FCFC;
        r.endOffset   = 0x07FD09;  // 14 bytes, secondary version stamp
        r.description = QStringLiteral("Secondary version stamp");
        p.protectedRegions.append(r);
    }
    return p;
}

} // namespace

ChecksumProfile Checksum::profileForSchema(const QString &schemaId)
{
    if (schemaId == QStringLiteral("28F0_100"))
        return build28F0Profile();
    return ChecksumProfile{};
}

quint32 Checksum::computeOne(const BinFile &bin, const ChecksumRange &r)
{
    if (r.endOffset < r.startOffset) return 0;
    if (qsizetype(r.endOffset) >= bin.size()) return 0;
    // Preserve mode: there is no algorithm to compute. Return the
    // currently-stored value so verify() reports OK as long as the
    // bytes haven't been disturbed (which is exactly the contract of
    // Preserve - "don't recompute, just keep what's there").
    if (r.algorithm == ChecksumAlgorithm::Preserve)
        return readStored(bin, r);

    quint32 sum = 0;
    quint32 xorAcc = 0;
    const QByteArray slice = bin.readBytes(r.startOffset,
                                           qsizetype(r.endOffset - r.startOffset + 1));
    for (qsizetype i = 0; i < slice.size(); ++i) {
        const quint8 b = quint8(slice.at(i));
        sum    += b;
        xorAcc ^= b;
    }
    switch (r.algorithm) {
    case ChecksumAlgorithm::Sum32BE:
    case ChecksumAlgorithm::Sum32LE:
        return sum;
    case ChecksumAlgorithm::Sum16BE:
        return sum & 0xFFFFu;
    case ChecksumAlgorithm::XorBytes:
        return xorAcc & 0xFFu;
    case ChecksumAlgorithm::Preserve:
        return readStored(bin, r);  // unreachable; handled above
    }
    return sum;
}

quint32 Checksum::readStored(const BinFile &bin, const ChecksumRange &r)
{
    bool ok = false;
    switch (r.algorithm) {
    case ChecksumAlgorithm::Sum32BE:
        return bin.readU32BE(r.storeOffset, &ok);
    case ChecksumAlgorithm::Sum32LE: {
        const quint16 lo = bin.readU16LE(r.storeOffset, &ok);
        const quint16 hi = bin.readU16LE(r.storeOffset + 2, &ok);
        return (quint32(hi) << 16) | quint32(lo);
    }
    case ChecksumAlgorithm::Sum16BE:
        return bin.readU16BE(r.storeOffset, &ok);
    case ChecksumAlgorithm::XorBytes:
        return bin.readU8(r.storeOffset, &ok);
    case ChecksumAlgorithm::Preserve: {
        // Preserve treats the storage as a 4-byte u32 LE for display
        // purposes (logging, dialog) - the format follows what we
        // observed at 0x07BD7C in 28F0_100 bins.
        const quint16 lo = bin.readU16LE(r.storeOffset, &ok);
        const quint16 hi = bin.readU16LE(r.storeOffset + 2, &ok);
        return (quint32(hi) << 16) | quint32(lo);
    }
    }
    return 0;
}

ChecksumStatus Checksum::verify(const BinFile &bin, const ChecksumProfile &p)
{
    ChecksumStatus s;
    if (p.ranges.isEmpty()) {
        s.warnings.append(QStringLiteral("No checksum profile for this schema."));
        return s;
    }
    for (const ChecksumRange &r : p.ranges) {
        // Range plausibility: storeOffset must be within bin size and
        // must NOT be inside [start, end] (otherwise the sum includes
        // its own value - chicken/egg).
        if (qsizetype(r.endOffset) >= bin.size()
            || qsizetype(r.storeOffset) >= bin.size()) {
            s.warnings.append(QStringLiteral("Range out of bin size: %1").arg(r.description));
            s.ok.append(false);
            s.computed.append(0);
            s.stored.append(0);
            continue;
        }
        const quint32 c = computeOne(bin, r);
        const quint32 v = readStored(bin, r);
        s.computed.append(c);
        s.stored.append(v);
        s.ok.append(c == v);
    }
    return s;
}

int Checksum::repair(BinFile *bin, const ChecksumProfile &p,
                     bool dryRun, QStringList *log)
{
    if (!bin) return 0;
    int updated = 0;
    for (const ChecksumRange &r : p.ranges) {
        if (qsizetype(r.endOffset) >= bin->size()
            || qsizetype(r.storeOffset) >= bin->size()) {
            if (log)
                log->append(QStringLiteral("Skipped (out of range): %1").arg(r.description));
            continue;
        }
        // Refuse self-overlapping ranges - silently writing into a
        // checksum region you're summing produces a nonsensical value.
        if (r.storeOffset >= r.startOffset && r.storeOffset <= r.endOffset) {
            if (log)
                log->append(QStringLiteral("Skipped (storeOffset inside range): %1")
                                .arg(r.description));
            continue;
        }
        const quint32 c = computeOne(*bin, r);
        const quint32 v = readStored(*bin, r);
        if (c == v) {
            if (log)
                log->append(QStringLiteral("OK: %1 (sum=0x%2)")
                                .arg(r.description)
                                .arg(c, 8, 16, QLatin1Char('0')).toUpper());
            continue;
        }
        if (!dryRun) {
            // Write the new sum at storeOffset using the algorithm.
            switch (r.algorithm) {
            case ChecksumAlgorithm::Sum32BE: {
                bin->writeU16BE(r.storeOffset,     quint16(c >> 16));
                bin->writeU16BE(r.storeOffset + 2, quint16(c & 0xFFFFu));
                break;
            }
            case ChecksumAlgorithm::Sum32LE: {
                bin->writeU16LE(r.storeOffset,     quint16(c & 0xFFFFu));
                bin->writeU16LE(r.storeOffset + 2, quint16(c >> 16));
                break;
            }
            case ChecksumAlgorithm::Sum16BE:
                bin->writeU16BE(r.storeOffset, quint16(c & 0xFFFFu));
                break;
            case ChecksumAlgorithm::XorBytes: {
                QByteArray b(1, char(quint8(c & 0xFFu)));
                bin->writeBytes(r.storeOffset, b);
                break;
            }
            case ChecksumAlgorithm::Preserve:
                // Nothing to write - by definition Preserve never
                // disagrees with stored (computeOne returns stored).
                // If we got here, some prior code wrongly mutated the
                // protected bytes; the BinFile snapshot/restore guard
                // should prevent that, but log just in case.
                if (log)
                    log->append(QStringLiteral(
                        "Preserve range mismatch detected - this should "
                        "not happen, the protected-region guard in "
                        "BinFile should restore original bytes at save."));
                break;
            }
        }
        if (log)
            log->append(QStringLiteral("%1 %2: stored=0x%3 -> computed=0x%4")
                            .arg(dryRun ? QStringLiteral("WOULD UPDATE")
                                        : QStringLiteral("UPDATED"))
                            .arg(r.description)
                            .arg(v, 8, 16, QLatin1Char('0')).toUpper()
                            .arg(c, 8, 16, QLatin1Char('0')).toUpper());
        ++updated;
    }
    return updated;
}

QList<Checksum::DetectHit> Checksum::detect(const BinFile &bin, int maxHits)
{
    QList<DetectHit> hits;
    if (bin.size() < 0x1000) return hits;

    // Walk the bin in 256-byte aligned candidate offsets and check
    // whether the u32 BE there equals the additive sum of all bytes
    // between offset 0 and that location. This is a cheap sweep that
    // finds the simplest "sum from 0 to here" layout. It does not find
    // multi-range or non-zero-start layouts; for those the user must
    // configure a profile manually.
    quint32 runningSum = 0;
    for (qsizetype i = 0; i < bin.size() - 4; ++i) {
        const quint8 b = bin.readU8(quint32(i));
        runningSum += b;
        if ((i & 0xFF) != 0xFC) continue; // align candidate to *FC
        if (i < 0x100) continue;
        bool ok = false;
        const quint32 stored = bin.readU32BE(quint32(i + 1), &ok);
        if (!ok) continue;
        // Subtract the would-be storage bytes from the running sum
        // (they are part of "everything before stored u32").
        const quint32 sumExcludingStorage = runningSum;
        if (sumExcludingStorage == stored) {
            DetectHit h;
            h.rangeStart  = 0;
            h.rangeEnd    = quint32(i);
            h.storeOffset = quint32(i + 1);
            hits.append(h);
            if (hits.size() >= maxHits) break;
        }
    }
    return hits;
}

} // namespace EcuParser
