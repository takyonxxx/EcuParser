#include "HexEditorWidget.h"
#include "MainWindow.h"
#include "UndoCommands.h"
#include "../core/BinFile.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QFont>
#include <QColor>
#include <QBrush>
#include <QShortcut>
#include <QKeySequence>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QRegularExpression>
#include <QDebug>

namespace EcuParser {

namespace {

// 16 bytes per row, ECM Titanium-style. Constant - not user-tweakable.
// Address column + 16 hex columns + 1 ASCII column = 18 columns total.
constexpr int kBytesPerRow = 16;
constexpr int kColAddress  = 0;
constexpr int kColHexFirst = 1;                       // hex bytes start here
constexpr int kColHexLast  = kColHexFirst + 15;       // last hex column
constexpr int kColAscii    = kColHexFirst + 16;       // single ASCII cell

// Convert a byte to its printable ASCII glyph, replacing non-printable
// bytes with '.' (mirrors the right-hand panel in the screenshot the
// user sent).
QChar asciiGlyph(quint8 b)
{
    if (b >= 0x20 && b < 0x7F)
        return QLatin1Char(char(b));
    return QLatin1Char('.');
}

QString hexByte(quint8 b)
{
    return QStringLiteral("%1")
        .arg(uint(b), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString hexAddress(quint32 offset)
{
    return QStringLiteral("%1")
        .arg(offset, 6, 16, QLatin1Char('0'))
        .toUpper();
}

} // anonymous

HexEditorWidget::HexEditorWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void HexEditorWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // === Top toolbar: Go to / Find ===
    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);

    auto *gotoBtn = new QPushButton(QStringLiteral("Go to..."), this);
    gotoBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    gotoBtn->setToolTip(QStringLiteral("Jump to a hex address (Ctrl+G)"));
    connect(gotoBtn, &QPushButton::clicked, this, &HexEditorWidget::onGotoTriggered);
    bar->addWidget(gotoBtn);

    bar->addSpacing(12);
    bar->addWidget(new QLabel(QStringLiteral("Find:"), this));
    m_findInput = new QLineEdit(this);
    m_findInput->setPlaceholderText(
        QStringLiteral("Hex bytes, e.g. \"DE AD BE EF\" or \"deadbeef\" or \"DE ?? BE EF\""));
    m_findInput->setMinimumWidth(280);
    connect(m_findInput, &QLineEdit::returnPressed,
            this, &HexEditorWidget::onFindTriggered);
    bar->addWidget(m_findInput, 1);

    auto *findBtn = new QPushButton(QStringLiteral("Find"), this);
    findBtn->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    connect(findBtn, &QPushButton::clicked, this, &HexEditorWidget::onFindTriggered);
    bar->addWidget(findBtn);

    m_findNextBtn = new QPushButton(QStringLiteral("Find next"), this);
    m_findNextBtn->setShortcut(QKeySequence(Qt::Key_F3));
    m_findNextBtn->setEnabled(false);
    connect(m_findNextBtn, &QPushButton::clicked,
            this, &HexEditorWidget::onFindNext);
    bar->addWidget(m_findNextBtn);

    root->addLayout(bar);

