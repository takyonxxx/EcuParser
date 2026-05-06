#ifndef CHECKSUM_H
#define CHECKSUM_H

// EDC15C 28F0_100 checksum handling.
//
// Bosch EDC15C calibrations for the Jeep WJ 2.7 CRD (OM612 5-cyl
// diesel) store THREE 4-byte calibration checksums, each protecting
// a distinct byte range. They are placed at fixed locations
// preceded by the marker bytes "C3 C3 C3 13 C8":
//
//   Block A: range 0x000000..0x013FFB, checksum @ 0x013FFC (4 byte)
//   Block B: range 0x014000..0x07BD7B, checksum @ 0x07BD7C (4 byte)
//   Block C: range 0x07BD80..0x07FCFB, checksum @ 0x07FCFC (4 byte)
//
// Almost all calibration map data lives in Block B; Block A holds
// boot/loader code and Block C holds the trailing module ID and
// hardware tables. Most tunes therefore only disturb Block B.
//
// CHECKSUM ALGORITHM: Bosch proprietary CRC. Despite extensive
// testing (additive sum byte/word/dword in BE+LE, two's complement,
// XOR, CRC16/32 with several polynomials including 0x1021, 0x8005,
// 0xA001, 0xC867, 0xCDF4, 0x8408, multi-block sum chains, sum +
// const offset across all plausible ranges) the algorithm has NOT
// been recovered from sample bin pairs. Public references confirm
// EDC15C checksum is only computable by commercial tools (WinOLS,
// ChkSuite, Galletto, ECM Titanium, MPPS, ecu.design corrector).
//
// IMMOBILIZER OBSERVATION: In our specific bin pair, modifying a
// block's bytes WITHOUT updating that block's checksum word causes
// the ECU to refuse start - the immobilizer light stays lit. This
// rules out the "soft validation" hypothesis: the ECU verifies the
// stored checksum against bytes-on-flash at every boot. Therefore
// keeping the stock checksum value when the bytes have changed is
// NOT safe and will brick the start.
//
// PRAGMATIC STRATEGY (no algorithm available): we work with up to
// two reference bins -
//   1. ORIGINAL bin (stock, pre-edit). Always required.
//   2. REFERENCE bin (a different known-good calibration with
//      either matching stock checksums OR a previously-tuned bin
//      whose checksum a commercial tool already recomputed
//      correctly). Optional; lets the user reuse a known-good
//      checksum value.
//
// At export time, for each block we consult both reference sources
// and decide:
//
//   - If the modified bytes inside the block are byte-identical to
//     the original: keep the original block's stored checksum.
//   - Otherwise, if the modified bytes inside the block are byte-
//     identical to the reference bin's bytes: copy the reference
//     bin's stored checksum word for that block. (The reference's
//     checksum was computed by a real tool and will be valid for
//     the matching bytes.)
//   - Otherwise: fail the export with a clear message telling the
//     user that the unique combination of edits requires a
//     commercial checksum corrector. No "best-effort" garbage value
//     is written - the user gets a refusal, not a brick.
//
// This prevents the failure mode that was reported (immobilizer
// light, no-start) by structurally rejecting any export that would
// produce a bin with bytes the ECU cannot verify.

#include "BinFile.h"

#include <QList>
#include <QString>

namespace EcuParser {

// One protected calibration block: contiguous data range plus a
// 4-byte checksum word at storeOffset.
//
// algorithm field describes the ON-DISK ENCODING of the stored word
// (so we read/write/display it correctly). It does NOT imply we
// know how to compute a value matching that encoding from the data
// range - we don't. For 28F0_100 all three blocks store the
// checksum as 4 bytes treated as Sum32BE for display purposes; the
// underlying CRC polynomial is unrecovered.
//
// Operationally we treat every block as "Preserve or copy from
// reference" - see ChecksumStrategy below.
enum class ChecksumAlgorithm {
    Sum32BE,       // 4-byte word, big-endian display
    Sum32LE,       // 4-byte word, little-endian display
    Sum16BE,       // 2-byte word, big-endian
    XorBytes,      // 1-byte XOR
    OpaqueCrc32,   // 4 bytes; algorithm proprietary, never compute,
                   // only preserve-or-copy. This is what every
                   // 28F0_100 block uses.
};

// Strategy outcome decided per block at save time.
enum class ChecksumStrategy {
    KeepOriginal,        // Block bytes unchanged vs original -> keep
                         // the original stored checksum.
    CopyFromReference,   // Block bytes match reference bin exactly
                         // -> copy the reference's stored checksum.
    Unresolvable,        // Block bytes differ from BOTH original
                         // and reference -> we cannot produce a
                         // valid checksum. Export must refuse.
};

struct ChecksumRange {
    quint32 startOffset = 0;       // first byte protected (inclusive)
    quint32 endOffset   = 0;       // last byte protected (inclusive)
    quint32 storeOffset = 0;       // where the 4-byte word lives
    quint8  storeLength = 4;       // size of the stored word
    ChecksumAlgorithm algorithm = ChecksumAlgorithm::OpaqueCrc32;
    QString description;
};

struct ChecksumProfile {
    QString schemaId;              // e.g. "28F0_100"
    QString name;
    QList<ChecksumRange> ranges;   // one per block (3 for 28F0_100)
};

// Per-block status as evaluated against the in-memory bin.
struct ChecksumBlockStatus {
    QString  description;
    quint32  startOffset = 0;
    quint32  endOffset   = 0;
    quint32  storeOffset = 0;
    bool     dataMatchesOriginal  = false;  // block bytes = original?
    bool     dataMatchesReference = false;  // block bytes = reference?
    bool     hasReference         = false;
    quint32  storedValue   = 0;             // currently in modBin
    quint32  originalValue = 0;             // in original bin
    quint32  referenceValue = 0;            // in reference bin (if any)
    ChecksumStrategy strategy = ChecksumStrategy::Unresolvable;
};

struct ChecksumStatus {
    QList<ChecksumBlockStatus> blocks;
    QStringList warnings;
    bool allResolvable() const {
        for (const auto &b : blocks)
            if (b.strategy == ChecksumStrategy::Unresolvable) return false;
        return !blocks.isEmpty();
    }
};

class Checksum
{
public:
    // Look up the bundled profile for a schema. Returns empty profile
    // when the schema is not supported.
    static ChecksumProfile profileForSchema(const QString &schemaId);

