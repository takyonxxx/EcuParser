#ifndef AXISDEFINITION_H
#define AXISDEFINITION_H

#include <QString>
#include <cstdint>

namespace EcuParser {

// Axis descriptor as it appears in a .drt map record.
//
// In the file an axis is a comma-separated 4-tuple, e.g. "G,P,0,076F0E"
//   group   - 'G' (group/breakpoint table) or 'C' (constant / no axis)
//   kind    - 'P' (parameter / lookup table), 'D' (data / inline), 'C' (constant)
//   formula - integer 0..11, the reference "Tipo" (RPM = ... formula). Tipo 1 = fixed value.
//   address - hex offset of the breakpoint table inside the bin (or 000000 if none)
//
// We keep the raw fields verbatim and only interpret them when needed; this
// matches reference's storage and avoids losing information from drivers we
// haven't fully decoded yet.
struct AxisDefinition {
    char     group   = 'C';   // 'G' or 'C'
    char     kind    = 'C';   // 'P', 'D', 'C'
    int      formula = 0;     // 0..11
    quint32  address = 0;     // bin offset of breakpoint table
    QString  parameter;       // optional units/parameter name (XDF only)

    bool isPresent() const { return (group == 'G' && address != 0) || address != 0; }

    // Parse from the raw field string ("G,P,0,076F0E").
    // Returns false if the field doesn't have 4 comma-separated parts.
    static bool parse(const QString &field, AxisDefinition &out, QString *errorOut = nullptr);

    // For debugging.
    QString toDebugString() const;
};

} // namespace EcuParser

#endif // AXISDEFINITION_H