    // === Hex table ===
    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColAscii + 1);
    QStringList headers;
    headers << QStringLiteral("Address");
    for (int i = 0; i < kBytesPerRow; ++i)
        headers << hexByte(quint8(i));        // "00", "01", ..., "0F"
    headers << QStringLiteral("ASCII");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);

    // Monospace font - mandatory for hex alignment to look right.
    QFont mono(QStringLiteral("Consolas"));
    if (!QFontInfo(mono).fixedPitch()) {
        mono = QFont(QStringLiteral("Courier New"));
        if (!QFontInfo(mono).fixedPitch())
            mono = QFont(QStringLiteral("Monospace"));
    }
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_table->setFont(mono);

    // Compact, fixed-width columns. Hex columns get a tight width that
    // fits "FF " comfortably; ASCII gets enough for 16 chars.
    QFontMetrics fm(mono);
    const int hexW   = fm.horizontalAdvance(QStringLiteral("FF")) + 14;
    const int addrW  = fm.horizontalAdvance(QStringLiteral("000000")) + 16;
    const int asciiW = fm.horizontalAdvance(QStringLiteral("0123456789ABCDEF")) + 12;
    m_table->setColumnWidth(kColAddress, addrW);
    for (int i = 0; i < kBytesPerRow; ++i)
        m_table->setColumnWidth(kColHexFirst + i, hexW);
    m_table->setColumnWidth(kColAscii, asciiW);
    m_table->horizontalHeader()->setStretchLastSection(false);
    // Tighter rows so more bytes are visible at once.
    m_table->verticalHeader()->setDefaultSectionSize(fm.height() + 2);

    connect(m_table, &QTableWidget::cellChanged,
            this, &HexEditorWidget::onCellChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &HexEditorWidget::onCellSelectionChanged);

    root->addWidget(m_table, 1);

    // === Bottom status row, ECM Titanium-ish "Address / Loaded EPROM data" ===
    auto *status = new QHBoxLayout;
    status->setSpacing(12);
    m_addrLbl  = new QLabel(QStringLiteral("Address: ------"), this);
    m_valueLbl = new QLabel(QStringLiteral("Value: --"), this);
    m_selLbl   = new QLabel(QStringLiteral("Selection: -"), this);
    m_sizeLbl  = new QLabel(QStringLiteral("Size: 0 bytes"), this);
    m_addrLbl->setFont(mono);
    m_valueLbl->setFont(mono);
    m_selLbl->setFont(mono);
    m_sizeLbl->setFont(mono);
    status->addWidget(m_addrLbl);
    status->addWidget(m_valueLbl);
    status->addWidget(m_selLbl);
    status->addStretch(1);
    status->addWidget(m_sizeLbl);
    root->addLayout(status);
}

void HexEditorWidget::setBins(BinFile *original, BinFile *modified)
{
    m_orig = original;
    m_mod  = modified;
    // If the user is currently looking at this tab, rebuild now;
    // otherwise just mark dirty - rebuildTable() will run the next
    // time the tab becomes visible (showEvent) or refresh() is called
    // explicitly. For a 512 KiB bin the table holds 32768 rows, and
    // we don't want to pay that cost while the user is in Table /
    // Graph / 3D / Diff and never touches Hex.
    if (isVisible())
        rebuildTable();
    else
        m_pendingRebuild = true;
}

void HexEditorWidget::showEvent(QShowEvent *ev)
{
    QWidget::showEvent(ev);
    if (m_pendingRebuild) {
        m_pendingRebuild = false;
        rebuildTable();
    }
}

