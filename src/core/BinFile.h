#ifndef BINFILE_H
#define BINFILE_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QPair>
#include <cstdint>

namespace EcuParser {

// Loads an ECU bin into memory and exposes typed reads.
//
// The Bosch EDC15C bins observed are 512 KiB, big-endian for 16-bit map
// cells. We default to big-endian but expose a flag to flip per-call for
// future ECUs. Writes are not implemented in Phase 1.
class BinFile
{
public:
    BinFile();
    explicit BinFile(const QByteArray &data);

    bool loadFile(const QString &path, QString *errorOut = nullptr);

    // Save the current bytes to disk. Returns false and fills errorOut on
    // failure. Used to export modified bins.
    bool saveFile(const QString &path, QString *errorOut = nullptr) const;

    qsizetype size() const { return m_data.size(); }
    bool isEmpty()   const { return m_data.isEmpty(); }
    const QByteArray &raw() const { return m_data; }

    // Range-checked typed reads. Out-of-range returns 0 and sets ok=false
    // when ok is non-null; otherwise it logs a qWarning and returns 0.
    quint8  readU8 (quint32 offset, bool *ok = nullptr) const;
    quint16 readU16BE(quint32 offset, bool *ok = nullptr) const;
    quint16 readU16LE(quint32 offset, bool *ok = nullptr) const;
    qint16  readS16BE(quint32 offset, bool *ok = nullptr) const;
    quint32 readU32BE(quint32 offset, bool *ok = nullptr) const;

    // Write a u16 big-endian value at offset. Returns false if out of range.
    bool writeU16BE(quint32 offset, quint16 value);

    // Write a u16 little-endian value at offset. Returns false if OOR.
    // EDC15C cell writes go through this (cells are stored LE in the bin).
    bool writeU16LE(quint32 offset, quint16 value);

    // Bulk replace a contiguous region. Returns false if out of range.
    bool writeBytes(quint32 offset, const QByteArray &bytes);

    // Bulk read for a contiguous region. Returns empty QByteArray on error.
    QByteArray readBytes(quint32 offset, qsizetype length) const;

    // === Protected Region API ===
    //
    // A "protected region" is a byte span whose contents must remain
    // identical to a reference snapshot regardless of any subsequent
    // edits. This is how we deal with the EDC15C 28F0_100 calibration
    // checksum word at 0x07BD7C: rather than recompute (which we
    // can't, the polynomial is proprietary), we keep the stock value
    // from the original bin verbatim.
    //
    // Workflow:
    //   1. Caller loads the original (stock) bin into one BinFile.
    //   2. Caller copies it into the modified BinFile.
    //   3. Caller calls setProtectedSnapshot(orig.raw(), regions) on
    //      the modified BinFile. The BinFile now stores the byte
    //      values at those regions from the snapshot.
    //   4. Stages, custom tunes, etc. write to the modified BinFile.
    //      Some of those writes may accidentally hit a protected
    //      region. That's fine - the in-memory state can be wrong.
    //   5. At save time (saveFile()) the BinFile restores the
    //      protected bytes from the snapshot just before writing to
    //      disk. The on-disk file is therefore guaranteed to have
    //      stock values in the protected regions.
    //
    // This is a defense-in-depth pattern: the stage code SHOULD
    // never write to a protected region, but the BinFile guard
    // ensures saved bins are always correct even if the stage code
    // has a bug. It also makes it trivial to add new protected
    // regions later (just extend the list).
    struct ProtectedSnapshot {
        quint32    startOffset = 0;
        QByteArray bytes;          // copied from the stock bin at setup
        QString    description;
    };

    // Set the snapshot list. Pass the original (stock) bin's raw bytes
    // as `originalRaw`. The BinFile copies the relevant slices and
    // remembers them; later it restores them at save time.
    void setProtectedSnapshots(const QByteArray &originalRaw,
                               const QList<QPair<quint32, quint32>> &regions,
                               const QStringList &descriptions = {});

    // Forget all protected snapshots. Called when a fresh bin is
    // loaded so the previous bin's snapshots don't leak to the new
    // edit session.
    void clearProtectedSnapshots();

    // Read-only accessor for diagnostics / tests.
    const QList<ProtectedSnapshot> &protectedSnapshots() const
    { return m_protectedSnapshots; }

    // Apply the protected snapshots to the in-memory bytes RIGHT NOW
    // (without writing to disk). Returns the number of bytes that
    // were modified by the restore. Useful for the StagePreview and
    // Diff views, which want to show the user the bytes that will
    // actually be saved - including the post-restore protected
    // regions. Most callers don't need this; saveFile() runs it
    // automatically.
    int applyProtectedSnapshots();

    // Best-effort guess at the schema this bin matches. Returns the
    // schemaId string ("28F0_100" for Jeep WJ 2.7 CRD EDC15C bins) or
    // empty if no signature matches. Detection uses (file size, sample
    // values at known map addresses) tuples - a 512 KiB bin whose first
    // injection map cell at 0x076F52 reads 3600 LE is almost certainly
    // a 28F0_100 calibration. Cheap enough to call from the bin combo
    // change handler so we can auto-suggest the matching .drt.
    QString detectSchema() const;

private:
    QByteArray m_data;
    QList<ProtectedSnapshot> m_protectedSnapshots;

    bool checkRange(quint32 offset, qsizetype length, bool *ok) const;
};

} // namespace EcuParser

#endif // BINFILE_H
