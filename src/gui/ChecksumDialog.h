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

// Modal verify/preview UI for the new strategy-based checksum flow.
//
// Layout:
//
//   header: "Bin: <path>  Schema: <id>"
//           "Reference bin: (none|<path>)   [Load reference...]"
//   table : one row per ChecksumProfile range (3 for 28F0_100)
//          | block | bytes vs orig | bytes vs ref | strategy | stored | will-write |
//   buttons: [Apply now] [Close]
//
// "Apply now" runs Checksum::applyStrategies on a copy of the
// modified bin and shows the per-block decisions. The dialog never
// touches the on-disk file - the user must still go through Export
// to persist. This is intentional: the dialog is read-only in
// terms of long-lived state, so an accidental click can't brick
// anything.
//
// The dialog also surfaces the diagnostic ECM-Titanium-style metrics
// (byte sum, even/odd sums, word/dword sums LE+BE) for whichever
// block the user clicks on. These are PURELY informational - none
// of them is the actual ECU CRC, which is not implementable. The
// metrics help when the user needs to enter values into a third-
// party checksum corrector that exposes them as inputs.
class ChecksumDialog : public QDialog
{
    Q_OBJECT
public:
    ChecksumDialog(MainWindow *win,
                   BinFile *modBin,
                   const BinFile *origBin,
                   const BinFile *refBin,
                   const QString &binPath,
                   const QString &schemaId,
                   QWidget *parent = nullptr);

private slots:
    void onLoadReferenceBin();
    void onApply();
    void refreshTable();

private:
    void buildUi();
    QString strategyText(ChecksumStrategy s) const;

    MainWindow *m_win = nullptr;
    BinFile          *m_modBin  = nullptr;
    const BinFile    *m_origBin = nullptr;
    const BinFile    *m_refBin  = nullptr;
    QString     m_binPath;
    QString     m_schemaId;
    ChecksumProfile m_profile;

    QLabel       *m_header   = nullptr;
    QLabel       *m_refLabel = nullptr;
    QTableWidget *m_table    = nullptr;
    QLabel       *m_log      = nullptr;
    QPushButton  *m_applyBtn = nullptr;
    QPushButton  *m_loadRefBtn = nullptr;
};

} // namespace EcuParser

#endif // CHECKSUMDIALOG_H