void HexEditorWidget::refresh()
{
    // Same lazy strategy as setBins(): if the tab isn't on screen we
    // just remember that the bytes changed and let showEvent() do the
    // work. The undoRedoRefresh path runs on every undo/redo and on
    // every Apply Stage, so skipping it when invisible saves a lot of
    // QTableWidgetItem churn.
    if (!isVisible()) {
        m_pendingRebuild = true;
        return;
    }

    // Fast path: if the table is already laid out for the current bin
    // size, just walk the existing QTableWidgetItems and update their
    // text/diff color. Replacing items via setItem() invalidates user
    // selection and scroll position and is multiple times slower for a
    // 512 KiB bin (32768 rows). Slow path (rebuild from scratch) is
    // only taken on first load or when the bin size changes underneath.
    if (m_mod && !m_mod->isEmpty()) {
        const qsizetype total = m_mod->size();
        const int expectedRows = int((total + kBytesPerRow - 1) / kBytesPerRow);
        if (m_table->rowCount() == expectedRows && expectedRows > 0) {
            QSignalBlocker block(m_table);
            m_suppressCellSignals = true;
            const QByteArray &bytes = m_mod->raw();
            for (int r = 0; r < expectedRows; ++r) {
                const quint32 base = quint32(r) * kBytesPerRow;
                QString asciiBuf;
                asciiBuf.reserve(kBytesPerRow);
                for (int c = 0; c < kBytesPerRow; ++c) {
                    const qsizetype off = qsizetype(base) + c;
                    QTableWidgetItem *cell = m_table->item(r, kColHexFirst + c);
                    if (!cell) continue;
                    if (off < total) {
                        const quint8 b = quint8(bytes.at(off));
                        cell->setText(hexByte(b));
                        asciiBuf.append(asciiGlyph(b));
                    } else {
                        cell->setText(QString());
                        asciiBuf.append(QLatin1Char(' '));
                    }
                }
                if (auto *asc = m_table->item(r, kColAscii))
                    asc->setText(asciiBuf);
                updateRowDiff(r);
            }
            m_sizeLbl->setText(QStringLiteral("Size: %1 bytes (0x%2)")
                                   .arg(total)
                                   .arg(quint64(total), 0, 16).toUpper());
            m_suppressCellSignals = false;
            return;
        }
    }
    // Slow path: layout differs (first show, or bin swapped to a
    // different size). Rebuild the whole table.
    rebuildTable();
}

void HexEditorWidget::rebuildTable()
{
    QSignalBlocker block(m_table);
    m_suppressCellSignals = true;

    if (!m_mod || m_mod->isEmpty()) {
        m_table->setRowCount(0);
        m_sizeLbl->setText(QStringLiteral("Size: 0 bytes"));
        m_addrLbl->setText(QStringLiteral("Address: ------"));
        m_valueLbl->setText(QStringLiteral("Value: --"));
        m_selLbl->setText(QStringLiteral("Selection: -"));
        m_suppressCellSignals = false;
        return;
    }

    const QByteArray &bytes = m_mod->raw();
    const qsizetype total = bytes.size();
    const int rows = int((total + kBytesPerRow - 1) / kBytesPerRow);
    m_table->setRowCount(rows);

    for (int r = 0; r < rows; ++r) {
        const quint32 base = quint32(r) * kBytesPerRow;

        // Address column - read-only.
        auto *addrItem = new QTableWidgetItem(hexAddress(base));
        addrItem->setFlags(Qt::ItemIsEnabled);
        addrItem->setForeground(QBrush(QColor(80, 80, 80)));
        m_table->setItem(r, kColAddress, addrItem);

        // 16 hex byte cells.
        QString asciiBuf;
        asciiBuf.reserve(kBytesPerRow);
        for (int c = 0; c < kBytesPerRow; ++c) {
            const qsizetype off = qsizetype(base) + c;
            QTableWidgetItem *cell = new QTableWidgetItem;
            cell->setTextAlignment(Qt::AlignCenter);
            if (off < total) {
                const quint8 b = quint8(bytes.at(off));
                cell->setText(hexByte(b));
                asciiBuf.append(asciiGlyph(b));
            } else {
                cell->setFlags(Qt::ItemIsEnabled);   // dim off-end cells
                cell->setText(QString());
                asciiBuf.append(QLatin1Char(' '));
            }
            m_table->setItem(r, kColHexFirst + c, cell);
        }

        // ASCII gutter - editable, but we restrict edits to single-char
        // typing inside onCellChanged.
        auto *asciiItem = new QTableWidgetItem(asciiBuf);
        asciiItem->setFont(m_table->font());
        m_table->setItem(r, kColAscii, asciiItem);

        updateRowDiff(r);
    }

    m_sizeLbl->setText(QStringLiteral("Size: %1 bytes (0x%2)")
                           .arg(total)
                           .arg(quint64(total), 0, 16).toUpper());
    m_suppressCellSignals = false;
}

void HexEditorWidget::refreshDiffOnly()
{
    if (!m_mod || m_mod->isEmpty()) return;
    QSignalBlocker block(m_table);
    m_suppressCellSignals = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        updateRowDiff(r);
    m_suppressCellSignals = false;
}

