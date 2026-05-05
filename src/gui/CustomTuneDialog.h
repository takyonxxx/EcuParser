#ifndef CUSTOMTUNEDIALOG_H
#define CUSTOMTUNEDIALOG_H

#include "../core/StagePackage.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

namespace EcuParser {

struct DriverModel;
class BinFile;
class MainWindow;

// Interactive stage builder. The user picks a map from the loaded
// driver, sets pct/window/cap, hits Add - one StageEdit appears as a
// row in the table. They iterate until satisfied, then either Apply
// (writes to modified bin via undoable BulkRegionCommand) or Save (to
// data/stages/<name>.json so it joins Stage1/Stage2 in the picker).
//
// Layout:
//
//   [Stage name: ____]   [Description: ____]
//
//   Edits table:
//     | Map | pct% | rowMin | rowMax | colMin | colMax | maxValue | comment |
//
//   [Add row] [Remove row] [Move up] [Move down]
//
//   --- Live preview (updated on every change) ---
//   total cells: N    clipped: N    decreased: N
//
//   [Save as JSON...] [Apply to MOD] [Cancel]
//
// All numeric fields accept -1 / blank to mean "no limit" (matching the
// JSON semantics). Comment column is freeform.
class CustomTuneDialog : public QDialog
{
    Q_OBJECT
public:
    CustomTuneDialog(MainWindow *win,
                     const DriverModel *driver,
                     const BinFile *origBin,
                     QWidget *parent = nullptr);

    // After exec() returns Accepted, the caller can fetch the
    // user-built stage via this method and feed it to applyStage().
    // Returns an empty package if the dialog was cancelled or if no
    // edits were configured.
    StagePackage resultPackage() const { return m_resultPackage; }

private slots:
    void onAddRow();
    void onRemoveRow();
    void onMoveUp();
    void onMoveDown();
    void onCellChanged(int row, int column);
    void onSaveJson();
    void onApply();
    void recomputePreview();

private:
    void buildUi();
    StagePackage buildPackage() const;
    void writeRow(int row, const StageEdit &e);
    StageEdit readRow(int row) const;
    void swapRows(int a, int b);

    MainWindow         *m_win;
    const DriverModel  *m_driver;
    const BinFile      *m_origBin;

    QLineEdit          *m_nameEdit  = nullptr;
    QLineEdit          *m_descEdit  = nullptr;
    QTableWidget       *m_table     = nullptr;
    QPlainTextEdit     *m_previewLog= nullptr;
    QPushButton        *m_applyBtn  = nullptr;
    QPushButton        *m_saveBtn   = nullptr;

    // List of canonical map names available for selection. Filled from
    // the driver in buildUi(); used to populate the per-row map combo.
    QStringList         m_mapNames;

    // Populated when the user clicks Apply. The parent reads this via
    // resultPackage() after exec() returns Accepted, then routes the
    // edits through MainWindow's existing apply-stage path so the same
    // BulkRegionCommand undo accounting kicks in.
    StagePackage        m_resultPackage;

    // Recursion guard for cellChanged. We mutate cells inside slot
    // handlers (e.g. when adding a row we set initial values), and we
    // don't want each setItem to trigger another preview recompute.
    bool m_suppressCellChanged = false;
};

} // namespace EcuParser

#endif // CUSTOMTUNEDIALOG_H
