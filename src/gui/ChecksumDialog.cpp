#include "ChecksumDialog.h"
#include "MainWindow.h"
#include "UndoCommands.h"
#include "../core/BinFile.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace EcuParser {

namespace {
const QColor kKeepBg   (220, 245, 220);   // green: KeepOriginal
const QColor kCopyBg   (220, 230, 250);   // blue:  CopyFromReference
const QColor kBadBg    (245, 215, 210);   // red:   Unresolvable
const QColor kBadFg    (130,  20,  20);
} // namespace

ChecksumDialog::ChecksumDialog(MainWindow *win,
                               BinFile *modBin,
                               const BinFile *origBin,
                               const BinFile *refBin,
                               const QString &binPath,
                               const QString &schemaId,
                               QWidget *parent)
    : QDialog(parent),
      m_win(win),
      m_modBin(modBin),
      m_origBin(origBin),
      m_refBin(refBin),
      m_binPath(binPath),
      m_schemaId(schemaId)
{
    setWindowTitle(QStringLiteral("Checksum verify / preview"));
    m_profile = Checksum::profileForSchema(schemaId);
    buildUi();
    refreshTable();
}

QString ChecksumDialog::strategyText(ChecksumStrategy s) const
{
    switch (s) {
    case ChecksumStrategy::KeepOriginal:
        return QStringLiteral("Keep original");
    case ChecksumStrategy::CopyFromReference:
        return QStringLiteral("Copy from reference");
    case ChecksumStrategy::Unresolvable:
        return QStringLiteral("UNRESOLVABLE");
    }
    return QString();
}

void ChecksumDialog::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    m_header = new QLabel(this);
    QString headerText = QStringLiteral(
        "<b>Modified bin:</b> %1<br><b>Schema:</b> %2<br><b>Profile:</b> %3")
            .arg(QFileInfo(m_binPath).fileName(), m_schemaId,
                 m_profile.name.isEmpty() ? QStringLiteral("(none)")
                                          : m_profile.name);
    if (m_profile.ranges.isEmpty()) {
        headerText += QStringLiteral(
            "<br><span style='color:#A02020;'>"
            "<b>No profile available for this schema.</b></span>");
    } else if (!m_origBin) {
        headerText += QStringLiteral(
            "<br><span style='color:#A02020;'>"
            "<b>No original bin loaded.</b> Load one to enable checksum "
            "preview - the original is the source of valid stored CS "
            "words for unmodified blocks.</span>");
    } else {
        headerText += QStringLiteral(
            "<br><i>Bosch EDC15C calibration CRC is proprietary; we "
            "cannot recompute. Strategies below preserve or copy known-"
            "good stored values from the original / reference bin.</i>");
    }
    m_header->setText(headerText);
    m_header->setWordWrap(true);
    m_header->setStyleSheet(QStringLiteral(
        "QLabel { background: #F0F2F4; padding: 8px; border: 1px solid #DDD; }"));
    vlay->addWidget(m_header);

    auto *refRow = new QHBoxLayout();
    m_refLabel = new QLabel(this);
    m_refLabel->setWordWrap(true);
    refRow->addWidget(m_refLabel, 1);
    m_loadRefBtn = new QPushButton(QStringLiteral("Load reference..."), this);
    m_loadRefBtn->setToolTip(QStringLiteral(
        "Pick a known-good tuned bin whose checksum words can be copied "
        "into the modified bin for blocks where the bytes match exactly."));
    refRow->addWidget(m_loadRefBtn);
    vlay->addLayout(refRow);

    m_table = new QTableWidget(this);
    const QStringList cols {
        QStringLiteral("Block"),
        QStringLiteral("Range"),
        QStringLiteral("Store @"),
        QStringLiteral("Bytes vs Orig"),
        QStringLiteral("Bytes vs Ref"),
        QStringLiteral("Stored"),
        QStringLiteral("Will write"),
        QStringLiteral("Strategy"),
    };
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    vlay->addWidget(m_table, 1);

    m_log = new QLabel(this);
    m_log->setWordWrap(false);
    m_log->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_log->setStyleSheet(QStringLiteral(
        "QLabel { background: #FAFAFA; color: #333; padding: 6px;"
        " border: 1px solid #DDD; font-family: monospace; font-size: 11px; }"));
    m_log->setText(QStringLiteral("(idle)"));
    m_log->setMinimumHeight(220);
    m_log->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    vlay->addWidget(m_log);

    auto *btnRow = new QHBoxLayout();
    m_applyBtn = new QPushButton(QStringLiteral("Apply strategies now"), this);
    m_applyBtn->setToolTip(QStringLiteral(
        "Write the resolved checksum words into the modified bin in "
        "memory. The actual file on disk is not touched - use File > "
        "Export modified to save."));
    m_applyBtn->setEnabled(!m_profile.ranges.isEmpty() && m_origBin && m_modBin);
    btnRow->addWidget(m_applyBtn);
    btnRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    btnRow->addWidget(closeBtn);
    vlay->addLayout(btnRow);

    connect(m_loadRefBtn, &QPushButton::clicked, this, &ChecksumDialog::onLoadReferenceBin);
    connect(m_applyBtn,   &QPushButton::clicked, this, &ChecksumDialog::onApply);
    connect(closeBtn,     &QPushButton::clicked, this, &QDialog::accept);

    setMinimumSize(960, 720);
}