void HexEditorWidget::updateRowDiff(int row)
{
    if (!m_mod) return;
    for (int c = 0; c < kBytesPerRow; ++c)
        recolorByte(row, kColHexFirst + c);
}

void HexEditorWidget::recolorByte(int row, int hexCol)
{
    if (!m_mod) return;
    QTableWidgetItem *cell = m_table->item(row, hexCol);
    if (!cell) return;
    const qsizetype off = qsizetype(row) * kBytesPerRow
                          + (hexCol - kColHexFirst);
    if (off >= m_mod->size()) {
        cell->setBackground(QBrush());
        cell->setForeground(QBrush());
        return;
    }
    const quint8 modByte = quint8(m_mod->raw().at(off));

    bool diff = false;
    if (m_orig && off < m_orig->size())
        diff = (modByte != quint8(m_orig->raw().at(off)));

    // ECM Titanium uses red rows in the "Modified" status panel; we
    // mirror that by coloring divergent bytes' background red and the
    // foreground white so they stand out against alternating row tints.
    if (diff) {
        cell->setBackground(QBrush(QColor(0xC0, 0x39, 0x2B)));
        cell->setForeground(QBrush(Qt::white));
    } else {
        cell->setBackground(QBrush());
        cell->setForeground(QBrush());
    }
}

void HexEditorWidget::onCellChanged(int row, int col)
{
    if (m_suppressCellSignals) return;
    if (!m_mod) return;

    // Hex side edit ----------------------------------------------------
    if (col >= kColHexFirst && col <= kColHexLast) {
        QTableWidgetItem *cell = m_table->item(row, col);
        if (!cell) return;
        const qint64 off = cellToOffsetHex(row, col);
        if (off < 0 || off >= m_mod->size()) {
            // Restore display on invalid offset.
            QSignalBlocker b(m_table);
            cell->setText(QString());
            return;
        }
        const QString text = cell->text().trimmed();
        bool ok = false;
        const uint v = text.toUInt(&ok, 16);
        const quint8 oldByte = quint8(m_mod->raw().at(off));
        if (!ok || v > 0xFF) {
            // Reject the edit, snap text back to the on-disk byte.
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(hexByte(oldByte));
            m_suppressCellSignals = false;
            return;
        }
        const quint8 newByte = quint8(v);
        if (newByte == oldByte) {
            // Normalize formatting (e.g. user typed "a" -> "0A") but
            // don't push an undo command for a no-op.
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(hexByte(newByte));
            m_suppressCellSignals = false;
            return;
        }
        if (!writeByteUndoable(quint32(off), newByte)) {
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(hexByte(oldByte));
            m_suppressCellSignals = false;
            return;
        }
        // Refresh display: hex cell text, matching ASCII cell, diff color.
        QSignalBlocker b(m_table);
        m_suppressCellSignals = true;
        cell->setText(hexByte(newByte));
        // Update the ASCII gutter.
        QTableWidgetItem *asc = m_table->item(row, kColAscii);
        if (asc) {
            QString s = asc->text();
            const int idx = col - kColHexFirst;
            if (idx >= 0 && idx < s.size())
                s[idx] = asciiGlyph(newByte);
            else if (idx >= 0)
                s.append(asciiGlyph(newByte));
            asc->setText(s);
        }
        recolorByte(row, col);
        updateStatusForOffset(quint32(off));
        m_suppressCellSignals = false;
        return;
    }

    // ASCII side edit --------------------------------------------------
    if (col == kColAscii) {
        QTableWidgetItem *cell = m_table->item(row, col);
        if (!cell) return;
        const QString s = cell->text();
        // Reconstruct what the row's ASCII text "should" be from current
        // bytes, compare char-by-char to find the single position the
        // user changed. We only accept edits where exactly one
        // printable character differs.
        const quint32 base = quint32(row) * kBytesPerRow;
        QString expected;
        expected.reserve(kBytesPerRow);
        for (int i = 0; i < kBytesPerRow; ++i) {
            const qsizetype off = qsizetype(base) + i;
            if (off < m_mod->size())
                expected.append(asciiGlyph(quint8(m_mod->raw().at(off))));
            else
                expected.append(QLatin1Char(' '));
        }

        // Find the first differing position and force the edit to apply
        // there. If the lengths don't match (user added/removed chars)
        // we restore the expected string.
        int diffIdx = -1;
        if (s.size() == expected.size()) {
            for (int i = 0; i < s.size(); ++i) {
                if (s.at(i) != expected.at(i)) {
                    diffIdx = i;
                    break;
                }
            }
        }
        if (diffIdx < 0) {
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(expected);
            m_suppressCellSignals = false;
            return;
        }
        const QChar typed = s.at(diffIdx);
        // Only accept printable ASCII input; anything else snaps back.
        const ushort u = typed.unicode();
        if (u < 0x20 || u >= 0x7F) {
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(expected);
            m_suppressCellSignals = false;
            return;
        }
        const qsizetype off = qsizetype(base) + diffIdx;
        if (off >= m_mod->size()) {
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(expected);
            m_suppressCellSignals = false;
            return;
        }
        const quint8 newByte = quint8(u);
        if (!writeByteUndoable(quint32(off), newByte)) {
            QSignalBlocker b(m_table);
            m_suppressCellSignals = true;
            cell->setText(expected);
            m_suppressCellSignals = false;
            return;
        }
        // Update the matching hex cell + diff coloring.
        QSignalBlocker b(m_table);
        m_suppressCellSignals = true;
        QTableWidgetItem *hex = m_table->item(row, kColHexFirst + diffIdx);
        if (hex) hex->setText(hexByte(newByte));
        recolorByte(row, kColHexFirst + diffIdx);
        updateStatusForOffset(quint32(off));
        m_suppressCellSignals = false;
        return;
    }
}