    // Read the currently-stored 4-byte word at r.storeOffset using the
    // algorithm's display encoding.
    static quint32 readStored(const BinFile &bin, const ChecksumRange &r);

    // Write a 4-byte word at r.storeOffset using the algorithm's
    // display encoding.
    static void writeStored(BinFile *bin, const ChecksumRange &r, quint32 value);

    // Compare a slice [start..endIncl] across two raw byte arrays;
    // true if every byte in the range matches.
    static bool blockBytesMatch(const QByteArray &a, const QByteArray &b,
                                quint32 start, quint32 endIncl);

    // Evaluate every block of the profile against the modified bin,
    // using the original bin (always) and an optional reference bin.
    // Returns one ChecksumBlockStatus per block, with a strategy
    // already chosen.
    static ChecksumStatus evaluate(const BinFile &modBin,
                                   const BinFile &origBin,
                                   const BinFile *refBin,
                                   const ChecksumProfile &p);

    // Apply the strategies decided by evaluate() to the modified bin
    // in-place. Writes original-or-reference checksum words into the
    // store offsets so the saved bin matches the ECU's expectation.
    // Returns true only if every block resolved to KeepOriginal or
    // CopyFromReference - i.e. the bin is now safe to flash. If any
    // block was Unresolvable, returns false and writes nothing for
    // that block (the in-memory bin is left in a state the user can
    // either continue editing to match a reference or send to a
    // commercial corrector).
    //
    // `log` (optional) collects per-block diagnostic lines.
    static bool applyStrategies(BinFile *modBin,
                                const BinFile &origBin,
                                const BinFile *refBin,
                                const ChecksumProfile &p,
                                QStringList *log = nullptr);
};

// ECM-Titanium-style analytic metrics computed across an arbitrary
// byte range. None of these match the actual Bosch CRC stored at
// the block boundaries - they are general-purpose summaries
// commercial tools (ECM Titanium 1.61, WinOLS) display side-by-side
// with the original bin to give the user a quick "did this byte
// change?" signal. We compute the same set so the user can copy
// the values into a third-party corrector that takes them as input.
//
// All 11 metrics have been validated against ECM Titanium 1.61
// output for the 293-822 stock bin and the 293-822_stage1.5_egr_off
// tuned bin (perfect match for both).
struct PartialMetrics {
    quint32 startOffset = 0;
    quint32 endOffset   = 0;
    quint16 checksum16  = 0;   // sum of bytes & 0xFFFF
    quint16 complement16 = 0;  // ~checksum16 & 0xFFFF
    quint16 even16      = 0;   // sum of bytes at even indices, low 16
    quint16 odd16       = 0;   // sum of bytes at odd indices, low 16
    quint32 dword32     = 0;   // sum of bytes (full 32-bit)
    quint32 sumWordLE   = 0;   // 16bit LH (sum of u16 LE words)
    quint32 sumWordBE   = 0;   // 16bit HL (sum of u16 BE words)
    quint32 sumDwordBE  = 0;   // 32 bit #1: sum of u32 BE dwords
    quint32 sumDwordSwapHL = 0; // 32 bit #2: sum of dwords with byte
                                // permutation (1,0,3,2) - ECM tag
    quint32 sumDwordLE  = 0;   // 32 bit #3: sum of u32 LE dwords
    quint32 sumDwordSwapWord = 0; // 32 bit #4: sum of dwords with byte
                                  // permutation (2,3,0,1) - ECM tag
};

// Compute PartialMetrics over [start..endIncl]. Used by the GUI's
// "Partial Checksum" panel and by --partial-metrics in CLI mode.
// Both are diagnostic only: they do not feed into export safety.
PartialMetrics computePartialMetrics(const BinFile &bin,
                                     quint32 startOffset,
                                     quint32 endOffsetIncl);

} // namespace EcuParser

#endif // CHECKSUM_H
