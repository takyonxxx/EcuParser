#ifndef UNDOCOMMANDS_H
#define UNDOCOMMANDS_H

// QUndoCommand subclasses for EcuParser bin edits.
//
// Granularity:
//   - CellEditCommand: single u16 cell write (interactive table edit)
//   - BulkRegionCommand: a single contiguous byte region written as one
//     undoable unit. Used by Copy ORI -> MOD (the whole map region) and
//     by stage apply (the whole bin reset + edits, batched as one).
//
// All commands hold the OLD bytes so undo can put them back verbatim.
// We never re-derive old values from the original bin: the original
// might have changed (user swapped Original combo) between command and
// undo, so storing bytes is the only sound thing.

#include "../core/BinFile.h"

#include <QByteArray>
#include <QString>
#include <QUndoCommand>
#include <cstdint>

namespace EcuParser {

class MainWindow;

// One u16 cell write. mergeWith() coalesces consecutive writes to the
// same cell so rapid keystrokes in the same QTableWidgetItem don't
// produce a 30-deep undo stack.
class CellEditCommand : public QUndoCommand
{
public:
    CellEditCommand(MainWindow *win,
                    quint32 offset,
                    quint16 oldValue,
                    quint16 newValue,
                    const QString &mapName,
                    int row, int col,
                    int instance);

    void undo() override;
    void redo() override;

    int  id() const override { return 0xC1; }
    bool mergeWith(const QUndoCommand *other) override;

    quint32 offset() const { return m_offset; }

private:
    MainWindow *m_win;
    quint32 m_offset;
    quint16 m_oldValue;
    quint16 m_newValue;
    QString m_mapName;
    int     m_row;
    int     m_col;
    int     m_instance;
};

// A contiguous byte region replaced wholesale. Holds the OLD bytes for
// undo and the NEW bytes for redo. Used both when the user clicks
// "Copy ORI -> MOD" (region = one map's bin span) and when applyStage
// runs (region = the entire 512KiB bin, since stage first resets MOD
// to a copy of ORI and then edits it - we capture before/after of the
// whole thing in one command).
class BulkRegionCommand : public QUndoCommand
{
public:
    BulkRegionCommand(MainWindow *win,
                      quint32 offset,
                      const QByteArray &oldBytes,
                      const QByteArray &newBytes,
                      const QString &description);

    void undo() override;
    void redo() override;

    int id() const override { return 0xC2; }
    // Bulk regions never merge - they are deliberate user operations
    // and we want each Apply Stage / Copy ORI to be a separate undo
    // step.
    bool mergeWith(const QUndoCommand *) override { return false; }

private:
    MainWindow *m_win;
    quint32    m_offset;
    QByteArray m_oldBytes;
    QByteArray m_newBytes;
};

} // namespace EcuParser

#endif // UNDOCOMMANDS_H