bool HexEditorWidget::writeByteUndoable(quint32 offset, quint8 newValue)
{
    if (!m_mod) return false;
    if (qsizetype(offset) >= m_mod->size()) return false;

    const QByteArray oldBytes = m_mod->readBytes(offset, 1);
    if (oldBytes.size() != 1) return false;
    QByteArray newBytes;
    newBytes.append(char(newValue));

    if (m_win) {
        // BulkRegionCommand::redo() writes through to m_modBin and then
        // calls undoRedoRefresh() which fires the central refresh path.
        auto *cmd = new BulkRegionCommand(
            m_win, offset, oldBytes, newBytes,
            QStringLiteral("Hex edit @ 0x%1: %2 -> %3")
                .arg(offset, 6, 16, QLatin1Char('0'))
                .arg(uint(quint8(oldBytes.at(0))), 2, 16, QLatin1Char('0'))
                .arg(uint(newValue), 2, 16, QLatin1Char('0')));
        m_win->pushUndoCommand(cmd);
        // pushUndoCommand calls redo() synchronously; the byte is in
        // place by the time we return.
        return true;
    }

    // No MainWindow attached - fall back to direct write.
    return m_mod->writeBytes(offset, newBytes);
}

qint64 HexEditorWidget::cellToOffsetHex(int row, int col) const
{
    if (col < kColHexFirst || col > kColHexLast) return -1;
    return qint64(row) * kBytesPerRow + (col - kColHexFirst);
}

qint64 HexEditorWidget::cellToOffsetAscii(int row, int col) const
{
    if (col != kColAscii) return -1;
    return qint64(row) * kBytesPerRow;
}

void HexEditorWidget::offsetToHexCell(quint32 offset, int *row, int *col) const
{
    if (!m_mod || qsizetype(offset) >= m_mod->size()) {
        if (row) *row = -1;
        if (col) *col = -1;
        return;
    }
    if (row) *row = int(offset / kBytesPerRow);
    if (col) *col = kColHexFirst + int(offset % kBytesPerRow);
}

