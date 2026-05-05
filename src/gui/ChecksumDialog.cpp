#include "ChecksumDialog.h"
#include "MainWindow.h"
#include "UndoCommands.h"
#include "../core/BinFile.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoStack>
#include <QVBoxLayout>

namespace EcuParser {

namespace {
const QColor kOkBg      (220, 245, 220);
const QColor kBadBg     (245, 215, 210);
const QColor kBadFg     (130,  20,  20);

QString algoName(ChecksumAlgorithm a)
{
    switch (a) {
    case ChecksumAlgorithm::Sum32BE: return QStringLiteral("Sum32BE");
    case ChecksumAlgorithm::Sum32LE: return QStringLiteral("Sum32LE");
    case ChecksumAlgorithm::Sum16BE: return QStringLiteral("Sum16BE");
    case ChecksumAlgorithm::XorBytes: return QStringLiteral("XOR8");
    }
    return QStringLiteral("?");
}
} // namespace

ChecksumDialog::ChecksumDialog(MainWindow *win,
                               BinFile *bin,
                               const QString &binPath,
                               const QString &schemaId,
                               QWidget *parent)
    : QDialog(parent),
      m_win(win),
      m_bin(bin),
      m_binPath(binPath),
      m_schemaId(schemaId)
{
    setWindowTitle(QStringLiteral("Checksum verify / repair"));
    m_profile = Checksum::profileForSchema(schemaId);
    buildUi();
    refreshTable();
}

void ChecksumDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    m_header = new QLabel(this);
    QString headerText = QStringLiteral(
        "<b>Bin:</b> %1<br><b>Schema:</b> %2<br><b>Profile:</b> %3")
            .arg(QFileInfo(m_binPath).fileName(), m_schemaId,
                 m_profile.name.isEmpty() ? QStringLiteral("(none)")
                                          : m_profile.name);
    if (m_profile.ranges.isEmpty()) {
        headerText += QStringLiteral(
            "<br><span style='color:#A02020;'>"
            "<b>No profile available for this schema.</b> Use Detect to find "
            "candidate checksum locations, or extend Checksum.cpp.</span>");
    } else {
        headerText += QStringLiteral(
            "<br><i>Note: 28F0_100 profile addresses are placeholders. "
            "Verify against your physical-write tool before flashing the ECU.</i>");
    }
    m_header->setText(headerText);
    m_header->setWordWrap(true);
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { background: #F0F2F4; padding: 8px; border: 1px solid #DDD; }"));
    vlay->addWidget(m_header);

    m_table = new QTableWidget(this);
    const QStringList cols {
        QStringLiteral("Description"),
        QStringLiteral("Range"),
        QStringLiteral("Store @"),
        QStringLiteral("Algo"),
        QStringLiteral("Stored"),
        QStringLiteral("Computed"),
        QStringLiteral("Status"),
    };
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    vlay->addWidget(m_table, 1);

    m_log = new QLabel(this);
    m_log->setWordWrap(true);
    m_log->setStyleSheet(QStringLiteral(
        "QLabel { background: #FAFAFA; color: #333; padding: 6px;"
        " border: 1px solid #DDD; font-family: monospace; font-size: 11px; }"));
    m_log->setText(QStringLiteral("(idle)"));
    m_log->setMinimumHeight(80);
    vlay->addWidget(m_log);

    auto *btnRow = new QHBoxLayout();
    m_detectBtn = new QPushButton(QStringLiteral("Detect candidates"), this);
    m_detectBtn->setToolTip(QStringLiteral(
        "Sweep the bin for u32 BE values matching a running additive sum. "
        "Useful when no profile applies to your schema."));
    btnRow->addWidget(m_detectBtn);
    m_dryBtn = new QPushButton(QStringLiteral("Dry-run repair"), this);
    m_dryBtn->setToolTip(QStringLiteral("Show what would change without writing."));
    btnRow->addWidget(m_dryBtn);
    m_repairBtn = new QPushButton(QStringLiteral("Repair && write"), this);
    m_repairBtn->setToolTip(QStringLiteral(
        "Write computed sums into storeOffset for every range. Undoable (Ctrl+Z)."));
    m_repairBtn->setEnabled(!m_profile.ranges.isEmpty());
    btnRow->addWidget(m_repairBtn);
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    btnRow->addWidget(closeBtn);
    vlay->addLayout(btnRow);

    connect(m_detectBtn,  &QPushButton::clicked, this, &ChecksumDialog::onDetect);
    connect(m_dryBtn,     &QPushButton::clicked, this, &ChecksumDialog::onDryRun);
    connect(m_repairBtn,  &QPushButton::clicked, this, &ChecksumDialog::onRepair);
    connect(closeBtn,     &QPushButton::clicked, this, &QDialog::accept);

    setMinimumSize(820, 480);
}

void ChecksumDialog::refreshTable()
{
    if (!m_bin) return;
    m_table->setRowCount(m_profile.ranges.size());
    if (m_profile.ranges.isEmpty()) {
        return;
    }
    const ChecksumStatus s = Checksum::verify(*m_bin, m_profile);
    for (int i = 0; i < m_profile.ranges.size(); ++i) {
        const ChecksumRange &r = m_profile.ranges.at(i);
        const bool ok = (i < s.ok.size()) ? s.ok.at(i) : false;
        const quint32 stored   = (i < s.stored.size())   ? s.stored.at(i)   : 0;
        const quint32 computed = (i < s.computed.size()) ? s.computed.at(i) : 0;

        auto setCell = [&](int col, const QString &txt,
                           Qt::Alignment align = Qt::AlignCenter) {
            auto *it = new QTableWidgetItem(txt);
            it->setTextAlignment(align);
            if (ok) {
                it->setBackground(QBrush(kOkBg));
            } else {
                it->setBackground(QBrush(kBadBg));
                it->setForeground(QBrush(kBadFg));
                QFont f = it->font();
                f.setBold(col == 6);
                it->setFont(f);
            }
            m_table->setItem(i, col, it);
        };
        setCell(0, r.description, Qt::AlignLeft | Qt::AlignVCenter);
        setCell(1, QStringLiteral("0x%1..0x%2")
                       .arg(r.startOffset, 6, 16, QLatin1Char('0'))
                       .arg(r.endOffset, 6, 16, QLatin1Char('0')).toUpper());
        setCell(2, QStringLiteral("0x%1")
                       .arg(r.storeOffset, 6, 16, QLatin1Char('0')).toUpper());
        setCell(3, algoName(r.algorithm));
        setCell(4, QStringLiteral("0x%1")
                       .arg(stored, 8, 16, QLatin1Char('0')).toUpper());
        setCell(5, QStringLiteral("0x%1")
                       .arg(computed, 8, 16, QLatin1Char('0')).toUpper());
        setCell(6, ok ? QStringLiteral("OK") : QStringLiteral("MISMATCH"));
    }
    m_table->resizeColumnsToContents();
}

void ChecksumDialog::onDryRun()
{
    if (!m_bin) return;
    QStringList logLines;
    const int n = Checksum::repair(m_bin, m_profile, /*dryRun=*/true, &logLines);
    m_log->setText(QStringLiteral("Dry run: %1 ranges would update.\n%2")
                       .arg(n).arg(logLines.join(QStringLiteral("\n"))));
}

void ChecksumDialog::onRepair()
{
    if (!m_bin) return;
    if (m_profile.ranges.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Repair"),
            QStringLiteral("No profile loaded - nothing to repair."));
        return;
    }
    // Snapshot the bin BEFORE repair so we can wrap the write in a
    // BulkRegionCommand. We snapshot the whole bin (rather than per-range
    // slices) so a single Ctrl+Z reverts the entire repair.
    const QByteArray prev = m_bin->raw();
    QStringList logLines;
    const int n = Checksum::repair(m_bin, m_profile, /*dryRun=*/false, &logLines);
    if (n == 0) {
        m_log->setText(QStringLiteral("Already in sync - nothing written.\n%1")
                           .arg(logLines.join(QStringLiteral("\n"))));
        return;
    }
    const QByteArray after = m_bin->raw();
    // Roll the in-memory bin back so the BulkRegionCommand's redo() can
    // write the post-repair bytes through MainWindow's undo path. This
    // keeps undo accounting consistent (pre = prev, post = after) and
    // makes Ctrl+Z revert the repair in one step.
    m_bin->writeBytes(0, prev);

