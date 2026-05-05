#include "UndoCommands.h"
#include "MainWindow.h"

namespace EcuParser {

CellEditCommand::CellEditCommand(MainWindow *win,
                                 quint32 offset,
                                 quint16 oldValue,
                                 quint16 newValue,
                                 const QString &mapName,
                                 int row, int col,
                                 int instance)
    : m_win(win),
      m_offset(offset),
      m_oldValue(oldValue),
      m_newValue(newValue),
      m_mapName(mapName),
      m_row(row),
      m_col(col),
      m_instance(instance)
{
    setText(QStringLiteral("Edit %1 [%2,%3] %4 -> %5")
                .arg(mapName.isEmpty() ? QStringLiteral("cell") : mapName)
                .arg(row).arg(col)
                .arg(oldValue).arg(newValue));
}

void CellEditCommand::undo()
{
    if (m_win) {
        m_win->undoRedoWriteU16LE(m_offset, m_oldValue);
        m_win->undoRedoRefresh();
    }
}

void CellEditCommand::redo()
{
    if (m_win) {
        m_win->undoRedoWriteU16LE(m_offset, m_newValue);
        m_win->undoRedoRefresh();
    }
}

bool CellEditCommand::mergeWith(const QUndoCommand *other)
{
    // Coalesce edits to the same offset within the same map+instance.
    // Useful when the user types multiple digits into a cell - each
    // QLineEdit commit produces an itemChanged, but the user thinks of
    // it as one edit. We only merge same-offset commands so distinct
    // cells stay separate undo steps.
    if (other->id() != id()) return false;
    auto *o = static_cast<const CellEditCommand*>(other);
    if (o->m_offset != m_offset) return false;
    if (o->m_instance != m_instance) return false;
    // Adopt the later "newValue" but keep our original "oldValue".
    m_newValue = o->m_newValue;
    setText(QStringLiteral("Edit %1 [%2,%3] %4 -> %5")
                .arg(m_mapName.isEmpty() ? QStringLiteral("cell") : m_mapName)
                .arg(m_row).arg(m_col)
                .arg(m_oldValue).arg(m_newValue));
    return true;
}

BulkRegionCommand::BulkRegionCommand(MainWindow *win,
                                     quint32 offset,
                                     const QByteArray &oldBytes,
                                     const QByteArray &newBytes,
                                     const QString &description)
    : m_win(win),
      m_offset(offset),
      m_oldBytes(oldBytes),
      m_newBytes(newBytes)
{
    setText(description);
}

void BulkRegionCommand::undo()
{
    if (m_win && !m_oldBytes.isEmpty()) {
        m_win->undoRedoWriteBytes(m_offset, m_oldBytes);
        m_win->undoRedoRefresh();
    }
}

void BulkRegionCommand::redo()
{
    if (m_win && !m_newBytes.isEmpty()) {
        m_win->undoRedoWriteBytes(m_offset, m_newBytes);
        m_win->undoRedoRefresh();
    }
}

} // namespace EcuParser
