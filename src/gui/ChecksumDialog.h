#ifndef CHECKSUMDIALOG_H
#define CHECKSUMDIALOG_H

#include "../core/Checksum.h"

#include <QDialog>

class QTableWidget;
class QLabel;
class QPushButton;

namespace EcuParser {

class BinFile;
class MainWindow;

// Modal verify/repair UI. Layout:
//
//   header: "Bin: <path>  Schema: <id>  Profile: <name>"
//   table : one row per ChecksumRange
//          | desc | start..end | storeOffset | algo | stored | computed | ok? |
//   buttons: [Detect candidates] [Repair (dry run)] [Repair & write] [Close]
//
// Repair is undoable - it pushes a BulkRegionCommand that snapshots
// every storeOffset region before writing.
class ChecksumDialog : public QDialog
{
    Q_OBJECT
public:
    ChecksumDialog(MainWindow *win,
                   BinFile *bin,
                   const QString &binPath,
                   const QString &schemaId,
                   QWidget *parent = nullptr);

private slots:
    void onDryRun();
    void onRepair();
    void onDetect();
    void refreshTable();

private:
    void buildUi();

    MainWindow *m_win;
    BinFile    *m_bin;
    QString     m_binPath;
    QString     m_schemaId;
    ChecksumProfile m_profile;

    QLabel       *m_header   = nullptr;
    QTableWidget *m_table    = nullptr;
    QLabel       *m_log      = nullptr;
    QPushButton  *m_dryBtn   = nullptr;
    QPushButton  *m_repairBtn= nullptr;
    QPushButton  *m_detectBtn= nullptr;
};

} // namespace EcuParser

#endif // CHECKSUMDIALOG_H
