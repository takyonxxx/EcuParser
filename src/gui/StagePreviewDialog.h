#ifndef STAGEPREVIEWDIALOG_H
#define STAGEPREVIEWDIALOG_H

#include "../core/StagePackage.h"

#include <QDialog>

class QTableWidget;
class QLabel;

namespace EcuParser {

// Modal dry-run preview shown after the user picks a stage and ticks
// any options. Renders one row per StageEditPreview entry with:
//   map name | pct | window cells | changed | clipped (bug flag) | mean delta
//
// The "clipped" column is the headline value: a stage whose maxValue
// caps below the source map's existing maximum will silently clip a
// large fraction of cells, producing far less change than the comment
// promises. We highlight that row in red so the user sees the problem
// before confirming the apply.
class StagePreviewDialog : public QDialog
{
    Q_OBJECT
public:
    StagePreviewDialog(const StagePackage &pkg,
                       const StagePreview &preview,
                       const QString      &origPath,
                       const QString      &modPath,
                       QWidget *parent = nullptr);

private:
    void buildUi(const StagePackage &pkg,
                 const StagePreview &preview,
                 const QString &origPath,
                 const QString &modPath);

    QTableWidget *m_table = nullptr;
    QLabel       *m_summary = nullptr;
};

} // namespace EcuParser

#endif // STAGEPREVIEWDIALOG_H