void ChecksumDialog::refreshTable()
{
    // Reference bin label.
    if (m_refBin) {
        m_refLabel->setText(QStringLiteral(
            "<b>Reference bin:</b> loaded (size %1)").arg(m_refBin->size()));
    } else {
        m_refLabel->setText(QStringLiteral(
            "<b>Reference bin:</b> (none) - load one to enable "
            "CopyFromReference for blocks whose bytes you've changed."));
    }

    if (!m_modBin || !m_origBin) {
        m_table->setRowCount(0);
        return;
    }
    if (m_profile.ranges.isEmpty()) {
        m_table->setRowCount(0);
        return;
    }

    const ChecksumStatus s = Checksum::evaluate(
        *m_modBin, *m_origBin, m_refBin, m_profile);

    m_table->setRowCount(s.blocks.size());
    for (int i = 0; i < s.blocks.size(); ++i) {
        const ChecksumBlockStatus &b = s.blocks.at(i);

        QColor bg;
        switch (b.strategy) {
        case ChecksumStrategy::KeepOriginal:      bg = kKeepBg; break;
        case ChecksumStrategy::CopyFromReference: bg = kCopyBg; break;
        case ChecksumStrategy::Unresolvable:      bg = kBadBg;  break;
        }

        auto setCell = [&](int col, const QString &txt,
                           Qt::Alignment align = Qt::AlignCenter) {
            auto *it = new QTableWidgetItem(txt);
            it->setTextAlignment(align);
            it->setBackground(QBrush(bg));
            if (b.strategy == ChecksumStrategy::Unresolvable) {
                it->setForeground(QBrush(kBadFg));
                QFont f = it->font();
                f.setBold(col == 7);
                it->setFont(f);
            }
            m_table->setItem(i, col, it);
        };

        // Pick the value that will be written, based on strategy.
        quint32 willWrite = 0;
        switch (b.strategy) {
        case ChecksumStrategy::KeepOriginal:      willWrite = b.originalValue;  break;
        case ChecksumStrategy::CopyFromReference: willWrite = b.referenceValue; break;
        case ChecksumStrategy::Unresolvable:      willWrite = 0;                break;
        }

        setCell(0, QStringLiteral("#%1").arg(i));
        setCell(1, QStringLiteral("0x%1..0x%2")
                       .arg(b.startOffset, 6, 16, QLatin1Char('0'))
                       .arg(b.endOffset,   6, 16, QLatin1Char('0')).toUpper());
        setCell(2, QStringLiteral("0x%1")
                       .arg(b.storeOffset, 6, 16, QLatin1Char('0')).toUpper());
        setCell(3, b.dataMatchesOriginal
                       ? QStringLiteral("match")
                       : QStringLiteral("DIFFER"));
        setCell(4, !b.hasReference ? QStringLiteral("(no ref)")
                       : (b.dataMatchesReference ? QStringLiteral("match")
                                                 : QStringLiteral("DIFFER")));
        setCell(5, QStringLiteral("0x%1")
                       .arg(b.storedValue, 8, 16, QLatin1Char('0')).toUpper());
        setCell(6, b.strategy == ChecksumStrategy::Unresolvable
                       ? QStringLiteral("(refused)")
                       : QStringLiteral("0x%1")
                             .arg(willWrite, 8, 16, QLatin1Char('0')).toUpper());
        setCell(7, strategyText(b.strategy),
                Qt::AlignLeft | Qt::AlignVCenter);
    }
    m_table->resizeColumnsToContents();

    // Bottom log: per-block summary, plus warnings if any.
    QStringList lines;
    for (const auto &w : s.warnings) lines.append(QStringLiteral("warn: ") + w);
    if (!s.allResolvable() && !s.blocks.isEmpty()) {
        lines.append(QStringLiteral(
            "Some blocks are UNRESOLVABLE - export will refuse them "
            "unless you load a reference bin matching the modified bytes "
            "OR use a commercial checksum corrector."));
    } else if (!s.blocks.isEmpty()) {
        lines.append(QStringLiteral(
            "All blocks resolvable. Apply strategies, then export."));
    }

    // ECM-Titanium-style analytic metrics across the whole bin. Useful
    // when the user wants to copy values into a third-party corrector
    // (most of them accept these as input). We print the metrics for
    // BOTH the modified bin and the original side-by-side so the user
    // can spot at a glance which sums changed.
    const PartialMetrics mm = computePartialMetrics(
        *m_modBin, 0, quint32(m_modBin->size() - 1));
    const PartialMetrics mo = computePartialMetrics(
        *m_origBin, 0, quint32(m_origBin->size() - 1));
    auto eq = [](quint32 a, quint32 b) {
        return a == b ? QStringLiteral("=") : QStringLiteral("!");
    };
    lines.append(QString());
    lines.append(QStringLiteral(
        "Partial metrics (whole bin, ECM-Titanium-compatible):"));
    lines.append(QStringLiteral(
        "                       MODIFIED        ORIG           same?"));
    lines.append(QStringLiteral(
        "  Checksum   (16):     0x%1          0x%2         %3")
            .arg(mm.checksum16,   4, 16, QLatin1Char('0')).toUpper()
            .arg(mo.checksum16,   4, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.checksum16, mo.checksum16)));
    lines.append(QStringLiteral(
        "  Compl      (16):     0x%1          0x%2         %3")
            .arg(mm.complement16, 4, 16, QLatin1Char('0')).toUpper()
            .arg(mo.complement16, 4, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.complement16, mo.complement16)));
    lines.append(QStringLiteral(
        "  Even       (16):     0x%1          0x%2         %3")
            .arg(mm.even16,       4, 16, QLatin1Char('0')).toUpper()
            .arg(mo.even16,       4, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.even16,    mo.even16)));
    lines.append(QStringLiteral(
        "  Odd        (16):     0x%1          0x%2         %3")
            .arg(mm.odd16,        4, 16, QLatin1Char('0')).toUpper()
            .arg(mo.odd16,        4, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.odd16,     mo.odd16)));
    lines.append(QStringLiteral(
        "  DWord      (32):     0x%1      0x%2     %3")
            .arg(mm.dword32,      8, 16, QLatin1Char('0')).toUpper()
            .arg(mo.dword32,      8, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.dword32,   mo.dword32)));
    lines.append(QStringLiteral(
        "  16bit LH   (32):     0x%1      0x%2     %3")
            .arg(mm.sumWordLE,    8, 16, QLatin1Char('0')).toUpper()
            .arg(mo.sumWordLE,    8, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.sumWordLE, mo.sumWordLE)));
    lines.append(QStringLiteral(
        "  16bit HL   (32):     0x%1      0x%2     %3")
            .arg(mm.sumWordBE,    8, 16, QLatin1Char('0')).toUpper()
            .arg(mo.sumWordBE,    8, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.sumWordBE, mo.sumWordBE)));
    lines.append(QStringLiteral(
        "  32bit #1   (BE):     0x%1      0x%2     %3")
            .arg(mm.sumDwordBE,   8, 16, QLatin1Char('0')).toUpper()
            .arg(mo.sumDwordBE,   8, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.sumDwordBE, mo.sumDwordBE)));
    lines.append(QStringLiteral(
        "  32bit #3   (LE):     0x%1      0x%2     %3")
            .arg(mm.sumDwordLE,   8, 16, QLatin1Char('0')).toUpper()
            .arg(mo.sumDwordLE,   8, 16, QLatin1Char('0')).toUpper()
            .arg(eq(mm.sumDwordLE, mo.sumDwordLE)));

    m_log->setText(lines.isEmpty() ? QStringLiteral("(idle)")
                                   : lines.join(QStringLiteral("\n")));
}

