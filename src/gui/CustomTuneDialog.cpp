#include "CustomTuneDialog.h"
#include "AppPaths.h"
#include "MainWindow.h"
#include "UndoCommands.h"
#include "../core/BinFile.h"
#include "../core/StagePackage.h"
#include "../model/DriverModel.h"
#include "../model/DriverNames.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace EcuParser {

namespace {

enum Col {
    ColMap     = 0,
    ColPct     = 1,
    ColRowMin  = 2,
    ColRowMax  = 3,
    ColColMin  = 4,
    ColColMax  = 5,
    ColMaxVal  = 6,
    ColComment = 7,
    ColCount   = 8,
};

// Parse "" or "-1" to -1, otherwise the integer. Used for the four
// optional row/col bounds and the maxValue cap. We treat any non-numeric
// string as -1 too (cell is "blank"), so users can clear a constraint
// by deleting its text.
int parseOptInt(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty()) return -1;
    bool ok = false;
    const int v = t.toInt(&ok);
    if (!ok) return -1;
    return v;
}

QString fmtOptInt(int v)
{
    if (v < 0) return QString();
    return QString::number(v);
}

} // namespace

CustomTuneDialog::CustomTuneDialog(MainWindow *win,
                                   const DriverModel *driver,
                                   const BinFile *origBin,
                                   QWidget *parent)
    : QDialog(parent),
      m_win(win),
      m_driver(driver),
      m_origBin(origBin)
{
    setWindowTitle(QStringLiteral("Custom tune editor"));
    if (driver) {
        // Build the canonical name list once. Used by the map combo in
        // every row. We use canonical names (DriverNames::displayName)
        // because that's what StagePackage::applyStage matches against.
        QSet<QString> seen;
        for (const MapDefinition &m : driver->maps) {
            const QString n = DriverNames::displayName(driver->schemaId, m);
            if (!n.isEmpty() && !seen.contains(n)) {
                m_mapNames.append(n);
                seen.insert(n);
            }
        }
    }
    buildUi();
    // Start with one blank row so the user has something to edit
    // immediately - empty tables are intimidating UX.
    onAddRow();
}

void CustomTuneDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    // Stage name + description
    auto *form = new QFormLayout();
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setText(QStringLiteral("My tune"));
    m_descEdit = new QLineEdit(this);
    m_descEdit->setPlaceholderText(QStringLiteral("Optional - what this tune does"));
    form->addRow(QStringLiteral("Stage name:"), m_nameEdit);
    form->addRow(QStringLiteral("Description:"), m_descEdit);
    vlay->addLayout(form);

    // Edits table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("Map")
        << QStringLiteral("pct %")
        << QStringLiteral("rowMin")
        << QStringLiteral("rowMax")
        << QStringLiteral("colMin")
        << QStringLiteral("colMax")
        << QStringLiteral("maxValue")
        << QStringLiteral("Comment"));
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(180);
    vlay->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("Add edit"), this);
    auto *delBtn = new QPushButton(QStringLiteral("Remove"), this);
    auto *upBtn  = new QPushButton(QStringLiteral("Move up"), this);
    auto *dnBtn  = new QPushButton(QStringLiteral("Move down"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    btnRow->addWidget(upBtn);
    btnRow->addWidget(dnBtn);
    vlay->addLayout(btnRow);

    // Live preview log
    auto *previewLabel = new QLabel(QStringLiteral("Preview (auto-updates):"), this);
    QFont f = previewLabel->font();
    f.setBold(true);
    previewLabel->setFont(f);
    vlay->addWidget(previewLabel);
    m_previewLog = new QPlainTextEdit(this);
    m_previewLog->setReadOnly(true);
    m_previewLog->setMinimumHeight(120);
    m_previewLog->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #FAFAFA; font-family: monospace; font-size: 11px; }"));
    vlay->addWidget(m_previewLog);

    // Footer actions
    auto *footer = new QHBoxLayout();
    m_saveBtn = new QPushButton(QStringLiteral("Save as JSON..."), this);
    footer->addWidget(m_saveBtn);
    footer->addStretch();
    auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_applyBtn = new QPushButton(QStringLiteral("Apply to MOD"), this);
    m_applyBtn->setDefault(true);
    footer->addWidget(cancelBtn);
    footer->addWidget(m_applyBtn);
    vlay->addLayout(footer);

    connect(addBtn,    &QPushButton::clicked, this, &CustomTuneDialog::onAddRow);
    connect(delBtn,    &QPushButton::clicked, this, &CustomTuneDialog::onRemoveRow);
    connect(upBtn,     &QPushButton::clicked, this, &CustomTuneDialog::onMoveUp);
    connect(dnBtn,     &QPushButton::clicked, this, &CustomTuneDialog::onMoveDown);
    connect(m_saveBtn, &QPushButton::clicked, this, &CustomTuneDialog::onSaveJson);
    connect(m_applyBtn,&QPushButton::clicked, this, &CustomTuneDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_table,   &QTableWidget::cellChanged,
            this,      &CustomTuneDialog::onCellChanged);

    setMinimumSize(900, 620);
}

void CustomTuneDialog::onAddRow()
{
    const int r = m_table->rowCount();
    m_suppressCellChanged = true;
    m_table->insertRow(r);

    // Map column gets a combo populated from the driver. We embed the
    // QComboBox via setCellWidget rather than QTableWidgetItem so the
    // user gets a dropdown instead of free-text typing - keeps map
    // names canonical.
    auto *combo = new QComboBox(this);
    combo->addItems(m_mapNames);
    m_table->setCellWidget(r, ColMap, combo);
    connect(combo, &QComboBox::currentTextChanged, this, [this]() {
        recomputePreview();
    });

    // Initial values: pct=10, no window constraints, no cap, blank comment.
    auto setInt = [&](int col, int v) {
        auto *it = new QTableWidgetItem(fmtOptInt(v));
        it->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, col, it);
    };
    auto setNum = [&](int col, double v) {
        auto *it = new QTableWidgetItem(QString::number(v, 'g', 4));
        it->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(r, col, it);
    };
    setNum(ColPct, 10.0);
    setInt(ColRowMin, -1);
    setInt(ColRowMax, -1);
    setInt(ColColMin, -1);
    setInt(ColColMax, -1);
    setInt(ColMaxVal, -1);
    auto *commentItem = new QTableWidgetItem(QString());
    m_table->setItem(r, ColComment, commentItem);

    m_table->resizeColumnsToContents();
    m_suppressCellChanged = false;
    m_table->selectRow(r);
    recomputePreview();
}

