#include "BinFile.h"

#include <QFile>
#include <QDebug>

namespace EcuParser {

BinFile::BinFile() = default;

BinFile::BinFile(const QByteArray &data) : m_data(data) {}

bool BinFile::loadFile(const QString &path, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    m_data = f.readAll();
    return !m_data.isEmpty();
}

bool BinFile::saveFile(const QString &path, QString *errorOut) const
{
    // Apply protected-region snapshots just before writing. The
    // saveFile() method is const (callers expect it not to mutate the
    // in-memory state), so we make a temporary buffer here, apply the
    // restores to that buffer, and write it. The in-memory m_data is
    // not changed - this matters because the user might continue
    // editing after a save, and we don't want save-time restores to
    // suddenly "undo" their pending edits in the protected regions
    // (even though those edits are doomed to be restored at the next
    // save anyway - the in-memory representation matches the on-disk
    // representation only AFTER applyProtectedSnapshots() is called).
    QByteArray buf = m_data;
    int restored = 0;
    for (const ProtectedSnapshot &snap : m_protectedSnapshots) {
        if (snap.bytes.isEmpty()) continue;
        if (qsizetype(snap.startOffset) + snap.bytes.size() > buf.size()) {
            qWarning("BinFile::saveFile: protected snapshot out of "
                     "range, skipping (start=0x%08X len=%lld bin=%lld)",
                     snap.startOffset, qlonglong(snap.bytes.size()),
                     qlonglong(buf.size()));
            continue;
        }
        // Replace verbatim with the snapshot bytes.
        buf.replace(qsizetype(snap.startOffset), snap.bytes.size(), snap.bytes);
        ++restored;
    }
    if (restored > 0) {
        qInfo("BinFile::saveFile: restored %d protected region(s) "
              "before writing %s", restored, qUtf8Printable(path));
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    const qint64 written = f.write(buf);
    if (written != buf.size()) {
        if (errorOut) *errorOut = QStringLiteral("short write (%1/%2)")
                                      .arg(written).arg(buf.size());
        return false;
    }
    return true;
}

bool BinFile::checkRange(quint32 offset, qsizetype length, bool *ok) const
{
    if (offset > quint32(std::numeric_limits<qsizetype>::max())
        || qsizetype(offset) + length > m_data.size()) {
        if (ok) *ok = false;
        else qWarning("BinFile: read OOR at 0x%08X len=%lld size=%lld",
                      offset, qlonglong(length), qlonglong(m_data.size()));
        return false;
    }
    if (ok) *ok = true;
    return true;
}

quint8 BinFile::readU8(quint32 offset, bool *ok) const
{
    if (!checkRange(offset, 1, ok)) return 0;
    return quint8(m_data.at(qsizetype(offset)));
}

quint16 BinFile::readU16BE(quint32 offset, bool *ok) const
{
    if (!checkRange(offset, 2, ok)) return 0;
    const quint8 a = quint8(m_data.at(qsizetype(offset)));
    const quint8 b = quint8(m_data.at(qsizetype(offset + 1)));
    return quint16((quint16(a) << 8) | quint16(b));
}

quint16 BinFile::readU16LE(quint32 offset, bool *ok) const
{
    if (!checkRange(offset, 2, ok)) return 0;
    const quint8 a = quint8(m_data.at(qsizetype(offset)));
    const quint8 b = quint8(m_data.at(qsizetype(offset + 1)));
    return quint16((quint16(b) << 8) | quint16(a));
}

qint16 BinFile::readS16BE(quint32 offset, bool *ok) const
{
    return qint16(readU16BE(offset, ok));
}

quint32 BinFile::readU32BE(quint32 offset, bool *ok) const
{
    if (!checkRange(offset, 4, ok)) return 0;
    const quint8 a = quint8(m_data.at(qsizetype(offset + 0)));
    const quint8 b = quint8(m_data.at(qsizetype(offset + 1)));
    const quint8 c = quint8(m_data.at(qsizetype(offset + 2)));
    const quint8 d = quint8(m_data.at(qsizetype(offset + 3)));
    return (quint32(a) << 24) | (quint32(b) << 16) | (quint32(c) << 8) | quint32(d);
}

QByteArray BinFile::readBytes(quint32 offset, qsizetype length) const
{
    bool ok = false;
    if (!checkRange(offset, length, &ok))
        return QByteArray();
    return m_data.mid(qsizetype(offset), length);
}

bool BinFile::writeU16BE(quint32 offset, quint16 value)
{
    bool ok = false;
    if (!checkRange(offset, 2, &ok))
        return false;
    m_data[qsizetype(offset)]     = char(quint8(value >> 8));
    m_data[qsizetype(offset + 1)] = char(quint8(value & 0xFF));
    return true;
}

bool BinFile::writeU16LE(quint32 offset, quint16 value)
{
    bool ok = false;
    if (!checkRange(offset, 2, &ok))
        return false;
    m_data[qsizetype(offset)]     = char(quint8(value & 0xFF));
    m_data[qsizetype(offset + 1)] = char(quint8(value >> 8));
    return true;
}

bool BinFile::writeBytes(quint32 offset, const QByteArray &bytes)
{
    bool ok = false;
    if (!checkRange(offset, bytes.size(), &ok))
        return false;
    for (qsizetype i = 0; i < bytes.size(); ++i)
        m_data[qsizetype(offset) + i] = bytes.at(i);
    return true;
}

QString BinFile::detectSchema() const
{
    // We support several bin sizes for different ECU families:
    //   - 512 KiB : Bosch EDC15C (28F0_100), GM PCM 0411 small variant
    //   - 1 MiB   : GM E40 PCM (1MB main, used in 2003-2005 GTO etc.)
    //   - 1.25 MiB: GM E40 PCM with bolt-on slave chip (extra 256 KiB)
    //
    // Reject anything else early.
    const qsizetype sz = m_data.size();
    if (sz != 524288 && sz != 1048576 && sz != 1310720)
        return QString();

    auto u16le = [&](quint32 off) -> int {
        if (qsizetype(off) + 2 > m_data.size()) return -1;
        const quint8 a = quint8(m_data.at(qsizetype(off)));
        const quint8 b = quint8(m_data.at(qsizetype(off + 1)));
        return int(quint16(a) | (quint16(b) << 8));
    };
    auto u32be = [&](quint32 off) -> quint32 {
        if (qsizetype(off) + 4 > m_data.size()) return 0;
        const quint8 a = quint8(m_data.at(qsizetype(off    )));
        const quint8 b = quint8(m_data.at(qsizetype(off + 1)));
        const quint8 c = quint8(m_data.at(qsizetype(off + 2)));
        const quint8 d = quint8(m_data.at(qsizetype(off + 3)));
        return (quint32(a) << 24) | (quint32(b) << 16)
             | (quint32(c) << 8)  |  quint32(d);
    };
    // Read an 8-char ASCII OS-id from the given offset (used for both
    // GM 0411 and GM E40 - their OS numbers are stored as 8-digit
    // ASCII strings inside an "OS ID" map). Returns the parsed integer
    // or 0 if the bytes don't form an 8-digit decimal string.
    auto readAsciiOsId = [&](quint32 off) -> quint32 {
        if (qsizetype(off) + 8 > m_data.size()) return 0;
        quint32 v = 0;
        for (int i = 0; i < 8; ++i) {
            const quint8 c = quint8(m_data.at(qsizetype(off) + i));
            if (c < '0' || c > '9') return 0;
            v = v * 10 + (c - '0');
        }
        return v;
    };

    // === GM E40 PCM detection (1 MiB or 1.25 MiB with slave chip) ===
    //
    // E40 is the PowerPC-based PCM used in 2003-2005 GM performance
    // cars (GTO, CTS-V, others). Calibration size is 1 MiB; some
    // tunes ship as 1.25 MiB to include a "slave chip" 256-KiB
    // overlay - the first 1 MiB is the main PCM image regardless.
    //
    // Signature:
    //   - Vector table at 0x0000-0x000C is PowerPC-style: starts with
    //     four zero bytes (initial NIA), then a non-zero entry-point
    //     address in the 0x00000400..0x000FFFFF range.
    //   - 8-character ASCII OS number at 0x1FE9E. This is the address
    //     of the "OS ID" map that ships in the Phoenix XDF
    //     (12598977_OS_V1_0_BETA.xdf) and in every other E40 XDF in
    //     circulation. Values like "12598977", "12586243", etc. - we
    //     accept any 8-digit decimal that starts with "12" (covers
    //     all known GM E40 OS numbers).
    if (sz == 1048576 || sz == 1310720) {
        const quint32 nia = u32be(0x0000);
        const quint32 entry = u32be(0x0004);
        const bool ppcVector = (nia == 0
                              && entry >= 0x00000400 && entry < 0x00100000);
        const quint32 osNum = readAsciiOsId(0x1FE9E);
        const bool osLooksOk = (osNum >= 12000000 && osNum < 13000000);
        if (ppcVector && osLooksOk) {
            return QStringLiteral("GM_E40_OS%1").arg(osNum);
        }
    }

    // === GM PCM 0411 detection (Motorola 68k-based, 512 KiB) ===
    //
    // 0411 is the Motorola 68332/68336 PCM used in 2002+ GM trucks/
    // SUVs (Vortec 4.8/5.3/6.0, LM4/LM7/LQ4 V8). Calibration size
    // is exactly 512 KiB.
    //
    // Signature:
    //   - Vector table at 0x0000-0x0007 follows Motorola 68k
    //     conventions: stack at 0x00FFXXXX (high word == 0x00FF), then
    //     reset PC at 0x000XXXXX (lower 1 MiB). Exact stack/PC
    //     addresses vary by OS revision.
    //   - OS number stored as 4-byte big-endian at offset 0x504. For
    //     this family OS values are 8-digit decimal in the range
    //     12200000..12700000.
    if (sz == 524288) {
        const quint8 b0 = quint8(m_data.at(0));
        const quint8 b1 = quint8(m_data.at(1));
        const bool stackHi = (b0 == 0x00 && b1 == 0xFF);
        const quint32 reset = u32be(0x0004);
        const bool resetLooksOk = (reset >= 0x00000400 && reset < 0x00080000);
        const quint32 osNum = u32be(0x0504);
        const bool osLooksOk = (osNum >= 12000000 && osNum < 13000000);
        if (stackHi && resetLooksOk && osLooksOk) {
            return QStringLiteral("GM_0411_OS%1").arg(osNum);
        }
    }

    // === 28F0_100 (Jeep WJ 2.7 CRD EDC15C, OM612) detection ===
    //
    // Signature:
    //   - File size exactly 512 KiB
    //   - injection at part throttle map at 0x076F52
    //   - rail pressure map at 0x07ADD2
    //   - turbo pressure map at 0x075EA0
    //   - torque limiter map at 0x076D82
    //
    // We don't trust a single cell value to match exactly across all
    // calibration revisions (293-822, 094-704, 409-438 differ), but
    // PLAUSIBLE RANGES at those addresses are tight enough to identify
    // the schema. We sample four known addresses and require at least
    // three to fall inside the schema's expected ranges.
    //
    // Reference values (from 293-822 stock):
    //   0x076F52 = 3600  (injection cell [0,0], usually 2400..6000)
    //   0x07ADD2 = ?     (rail pressure  [0,0], usually 2000..15000)
    //   0x075EA0 = ?     (turbo pressure [0,0], 1000..2300)
    //   0x076D82 = ?     (torque limiter row 0, 4000..5000)
    if (sz == 524288) {
        const int v_inj   = u16le(0x076F52);
        const int v_rail  = u16le(0x07ADD2);
        const int v_turbo = u16le(0x075EA0);
        const int v_tq    = u16le(0x076D82);

        auto in = [](int v, int lo, int hi) {
            return v >= lo && v <= hi;
        };

        int hits = 0;
        if (in(v_inj,   1500, 7500))  ++hits;
        if (in(v_rail,  1500, 16000)) ++hits;
        if (in(v_turbo, 800,  2400))  ++hits;
        if (in(v_tq,    3500, 5500))  ++hits;
        if (hits >= 3) return QStringLiteral("28F0_100");
    }

    return QString();
}

void BinFile::setProtectedSnapshots(
    const QByteArray &originalRaw,
    const QList<QPair<quint32, quint32>> &regions,
    const QStringList &descriptions)
{
    m_protectedSnapshots.clear();
    for (int i = 0; i < regions.size(); ++i) {
        const auto &reg = regions.at(i);
        const quint32 start = reg.first;
        const quint32 endIncl = reg.second;
        if (endIncl < start) {
            qWarning("BinFile::setProtectedSnapshots: invalid range 0x%08X..0x%08X",
                     start, endIncl);
            continue;
        }
        const qsizetype len = qsizetype(endIncl - start + 1);
        if (qsizetype(start) + len > originalRaw.size()) {
            qWarning("BinFile::setProtectedSnapshots: region 0x%08X..0x%08X "
                     "exceeds original bin size %lld - skipping",
                     start, endIncl, qlonglong(originalRaw.size()));
            continue;
        }
        ProtectedSnapshot snap;
        snap.startOffset = start;
        snap.bytes       = originalRaw.mid(qsizetype(start), len);
        snap.description = i < descriptions.size()
                               ? descriptions.at(i)
                               : QStringLiteral("region #%1").arg(i);
        m_protectedSnapshots.append(snap);
    }
    qInfo("BinFile: %d protected snapshot(s) registered",
          int(m_protectedSnapshots.size()));
}

void BinFile::clearProtectedSnapshots()
{
    m_protectedSnapshots.clear();
}

int BinFile::applyProtectedSnapshots()
{
    int restoredBytes = 0;
    for (const ProtectedSnapshot &snap : m_protectedSnapshots) {
        if (snap.bytes.isEmpty()) continue;
        if (qsizetype(snap.startOffset) + snap.bytes.size() > m_data.size()) {
            qWarning("BinFile::applyProtectedSnapshots: snapshot out of range");
            continue;
        }
        // Count bytes that actually differ before replace - this is the
        // useful return value (caller can tell if anything was disturbed).
        for (qsizetype i = 0; i < snap.bytes.size(); ++i) {
            if (m_data.at(qsizetype(snap.startOffset) + i) != snap.bytes.at(i))
                ++restoredBytes;
        }
        m_data.replace(qsizetype(snap.startOffset), snap.bytes.size(), snap.bytes);
    }
    return restoredBytes;
}

} // namespace EcuParser
