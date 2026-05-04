#ifndef BINFILE_H
#define BINFILE_H

#include <QString>
#include <QByteArray>
#include <cstdint>

namespace Titanium {

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

private:
    QByteArray m_data;

    bool checkRange(quint32 offset, qsizetype length, bool *ok) const;
};

} // namespace Titanium

#endif // BINFILE_H
