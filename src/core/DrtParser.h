#ifndef DRTPARSER_H
#define DRTPARSER_H

#include "../model/DriverModel.h"

#include <QString>
#include <QByteArray>
#include <optional>

namespace Titanium {

// Parses ECM Titanium .drt driver files.
//
// The format is ASCII-only, byte-delimited:
//   0x84 - record separator
//   0xBB - field separator within a record
//
// Both delimiters are "high ASCII" Latin-1 punctuation that doesn't appear
// in any of the legitimate field values (which are all hex digits, decimal
// digits, single uppercase letters or commas), so simple split parsing is
// safe.
class DrtParser
{
public:
    // Parse from a file path. Returns std::nullopt and fills errorOut on failure.
    static std::optional<DriverModel> parseFile(const QString &path,
                                                QString *errorOut = nullptr);

    // Parse from raw bytes. Useful for testing and for ZIP-hosted drivers
    // that we may want to load in-memory later.
    static std::optional<DriverModel> parseBytes(const QByteArray &bytes,
                                                 QString *errorOut = nullptr);

    // Byte values used as delimiters. Exposed for tests.
    static constexpr char kRecordSep = '\x84';
    static constexpr char kFieldSep  = '\xBB';
};

} // namespace Titanium

#endif // DRTPARSER_H