void HexEditorWidget::onCellSelectionChanged()
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) {
        m_selLbl->setText(QStringLiteral("Selection: -"));
        return;
    }

    // Compute min/max byte offset over the selection. Address column
    // and out-of-range hex cells contribute nothing.
    qint64 minOff = -1, maxOff = -1;
    for (QTableWidgetItem *it : items) {
        const int r = it->row();
        const int c = it->column();
        qint64 off = -1;
        if (c >= kColHexFirst && c <= kColHexLast)
            off = cellToOffsetHex(r, c);
        else if (c == kColAscii)
            off = cellToOffsetAscii(r, c);     // start of row
        if (off < 0) continue;
        if (m_mod && off >= m_mod->size()) continue;
        if (minOff < 0 || off < minOff) minOff = off;
        if (maxOff < 0 || off > maxOff) maxOff = off;
    }
    if (minOff < 0) {
        m_selLbl->setText(QStringLiteral("Selection: -"));
        return;
    }
    if (minOff == maxOff) {
        m_selLbl->setText(QStringLiteral("Selection: 0x%1")
                              .arg(quint64(minOff), 6, 16, QLatin1Char('0'))
                              .toUpper());
        updateStatusForOffset(quint32(minOff));
    } else {
        m_selLbl->setText(QStringLiteral("Selection: 0x%1 - 0x%2 (%3 bytes)")
                              .arg(quint64(minOff), 6, 16, QLatin1Char('0'))
                              .arg(quint64(maxOff), 6, 16, QLatin1Char('0'))
                              .arg(maxOff - minOff + 1)
                              .toUpper());
        updateStatusForOffset(quint32(minOff));
    }
}

void HexEditorWidget::updateStatusForOffset(quint32 offset)
{
    if (!m_mod || qsizetype(offset) >= m_mod->size()) {
        m_addrLbl->setText(QStringLiteral("Address: ------"));
        m_valueLbl->setText(QStringLiteral("Value: --"));
        return;
    }
    const quint8 b = quint8(m_mod->raw().at(offset));
    m_addrLbl->setText(QStringLiteral("Address: 0x%1")
                           .arg(offset, 6, 16, QLatin1Char('0'))
                           .toUpper());
    m_valueLbl->setText(QStringLiteral("Value: 0x%1 (%2 dec, '%3')")
                            .arg(uint(b), 2, 16, QLatin1Char('0'))
                            .arg(int(b))
                            .arg(asciiGlyph(b))
                            .toUpper());
}

