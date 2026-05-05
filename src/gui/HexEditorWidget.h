#ifndef HEXEDITORWIDGET_H
#define HEXEDITORWIDGET_H

// HexEditorWidget — ECM Titanium-style raw hex editor over a BinFile.
//
// Sits as a tab in MainWindow next to Table / Graph / 3D / Diff. Reads
// from the Original combo (mounted via setBins()) and writes through to
// the Modified BinFile owned by MainWindow, so any change here is
// immediately picked up by Export Modified Bin and the rest of the
// pipeline.
//
// Features:
//   - Address column (6-hex digits) | 16 hex bytes | ASCII gutter
//   - Inline edit on hex side (over-typing), inline edit on ASCII side
//   - Red highlight for bytes that differ from the Original bin
//   - Go-to-address (Ctrl+G), Find (Ctrl+F) - hex pattern only
//   - Selection start/end labels echoing ECM Titanium's "Selection" panel
//   - Status bar: current offset, byte value (hex/dec/ASCII), bin size
//
// All edits are routed through MainWindow's QUndoStack via
// BulkRegionCommand, so Ctrl+Z/Ctrl+Y interoperate with the table view's
// edit history.

#include <QWidget>
#include <QByteArray>
#include <cstdint>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QShowEvent;

namespace EcuParser {

class BinFile;
class MainWindow;

class HexEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HexEditorWidget(QWidget *parent = nullptr);

    // MainWindow handle so we can push QUndoCommand objects onto the
    // shared undo stack. Pass nullptr to disable undo integration (the
    // widget will still work, edits just won't be on the stack).
    void setMainWindow(MainWindow *win) { m_win = win; }

    // Mount the two BinFiles. Both pointers are non-owning and may be
    // null. Original is used for diff highlighting; Modified is the
    // edit target. Call refresh() afterwards (or rely on the implicit
    // call done here).
    void setBins(BinFile *original, BinFile *modified);

    // Called by MainWindow whenever the modified bin's bytes change
    // outside of this widget (Apply Stage, Copy ORI, table edit, undo
    // replay, etc.) so the visible cells stay in sync.
    void refresh();

    // Re-evaluate diff highlights only. Lighter than refresh() because
    // it doesn't re-fill cell text.
    void refreshDiffOnly();

    // Programmatically scroll to and select a byte. Used by Go-to and
    // Find. Out-of-range offsets are clamped.
    void gotoOffset(quint32 offset);

protected:
    void showEvent(QShowEvent *ev) override;

private slots:
    void onCellChanged(int row, int col);
    void onCellSelectionChanged();
    void onGotoTriggered();
    void onFindTriggered();
    void onFindNext();

private:
    void buildUi();
    void rebuildTable();
    void updateRowDiff(int row);
    void updateStatusForOffset(quint32 offset);

    // Writes a single byte to the modified bin, routed through the undo
    // stack when possible. Returns true on success.
    bool writeByteUndoable(quint32 offset, quint8 newValue);

    // Convert a (row, col) on the hex side to a byte offset, or -1 if
    // the cell is the address column or ASCII gutter.
    qint64 cellToOffsetHex(int row, int col) const;
    // Same for the ASCII gutter side.
    qint64 cellToOffsetAscii(int row, int col) const;

    // Convert a byte offset to the (row, hexCol) pair, or (-1, -1) if
    // out of range.
    void offsetToHexCell(quint32 offset, int *row, int *col) const;

    // Recolor one cell on the hex side (row, hexCol) and the matching
    // ASCII cell based on whether the modified byte differs from the
    // original.
    void recolorByte(int row, int hexCol);

    // Run a hex-pattern search starting from `startOffset`. Returns -1
    // if not found. Pattern is a parsed byte sequence (each entry is
    // 0..255 or -1 for "?" wildcard).
    qint64 findPattern(qint64 startOffset,
                       const QList<int> &pattern) const;

    // Parse "DE AD ?? BE EF" or "DEAD??BEEF" into a byte/wildcard list.
    // Returns empty list and sets *err on parse failure.
    static QList<int> parseHexPattern(const QString &input, QString *err);

private:
    MainWindow *m_win        = nullptr;
    BinFile    *m_orig       = nullptr;
    BinFile    *m_mod        = nullptr;

    QTableWidget *m_table    = nullptr;
    QLabel       *m_addrLbl  = nullptr;
    QLabel       *m_valueLbl = nullptr;
    QLabel       *m_sizeLbl  = nullptr;
    QLabel       *m_selLbl   = nullptr;
    QLineEdit    *m_findInput = nullptr;
    QPushButton  *m_findNextBtn = nullptr;

    // Last-used find pattern, kept across Find Next presses.
    QList<int> m_lastPattern;
    qint64     m_lastFoundOffset = -1;

    // Re-entrancy guard so programmatic table updates don't fire
    // onCellChanged() and try to undo-push themselves as user edits.
    bool m_suppressCellSignals = false;

    // Set when setBins() / refresh() is called while the widget is not
    // visible. We avoid building the 32k-row table until the user
    // actually opens the Hex tab. Cleared by showEvent().
    bool m_pendingRebuild = false;
};

} // namespace EcuParser

#endif // HEXEDITORWIDGET_H
