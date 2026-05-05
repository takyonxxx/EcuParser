#include "StagePreviewDialog.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace EcuParser {

StagePreviewDialog::StagePreviewDialog(const StagePackage &pkg,
                                       const StagePreview &preview,
                                       const QString      &origPath,
                                       const QString      &modPath,
                                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Stage preview"));
    buildUi(pkg, preview, origPath, modPath);
}

void StagePreviewDialog::buildUi(const StagePackage &pkg,
                                 const StagePreview &preview,
                                 const QString &origPath,
                                 const QString &modPath)
{
    auto *vlay = new QVBoxLayout(this);

    auto *header = new QLabel(
        QStringLiteral("<b>Dry run: '%1'</b>").arg(pkg.name), this);
    vlay->addWidget(header);

    auto *paths = new QLabel(
        QStringLiteral("Source: %1<br>Target: %2<br>"
                       "<i>Nothing has been written yet.</i>")
            .arg(QFileInfo(origPath).fileName(),
                 QFileInfo(modPath).fileName()),
        this);
    paths->setWordWrap(true);
    paths->setStyleSheet(QStringLiteral("color: #555; margin-bottom: 6px;"));
    vlay->addWidget(paths);

    // === Per-edit table.
    m_table = new QTableWidget(this);
    const QStringList cols {
        QStringLiteral("Map"),
        QStringLiteral("Req %"),
        QStringLiteral("Cells"),
        QStringLiteral("Changed"),
        QStringLiteral("Clipped"),
        QStringLiteral("Decreased"),
        QStringLiteral("Mean Δ raw"),
        QStringLiteral("Effective %"),
        QStringLiteral("Note"),
    };
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setRowCount(preview.edits.size());
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);

    // Pinkish red tint for rows where the cap silently clipped most of
    // the request - this is the failure mode the dry-run is meant to
    // surface. Tooltip explains why on hover.
    const QColor warnTint(245, 215, 210);
    const QColor warnText(160,  20,  20);

    for (int i = 0; i < preview.edits.size(); ++i) {
        const auto &ep = preview.edits.at(i);
        const double effPctMean = (ep.cellsInWindow > 0 && ep.sumOldRaw > 0)
            ? (double(ep.sumNewRaw - ep.sumOldRaw) / double(ep.sumOldRaw)) * 100.0
            : 0.0;
        const double meanDelta = (ep.cellsInWindow > 0)
            ? double(ep.sumNewRaw - ep.sumOldRaw) / double(ep.cellsInWindow)
            : 0.0;

        // Heuristic for "this edit underdelivered": clip count > 30% of
        // window OR effective % differs from request by > 5 absolute
        // points. Both cases are user-visible bugs in stage JSON authoring.
        const bool suspicious =
            (ep.cellsInWindow > 0
             && (double(ep.clippedCount) / ep.cellsInWindow > 0.30
                 || std::abs(effPctMean - ep.pctRequested) > 5.0
                 || ep.decreasedCount > 0));

        auto setCell = [&](int col, const QString &txt,
                           Qt::Alignment align = Qt::AlignCenter) {
            auto *it = new QTableWidgetItem(txt);
            it->setTextAlignment(align);
            if (suspicious) {
                it->setBackground(QBrush(warnTint));
                it->setForeground(QBrush(warnText));
                QFont f = it->font();
                f.setBold(col == 4 || col == 7); // bold in Clipped + Eff%
                it->setFont(f);
            }
            m_table->setItem(i, col, it);
        };

        setCell(0, ep.mapName, Qt::AlignLeft | Qt::AlignVCenter);
        setCell(1, QStringLiteral("%1%").arg(QString::number(ep.pctRequested, 'g', 3)));
        setCell(2, QString::number(ep.cellsInWindow));
        setCell(3, QString::number(ep.changedCount));
        setCell(4, QString::number(ep.clippedCount));
        setCell(5, QString::number(ep.decreasedCount));
        setCell(6, QString::number(meanDelta, 'f', 1));
        setCell(7, QStringLiteral("%1%").arg(QString::number(effPctMean, 'f', 1)));
        setCell(8, ep.comment, Qt::AlignLeft | Qt::AlignVCenter);

        if (suspicious) {
            // Tooltip on the whole row: explain what went wrong so the
            // stage author can fix the JSON cap.
            QString hint;
            if (ep.decreasedCount > 0)
                hint = QStringLiteral(
                    "%1 cells DECREASED. The cap is below the source map's "
                    "existing values - this edit is removing content rather "
                    "than adding it. Raise max_value or remove it.")
                    .arg(ep.decreasedCount);
            else if (double(ep.clippedCount) / ep.cellsInWindow > 0.30)
                hint = QStringLiteral(
                    "%1/%2 cells clipped to max_value. Most of the requested "
                    "%3%% increase didn't land. Raise max_value to let the "
                    "scaling work.")
                    .arg(ep.clippedCount).arg(ep.cellsInWindow)
                    .arg(QString::number(ep.pctRequested, 'g', 3));
            else
                hint = QStringLiteral(
                    "Effective change (%1%%) deviates from requested (%2%%) "
                    "by more than 5 points - check max_value cap.")
                    .arg(QString::number(effPctMean, 'f', 1))
                    .arg(QString::number(ep.pctRequested, 'g', 3));
            for (int c = 0; c < m_table->columnCount(); ++c)
                if (auto *it = m_table->item(i, c)) it->setToolTip(hint);
        }
    }

    m_table->resizeColumnsToContents();
    m_table->setMinimumHeight(220);
    vlay->addWidget(m_table, 1);

    // === Summary line under table.
    QString warnLine;
    if (preview.totalCellsClipped > 0)
        warnLine += QStringLiteral("  ⚠ clipped: %1 cells")
                        .arg(preview.totalCellsClipped);
    if (preview.totalCellsDecreased > 0)
        warnLine += QStringLiteral("  ⚠ decreased: %1 cells")
                        .arg(preview.totalCellsDecreased);

    m_summary = new QLabel(
        QStringLiteral("<b>Total cells changed: %1</b>%2")
            .arg(preview.totalCellsTouched)
            .arg(warnLine), this);
    m_summary->setStyleSheet(QStringLiteral(
        "QLabel { padding: 6px; background: #F0F2F4; border: 1px solid #DDD; }"));
    vlay->addWidget(m_summary);

    if (!preview.warnings.isEmpty()) {
        auto *w = new QLabel(
            QStringLiteral("<b>Warnings</b><br>%1")
                .arg(preview.warnings.join(QStringLiteral("<br>"))),
            this);
        w->setWordWrap(true);
        w->setStyleSheet(QStringLiteral("color: #A02020; padding: 4px;"));
        vlay->addWidget(w);
    }

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply now"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancel"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    vlay->addWidget(btns);

    setMinimumSize(840, 480);
}

} // namespace EcuParser