void HexEditorWidget::onGotoTriggered()
{
    if (!m_mod || m_mod->isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Go to address"),
                                 QStringLiteral("No bin loaded."));
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, QStringLiteral("Go to address"),
        QStringLiteral("Hex address (e.g. 0x07BD7C or 7BD7C):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || text.isEmpty()) return;
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        trimmed = trimmed.mid(2);
    bool parseOk = false;
    const quint64 off = trimmed.toULongLong(&parseOk, 16);
    if (!parseOk) {
        QMessageBox::warning(this, QStringLiteral("Go to address"),
                             QStringLiteral("Could not parse '%1' as hex.")
                                 .arg(text));
        return;
    }
    if (qsizetype(off) >= m_mod->size()) {
        QMessageBox::warning(this, QStringLiteral("Go to address"),
                             QStringLiteral("Address 0x%1 is past end of bin (size = 0x%2).")
                                 .arg(off, 0, 16)
                                 .arg(quint64(m_mod->size()), 0, 16)
                                 .toUpper());
        return;
    }
    gotoOffset(quint32(off));
}

void HexEditorWidget::gotoOffset(quint32 offset)
{
    if (!m_mod || m_mod->isEmpty()) return;
    if (qsizetype(offset) >= m_mod->size())
        offset = quint32(m_mod->size() - 1);
    int row = -1, col = -1;
    offsetToHexCell(offset, &row, &col);
    if (row < 0) return;
    QTableWidgetItem *it = m_table->item(row, col);
    if (!it) return;
    m_table->clearSelection();
    m_table->setCurrentItem(it);
    it->setSelected(true);
    m_table->scrollToItem(it, QAbstractItemView::PositionAtCenter);
    updateStatusForOffset(offset);
}

QList<int> HexEditorWidget::parseHexPattern(const QString &input, QString *err)
{
    QList<int> out;
    QString stripped = input;
    stripped.remove(QChar::Space);
    stripped.remove(QLatin1Char(','));
    if (stripped.isEmpty()) {
        if (err) *err = QStringLiteral("Pattern is empty.");
        return out;
    }
    if (stripped.size() % 2 != 0) {
        if (err) *err = QStringLiteral("Pattern must be an even number of hex digits "
                                        "(found %1).").arg(stripped.size());
        return out;
    }
    for (int i = 0; i < stripped.size(); i += 2) {
        const QStringView pair = QStringView(stripped).mid(i, 2);
        if (pair == QStringLiteral("??") || pair == QStringLiteral("**")) {
            out.append(-1);
            continue;
        }
        bool ok = false;
        const uint v = pair.toString().toUInt(&ok, 16);
        if (!ok || v > 0xFF) {
            if (err) *err = QStringLiteral("Bad byte '%1' at pos %2.")
                                .arg(pair.toString()).arg(i);
            return {};
        }
        out.append(int(v));
    }
    return out;
}

qint64 HexEditorWidget::findPattern(qint64 startOffset,
                                    const QList<int> &pattern) const
{
    if (!m_mod || pattern.isEmpty()) return -1;
    const QByteArray &b = m_mod->raw();
    const qint64 n = b.size();
    const qint64 m = pattern.size();
    if (n < m) return -1;
    for (qint64 i = std::max<qint64>(startOffset, 0); i + m <= n; ++i) {
        bool match = true;
        for (qint64 j = 0; j < m; ++j) {
            const int p = pattern.at(int(j));
            if (p < 0) continue;                     // wildcard
            if (quint8(b.at(int(i + j))) != quint8(p)) { match = false; break; }
        }
        if (match) return i;
    }
    return -1;
}

void HexEditorWidget::onFindTriggered()
{
    if (!m_mod || m_mod->isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Find"),
                                 QStringLiteral("No bin loaded."));
        return;
    }
    QString err;
    const QList<int> pat = parseHexPattern(m_findInput->text(), &err);
    if (pat.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Find"),
                             QStringLiteral("Bad pattern: %1").arg(err));
        m_findNextBtn->setEnabled(false);
        return;
    }
    m_lastPattern = pat;
    const qint64 hit = findPattern(0, pat);
    if (hit < 0) {
        m_lastFoundOffset = -1;
        m_findNextBtn->setEnabled(false);
        QMessageBox::information(this, QStringLiteral("Find"),
                                 QStringLiteral("Pattern not found."));
        return;
    }
    m_lastFoundOffset = hit;
    m_findNextBtn->setEnabled(true);
    gotoOffset(quint32(hit));
}

void HexEditorWidget::onFindNext()
{
    if (m_lastPattern.isEmpty() || !m_mod) return;
    const qint64 nextStart = m_lastFoundOffset + 1;
    const qint64 hit = findPattern(nextStart, m_lastPattern);
    if (hit < 0) {
        // Wrap from start.
        const qint64 wrap = findPattern(0, m_lastPattern);
        if (wrap < 0 || wrap == m_lastFoundOffset) {
            QMessageBox::information(this, QStringLiteral("Find"),
                                     QStringLiteral("No further matches."));
            return;
        }
        m_lastFoundOffset = wrap;
        gotoOffset(quint32(wrap));
        return;
    }
    m_lastFoundOffset = hit;
    gotoOffset(quint32(hit));
}

} // namespace EcuParser
