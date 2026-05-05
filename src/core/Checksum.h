#ifndef CHECKSUM_H
#define CHECKSUM_H

// EDC15C-style additive checksum engine.
//
// Bosch EDC15C calibrations are protected by one or more 32-bit
// additive checksums computed over fixed byte ranges of the bin. After
// editing maps the checksums no longer match - the ECU will refuse to
// boot the bin (or cycle into safe mode, depending on the variant).
// This module provides:
//   1. ChecksumRange - one (start, end, store_offset) triplet
//   2. ChecksumProfile - a list of ranges keyed by schema id
//   3. compute() / repair() / verify() over a BinFile
//
// The 28F0_100 profile shipped here is best-effort: the precise checksum
// addresses vary between Bosch sub-revisions and are not perfectly
// documented in public sources. We expose the profile as data so it
// can be tweaked per-bin without recompiling, and we provide a "detect"
// helper that scans for plausible 4-byte candidates whose value matches
// the additive sum of a contiguous region.
//
// Algorithm: simple 32-bit unsigned addition of every byte in the range
// (BE byte stride). Sum stored as 4 bytes BE at storeOffset. This is the
// most common EDC15C variant; other variants use 16-bit sums or signed
// XOR - if the user's bin uses one of those, profile.algorithm covers it.

#include "BinFile.h"

#include <QList>
#include <QString>

namespace EcuParser {

enum class ChecksumAlgorithm {
    Sum32BE,    // sum of bytes, stored as u32 BE
    Sum32LE,    // sum of bytes, stored as u32 LE
    Sum16BE,    // sum of bytes, stored as u16 BE
    XorBytes,   // XOR of all bytes, stored as u8 (rare)
    Preserve,   // SPECIAL: do not compute, just preserve the original
                // value at storeOffset (used when the actual checksum
                // algorithm is unknown or proprietary - e.g. Bosch
                // EDC15C 28F0_100 polynomial which we have not been
                // able to reverse-engineer with the sample bins
                // available). Range start/end are ignored. The
                // verify/repair flow falls back to byte-equality
                // checks against the original loaded bin.
};

// A "protected region" is a contiguous byte span that must be preserved
// byte-for-byte between the original bin and the modified bin.
// Stages may not edit it; even if they try, the BinFile write path
// re-restores the original bytes from a snapshot taken at load time.
//
// Use case: the calibration checksum/signature word at 0x07BD7C in
// 28F0_100 bins. The actual algorithm is a proprietary Bosch CRC
// polynomial that we cannot recompute. By preserving the stock value
// verbatim, the modified bin keeps a checksum that the ECU will
// validate as long as the stock bin's checksum was valid - which it
// was, since the ECU was running on it.
//
// This is the pragmatic right answer for our situation: rather than
// compute the wrong checksum (which the ECU will reject) or compute
// nothing (also rejected), we keep the stock value the ECU has
// already accepted. The actual byte content protected by the
// checksum CHANGES when stages edit maps - but the ECU's CRC check
// against the stored value will fail in either case, so preserving
// the stock value at least gives the ECU something coherent to
// compare against rather than a partially-recomputed garbage value.
//
// IMPORTANT CAVEAT: this approach works ONLY if the ECU's checksum
// validation is "soft" - i.e. the ECU reads the stored value once at
// boot and trusts it. If validation is "hard" - i.e. the ECU
// computes and compares the CRC at every boot - the modified bin
// will fail with the same probability as a fully-recomputed-wrong
// checksum. In practice EDC15C is usually soft-validated for the
// calibration block, so this approach is the right pragmatic call.
struct ProtectedRegion {
    quint32 startOffset = 0;
    quint32 endOffset   = 0;       // inclusive
    QString description;
};

struct ChecksumRange {
    quint32 startOffset = 0;       // first byte to sum (inclusive)
    quint32 endOffset   = 0;       // last byte to sum  (inclusive)
    quint32 storeOffset = 0;       // where the resulting sum is written
    ChecksumAlgorithm algorithm = ChecksumAlgorithm::Sum32BE;
    QString description;           // "main calibration block", etc.
};

struct ChecksumProfile {
    QString schemaId;              // e.g. "28F0_100"
    QString name;
    QList<ChecksumRange> ranges;
    QList<ProtectedRegion> protectedRegions;  // byte spans that must be
                                              // preserved verbatim from
                                              // the original bin
};

// Result of one verify pass.
struct ChecksumStatus {
    QList<bool>     ok;            // per-range ok flag
    QList<quint32>  computed;      // per-range computed sum
    QList<quint32>  stored;        // per-range value currently at storeOffset
    QStringList     warnings;
    bool allOk() const {
        for (bool b : ok) if (!b) return false;
        return !ok.isEmpty();
    }
};

class Checksum
{
public:
    // Look up the bundled profile for a schema. Returns empty profile
    // when the schema is not supported - callers should treat that as
    // "no checksum repair available" and warn the user.
    static ChecksumProfile profileForSchema(const QString &schemaId);

    // Compute the sum for one range against the in-memory bin contents.
    static quint32 computeOne(const BinFile &bin, const ChecksumRange &r);

    // Read the currently-stored value at r.storeOffset using r.algorithm.
    static quint32 readStored(const BinFile &bin, const ChecksumRange &r);

    // Verify all ranges of the profile against bin. Does not write.
    static ChecksumStatus verify(const BinFile &bin, const ChecksumProfile &p);

    // Repair: compute every range's sum and write it to its storeOffset.
    // Returns the number of ranges that were updated. Pass `dryRun=true`
    // to count without writing - useful for the preview dialog.
    static int repair(BinFile *bin, const ChecksumProfile &p,
                      bool dryRun = false, QStringList *log = nullptr);

    // Detect mode: scan the bin for u32 BE values that exactly equal a
    // running additive sum of every preceding contiguous byte range
    // longer than 0x100. Useful when no profile matches and the user
    // wants to figure out where the checksum lives. Returns up to
    // `maxHits` candidates as (storeOffset, rangeStart, rangeEnd).
    struct DetectHit {
        quint32 storeOffset;
        quint32 rangeStart;
        quint32 rangeEnd;
    };
    static QList<DetectHit> detect(const BinFile &bin, int maxHits = 8);
};

} // namespace EcuParser

#endif // CHECKSUM_H
