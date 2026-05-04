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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = f.errorString();
        return false;
    }
    const qint64 written = f.write(m_data);
    if (written != m_data.size()) {
        if (errorOut) *errorOut = QStringLiteral("short write (%1/%2)")
                                      .arg(written).arg(m_data.size());
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

} // namespace EcuParser
