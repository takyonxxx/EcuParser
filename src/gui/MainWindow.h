#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "../core/BinFile.h"
#include "../model/DriverModel.h"

#include <QMainWindow>
#include <memory>

class QComboBox;
class QLabel;
class QTabWidget;
class QPushButton;
class QAction;
class QUndoStack;
class QUndoCommand;

namespace EcuParser {

class DriverTreeWidget;
class MapTableWidget;
class MapGraphWidget;

// Dual-bin map browser with table & graph views.
//
// Layout:
//
// +-------------------------------------------------------------------+
// | Driver: [combo] [..]   Original: [combo] [..]   Modified: [combo] |
// |                                                       [Copy ORI]  |
// |                                                       [Export...] |
// +------------------+------------------------------------------------+
// | DriverTree       | [Table | Graph] tabs                           |
// | (categories,     |                                                |
// |  maps, instances)|   Map view (changes when tree click happens)   |
// |                  |                                                |
// +------------------+------------------------------------------------+
//                                                       [status bar]
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onDriverComboChanged(int index);
    void onOriginalBinComboChanged(int index);
    void onModifiedBinComboChanged(int index);
    void onMapSelected(const MapDefinition *map, int addressIndex);
    void onCopyOriginalToModified();
    void onExportModifiedBin();
    void onApplyStage();
    void onVerifyChecksum();
    void onCustomTuneEditor();
    void onShowTuneLog();
    void onCellEdited(const MapDefinition *map, int instanceIndex,
                      int row, int col, qint32 newValue);
    void onBulkEditBegin();
    void onBulkEditEnd();
    void onBrowseDriver();
    void onBrowseOriginalBin();
    void onBrowseModifiedBin();

private:
    void buildUi();
    void populateDataCombos();
    bool loadDriver(const QString &path);
    bool loadOriginalBin(const QString &path);
    bool loadModifiedBin(const QString &path);
    void refreshTitle();
    void refreshCurrentMap();
    // Push the original bin's bytes for the schema's protected
    // regions into the modified bin's snapshot list. Called whenever
    // either bin or the driver changes. The actual restore happens
    // automatically inside BinFile::saveFile() at write time. See
    // BinFile::setProtectedSnapshots() for the design rationale.
    void refreshProtectedSnapshots();

public:
    // Public hooks for QUndoCommand subclasses (CellEditCommand,
    // BulkRegionCommand). They must reach into the modified bin and the
    // refresh path; making them friend-classes is messier than exposing
    // a tight, narrow interface here. Each call writes directly into
    // m_modBin and refreshes the table+graph+tree highlights, mirroring
    // what onCellEdited() / onApplyStage() do for the forward path.
    bool undoRedoWriteU16LE(quint32 offset, quint16 value);
    bool undoRedoWriteBytes(quint32 offset, const QByteArray &bytes);
    void undoRedoRefresh();

    // Push a pre-built QUndoCommand onto the window's undo stack. Used
    // by dialogs that compute an edit themselves (checksum repair,
    // smoothing, custom-tune apply) and want it to be undoable. The
    // command's redo() runs immediately as a side effect of push().
    // No-op when m_undoStack is null.
    void pushUndoCommand(class QUndoCommand *cmd);

    // Refresh the Apply Stage button's enabled state and tooltip based
    // on the currently loaded driver and original bin. The button is
    // only useful when:
    //   - a driver is loaded (so we know the schema id)
    //   - the driver schema is one we ship stages for (currently only
    //     28F0_100 - the WJ 2.7 CRD OM612 EDC15C calibration)
    //   - an original bin is loaded (stages need a stock baseline)
    // When any of these are missing the button is disabled and its
    // tooltip explains why. Called from loadDriver() and
    // loadOriginalBin() right after the relevant state changes.
    void refreshApplyStageButton();

    // Toolbar widgets
    QComboBox  *m_driverCombo = nullptr;
    QComboBox  *m_origBinCombo = nullptr;
    QComboBox  *m_modBinCombo  = nullptr;
    QPushButton *m_copyOriBtn = nullptr;
    QPushButton *m_exportBtn  = nullptr;
    QPushButton *m_applyStageBtn = nullptr;

    // Central widgets
    QLabel           *m_summaryLabel = nullptr;
    DriverTreeWidget *m_tree         = nullptr;
    QTabWidget       *m_tabs         = nullptr;
    MapTableWidget   *m_tableView    = nullptr;
    MapGraphWidget   *m_graphView    = nullptr;
    class DiffViewWidget *m_diffView = nullptr;
    class Surface3DWidget *m_surfaceView = nullptr;

    // Owned data.
    std::unique_ptr<DriverModel> m_driver;
    std::unique_ptr<BinFile>     m_origBin;
    std::unique_ptr<BinFile>     m_modBin;

    // Path of the currently loaded modified bin (used as default for
    // Save As).
    QString m_modBinPath;

    // True if user edits or Copy ORI changed m_modBin since load.
    bool m_modDirty = false;

    // True between MapTableWidget::bulkEditBegin and bulkEditEnd. While
    // set, onCellEdited writes to the bin without triggering a display
    // refresh - the refresh happens once at bulkEditEnd. This is what
    // keeps a multi-cell "Set value..." from invalidating the very
    // QTableWidgetItem pointers it's iterating over.
    bool m_bulkEditInProgress = false;

    // Undo stack. Single per-window stack; cleared when a new modified
    // bin is loaded (the bytes change underneath, so old commands no
    // longer make sense). Edit history survives Apply Stage because
    // stage applies push a single BulkRegionCommand that captures the
    // whole-bin before/after, so undo gets you back to pre-stage state
    // in one Ctrl+Z.
    QUndoStack *m_undoStack = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;

    // True only while a QUndoCommand is replaying. Suppresses the
    // re-push of new commands from inside onCellEdited, otherwise undo
    // would pile commands on the stack instead of unwinding.
    bool m_replayingUndo = false;
};

} // namespace EcuParser

#endif // MAINWINDOW_H