void ChecksumDialog::onLoadReferenceBin()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load reference bin"),
        QFileInfo(m_binPath).absolutePath(),
        QStringLiteral("Bin files (*.bin);;All files (*)"));
    if (p.isEmpty()) return;
    // We can't mutate m_refBin (it's const) - delegate to MainWindow.
    if (m_win) {
        // Calling MainWindow::loadReferenceBin would couple us tighter
        // than the const pointer pattern allows. Instead, ask the user
        // to use File > Reference bin and re-open the dialog. This
        // keeps this dialog read-only with respect to long-lived
        // state, which is the point of the design.
        QMessageBox::information(this, QStringLiteral("Load reference"),
            QStringLiteral(
                "Use 'File > Load reference bin...' to set the reference "
                "bin. The change applies to the next time this dialog is "
                "opened."));
    }
}

void ChecksumDialog::onApply()
{
    if (!m_modBin || !m_origBin) return;
    if (m_profile.ranges.isEmpty()) return;

    // Snapshot for undo BEFORE we mutate.
    const QByteArray prev = m_modBin->raw();
    QStringList logLines;
    const bool ok = Checksum::applyStrategies(
        m_modBin, *m_origBin, m_refBin, m_profile, &logLines);
    const QByteArray after = m_modBin->raw();

    if (prev == after) {
        m_log->setText(QStringLiteral(
            "Already in sync - no checksum bytes changed.\n%1")
                .arg(logLines.join(QStringLiteral("\n"))));
        return;
    }

    // Wrap the change in a BulkRegionCommand so the undo stack reflects
    // the "checksum apply" as a single Ctrl+Z step. The command's
    // redo() runs immediately on push; we first roll the bin back so
    // redo() can re-apply via the undo path.
    m_modBin->writeBytes(0, prev);
    if (m_win) {
        auto *cmd = new BulkRegionCommand(
            m_win, 0, prev, after,
            QStringLiteral("Checksum apply (%1)")
                .arg(ok ? QStringLiteral("all blocks resolved")
                        : QStringLiteral("some blocks unresolved")));
        m_win->pushUndoCommand(cmd);
    } else {
        m_modBin->writeBytes(0, after);
    }

    QString summary = ok
        ? QStringLiteral("Applied. All blocks resolved.")
        : QStringLiteral(
            "Applied with WARNINGS - one or more blocks remain "
            "unresolved (see log). Export will require either a "
            "reference bin whose bytes match yours, or a commercial "
            "checksum corrector before flashing.");
    m_log->setText(summary + QStringLiteral("\n")
                   + logLines.join(QStringLiteral("\n")));
    refreshTable();
}

} // namespace EcuParser
