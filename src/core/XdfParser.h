#ifndef XDFPARSER_H
#define XDFPARSER_H

// Parser for the TunerPro XDF (Definition File) format. XDF is XML-based,
// open, and widely used in the open-source ECU tuning community - this
// gives EcuParser a path to ANY ECU that has an XDF definition published
// without needing per-schema reverse engineering.
//
// We deliberately implement a subset:
//   - <XDFFORMAT> + <XDFHEADER> with a description string
//   - <CATEGORY index="..." name="..."/> for top-level grouping
//   - <XDFTABLE> with <title>, <description>, <CATEGORYMEM>
//   - One <XDFAXIS id="z"> per table carrying the cell <EMBEDDEDDATA>:
//       mmedaddress     - byte offset into the bin
//       mmedelementsizebits - usually 16 (we accept 8/16/32)
//       mmedrowcount, mmedcolcount - dimensions
//   - X and Y axes are read for axis breakpoint addresses if present
//   - Endianness from <DEFAULTS lsbfirst="1"> at header level (default LE)
//
// Things we do not (yet) implement:
//   - <MATH equation="..."> evaluation - we treat cells as raw u16/s16
//   - <XDFCONSTANT> (single-value definitions)
//   - <XDFFLAG> (bit flags)
//   - per-table flags overriding global lsbfirst/signed
//
// The output is a DriverModel - same data structure DrtParser produces,
// so the rest of the app (tree, table, graph, stage application) works
// transparently regardless of which definition format was loaded.

#include "../model/DriverModel.h"
#include <QString>
#include <optional>

namespace EcuParser {

class XdfParser
{
public:
    // Parse an XDF file from disk. Returns nullopt and fills *errorOut
    // on failure (file missing, malformed XML, no tables found).
    static std::optional<DriverModel> parseFile(const QString &path,
                                                QString *errorOut = nullptr);
};

} // namespace EcuParser

#endif // XDFPARSER_H