    if (m_win) {
        auto *cmd = new BulkRegionCommand(
            m_win, 0, prev, after,
            QStringLiteral("Checksum repair (%1 ranges)").arg(n));
        m_win->pushUndoCommand(cmd);  // takes ownership; redo() runs.
    } else {
        m_bin->writeBytes(0, after);
    }
    m_log->setText(QStringLiteral("Repaired: %1 ranges written.\n%2")
                       .arg(n).arg(logLines.join(QStringLiteral("\n"))));
    refreshTable();
}

void ChecksumDialog::onDetect()
{
    if (!m_bin) return;
    const auto hits = Checksum::detect(*m_bin, /*maxHits=*/8);
    if (hits.isEmpty()) {
        m_log->setText(QStringLiteral(
            "Detect: no simple [0..N] additive-sum checksum found in this bin.\n"
            "The bin may use a different layout (multi-range, non-zero start, "
            "or a different algorithm). Open Checksum.cpp to add a profile."));
        return;
    }
    QStringList lines;
    lines.append(QStringLiteral("Detect: %1 candidate(s) found.").arg(hits.size()));
    lines.append(QStringLiteral(
        "Each line: store_offset = sum_of_bytes(range_start..range_end)"));
    for (const auto &h : hits) {
        lines.append(QStringLiteral(
            "  store @ 0x%1   <-  sum( 0x%2 .. 0x%3 )")
                .arg(h.storeOffset, 6, 16, QLatin1Char('0'))
                .arg(h.rangeStart,  6, 16, QLatin1Char('0'))
                .arg(h.rangeEnd,    6, 16, QLatin1Char('0')).toUpper());
    }
    m_log->setText(lines.join(QStringLiteral("\n")));
}

} // namespace EcuParser