void CustomTuneDialog::onRemoveRow()
{
    const int r = m_table->currentRow();
    if (r < 0) return;
    m_table->removeRow(r);
    recomputePreview();
}

void CustomTuneDialog::swapRows(int a, int b)
{
    if (a < 0 || b < 0) return;
    if (a >= m_table->rowCount() || b >= m_table->rowCount()) return;
    if (a == b) return;
    // QTableWidget has no native row-swap; we read the StageEdit on
    // each side then re-write to the swapped row. The map combo widget
    // also needs swapping by reading its text and re-setting.
    const StageEdit ea = readRow(a);
    const StageEdit eb = readRow(b);
    m_suppressCellChanged = true;
    writeRow(a, eb);
    writeRow(b, ea);
    m_suppressCellChanged = false;
    m_table->selectRow(b);
    recomputePreview();
}

void CustomTuneDialog::onMoveUp()
{
    const int r = m_table->currentRow();
    if (r > 0) swapRows(r, r - 1);
}

void CustomTuneDialog::onMoveDown()
{
    const int r = m_table->currentRow();
    if (r >= 0 && r < m_table->rowCount() - 1) swapRows(r, r + 1);
}

void CustomTuneDialog::onCellChanged(int /*row*/, int /*column*/)
{
    if (m_suppressCellChanged) return;
    recomputePreview();
}

StageEdit CustomTuneDialog::readRow(int row) const
{
    StageEdit e;
    if (auto *combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColMap)))
        e.mapName = combo->currentText();
    auto cellText = [&](int c) -> QString {
        auto *it = m_table->item(row, c);
        return it ? it->text() : QString();
    };
    e.pctChange = cellText(ColPct).toDouble();
    e.rowMin    = parseOptInt(cellText(ColRowMin));
    e.rowMax    = parseOptInt(cellText(ColRowMax));
    e.colMin    = parseOptInt(cellText(ColColMin));
    e.colMax    = parseOptInt(cellText(ColColMax));
    e.maxValue  = parseOptInt(cellText(ColMaxVal));
    e.comment   = cellText(ColComment);
    return e;
}

void CustomTuneDialog::writeRow(int row, const StageEdit &e)
{
    if (auto *combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColMap))) {
        const int idx = combo->findText(e.mapName);
        if (idx >= 0) combo->setCurrentIndex(idx);
    } else {
        // Row never had a combo (shouldn't happen, but guard anyway).
        auto *c = new QComboBox(m_table);
        c->addItems(m_mapNames);
        const int idx = c->findText(e.mapName);
        if (idx >= 0) c->setCurrentIndex(idx);
        m_table->setCellWidget(row, ColMap, c);
    }
    auto setText = [&](int col, const QString &t) {
        auto *it = m_table->item(row, col);
        if (!it) {
            it = new QTableWidgetItem(t);
            it->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(row, col, it);
        } else {
            it->setText(t);
        }
    };
    setText(ColPct,    QString::number(e.pctChange, 'g', 4));
    setText(ColRowMin, fmtOptInt(e.rowMin));
    setText(ColRowMax, fmtOptInt(e.rowMax));
    setText(ColColMin, fmtOptInt(e.colMin));
    setText(ColColMax, fmtOptInt(e.colMax));
    setText(ColMaxVal, fmtOptInt(e.maxValue));
    setText(ColComment, e.comment);
}

