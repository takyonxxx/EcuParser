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

    // Toolbar widgets
    QComboBox  *m_driverCombo = nullptr;
    QComboBox  *m_origBinCombo = nullptr;
    QComboBox  *m_modBinCombo  = nullptr;
    QPushButton *m_copyOriBtn = nullptr;
    QPushButton *m_exportBtn  = nullptr;

    // Central widgets
    QLabel           *m_summaryLabel = nullptr;
    DriverTreeWidget *m_tree         = nullptr;
    QTabWidget       *m_tabs         = nullptr;
    MapTableWidget   *m_tableView    = nullptr;
    MapGraphWidget   *m_graphView    = nullptr;

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
};

} // namespace EcuParser

#endif // MAINWINDOW_H