StagePackage CustomTuneDialog::buildPackage() const
{
    StagePackage pkg;
    pkg.name        = m_nameEdit->text();
    pkg.description = m_descEdit->text();
    if (m_driver && !m_driver->schemaId.isEmpty())
        pkg.schemas.append(m_driver->schemaId);
    for (int r = 0; r < m_table->rowCount(); ++r) {
        StageEdit e = readRow(r);
        if (e.mapName.isEmpty()) continue;
        // Skip pct=0 with no other operation - it's a no-op row the user
        // didn't fill in, no point passing it to applyStage.
        if (e.pctChange == 0.0 && e.setValue < 0 && !e.setToMapMax)
            continue;
        pkg.edits.append(e);
    }
    return pkg;
}

void CustomTuneDialog::recomputePreview()
{
    if (!m_driver || !m_origBin) {
        m_previewLog->setPlainText(
            QStringLiteral("Need both a driver and an Original bin loaded."));
        return;
    }
    const StagePackage pkg = buildPackage();
    if (pkg.edits.isEmpty()) {
        m_previewLog->setPlainText(QStringLiteral("(no edits configured)"));
        return;
    }
    const StagePreview prev = previewStage(pkg, *m_driver, *m_origBin);
    QStringList lines;
    lines.append(QStringLiteral("Total cells changed: %1   clipped: %2   decreased: %3")
                     .arg(prev.totalCellsTouched)
                     .arg(prev.totalCellsClipped)
                     .arg(prev.totalCellsDecreased));
    lines.append(QString());
    for (const auto &ep : prev.edits) {
        const double effPct = (ep.cellsInWindow > 0 && ep.sumOldRaw > 0)
            ? (double(ep.sumNewRaw - ep.sumOldRaw) / double(ep.sumOldRaw)) * 100.0
            : 0.0;
        QString flag;
        if (ep.decreasedCount > 0)
            flag = QStringLiteral("  [WARN: %1 decreased]").arg(ep.decreasedCount);
        else if (ep.cellsInWindow > 0
                 && double(ep.clippedCount) / ep.cellsInWindow > 0.30)
            flag = QStringLiteral("  [WARN: %1/%2 clipped]")
                       .arg(ep.clippedCount).arg(ep.cellsInWindow);
        lines.append(QStringLiteral(
            "  %1   pct=%2%   cells=%3   changed=%4   eff=%5%%6")
                .arg(ep.mapName, -42)
                .arg(QString::number(ep.pctRequested, 'g', 3), 6)
                .arg(ep.cellsInWindow, 4)
                .arg(ep.changedCount, 4)
                .arg(QString::number(effPct, 'f', 1), 5)
                .arg(flag));
    }
    if (!prev.warnings.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("Warnings:"));
        for (const QString &w : prev.warnings)
            lines.append(QStringLiteral("  - %1").arg(w));
    }
    m_previewLog->setPlainText(lines.join(QLatin1Char('\n')));
}

void CustomTuneDialog::onSaveJson()
{
    StagePackage pkg = buildPackage();
    if (pkg.name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Save"),
            QStringLiteral("Set a stage name first."));
        return;
    }
    if (pkg.edits.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Save"),
            QStringLiteral("No edits configured. Add at least one row."));
        return;
    }
    // Default save location: data/stages/ next to the built-in stages.
    // Default filename: lowercased stage name with spaces -> underscores.
    QString suggestedDir = AppPaths::dataDir();
    if (!suggestedDir.isEmpty())
        suggestedDir += QStringLiteral("/stages");
    QString suggestedFile = pkg.name.toLower();
    suggestedFile.replace(QLatin1Char(' '), QLatin1Char('_'));
    suggestedFile += QStringLiteral(".json");
    if (!suggestedDir.isEmpty())
        suggestedFile = QDir(suggestedDir).absoluteFilePath(suggestedFile);

    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save tune as JSON"), suggestedFile,
        QStringLiteral("Stage JSON (*.json);;All files (*)"));
    if (p.isEmpty()) return;
    QString err;
    if (!pkg.saveToJson(p, &err)) {
        QMessageBox::warning(this, QStringLiteral("Save"),
            QStringLiteral("Failed to save: %1").arg(err));
        return;
    }
    QMessageBox::information(this, QStringLiteral("Save"),
        QStringLiteral("Saved to %1\n\nIt will appear in the Apply Stage picker "
                       "after the next driver/bin reload.")
            .arg(QFileInfo(p).fileName()));
}

void CustomTuneDialog::onApply()
{
    if (!m_win || !m_driver || !m_origBin) {
        QMessageBox::warning(this, QStringLiteral("Apply"),
            QStringLiteral("Driver and Original bin must be loaded."));
        return;
    }
    const StagePackage pkg = buildPackage();
    if (pkg.edits.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Apply"),
            QStringLiteral("No edits configured. Add at least one row."));
        return;
    }
    // Stash for the parent to read after exec() returns. We don't apply
    // directly here so the parent can route the write through its
    // existing undoable BulkRegionCommand path - same as Apply Stage.
    m_resultPackage = pkg;
    accept();
}

} // namespace EcuParser
