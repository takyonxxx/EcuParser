#include "MapTableWidget.h"

#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverNames.h"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace EcuParser {

// Colours chosen to mirror a typical light "Edit map" theme:
//   - light grey/white cell background
//   - blue header text on a slightly darker header band (we use the
//     stylesheet for the header band itself)
//   - black cell text
//   - changed cells get a light green tint, similar to how reference
//     tools mark edits.
namespace palette {
    const QColor kCellBg          (245, 246, 248);
    const QColor kCellAlt         (236, 238, 242);
    const QColor kCellText        ( 25,  30,  40);
    const QColor kChangedBg       (170, 230, 170);   // edit-green
    const QColor kChangedText     ( 10,  60,  20);
    const QColor kIncreasedTint   (210, 240, 210);   // mod > orig
    const QColor kDecreasedTint   (240, 215, 210);   // mod < orig (light pinky)
    // Background outside the cells (the area below the last row, the
    // panel behind the title and status labels). A muted slate keeps
    // the eye on the cells and stops a big white expanse from glaring
    // when the map is small.
    const QColor kPanelBg         (210, 215, 222);   // slate
    const QColor kViewportBg      (210, 215, 222);   // same slate behind the cells
}

MapTableWidget::MapTableWidget(QWidget *parent)
    : QWidget(parent)
{
    // Paint our own panel background instead of inheriting the
    // application palette. We use a stylesheet rather than only
    // setPalette() because palettes don't always win against a dark
    // application style - QSS does. The viewport gets the same colour
    // so the empty area below the last row blends into the panel.
    setObjectName(QStringLiteral("MapTablePanel"));
    setStyleSheet(QStringLiteral(
        "QWidget#MapTablePanel { background-color: #B8C0CC; }"
        "QTableWidget#MapTable { background-color: #B8C0CC; }"
        "QTableWidget#MapTable QAbstractItemView { background-color: #B8C0CC; }"
    ));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(QStringLiteral("(no map selected)"));
    QFont f = m_titleLabel->font();
    f.setBold(true);
    m_titleLabel->setFont(f);
    layout->addWidget(m_titleLabel);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("MapTable"));
    // Editable; parent watches itemChanged to commit back to the bin.
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(true);
    m_table->setSortingEnabled(false);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setDefaultSectionSize(64);
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->setFrameShape(QFrame::NoFrame);
    layout->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    connect(m_table, &QTableWidget::itemChanged,
            this, &MapTableWidget::onItemChanged);

    // Custom context menu so the user can right-click a selection and
    // set every selected cell to one value at once. This is the standard
    // workflow for raising/lowering a whole region of a fuel or boost
    // map, and the reference tool has the equivalent feature under "Manual
    // change > Set value".
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &MapTableWidget::onTableContextMenu);
}

QList<int> MapTableWidget::readAxisValues(const BinFile *bin,
                                          const AxisDefinition &axis,
                                          int count) const
{
    QList<int> out;
    if (!bin || !axis.isPresent() || count <= 0)
        return out;

    // Axis breakpoint tables in EDC15C are little-endian (verified against
    // J293_822 rail-pressure RPM axis at 0x076F0E).
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const quint32 off = axis.address + quint32(i * 2);
        bool ok = false;
        const quint16 v = bin->readU16LE(off, &ok);
        out.append(ok ? int(v) : 0);
    }
    return out;
}

QList<int> MapTableWidget::synthesizeLoadAxis(int count)
{
    QList<int> out;
    if (count <= 0)
        return out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        // (i+1) * 100 / count, rounded - reproduces reference's Load header
        // for dim=16: 6, 13, 19, 25, 31, 38, 44, 50, 56, 63, 69, 75, 81,
        // 88, 94, 100.
        out.append(int((double(i + 1) * 100.0 / double(count)) + 0.5));
    }
    return out;
}

QColor MapTableWidget::heatColour(int /*value*/, int /*lo*/, int /*hi*/)
{
    // Heatmap mode is no longer used in the the reference tool-style display. We keep the
    // function as a stub so the header signature stays stable and old call
    // sites (none currently) still compile if reintroduced.
    return palette::kCellBg;
}

void MapTableWidget::clearMap()
{
    m_suppressEdits = true;
    m_titleLabel->setText(QStringLiteral("(no map selected)"));
    m_table->clear();
    m_table->setRowCount(0);
    m_table->setColumnCount(0);
    m_statusLabel->setText(QString());
    m_currentMap = nullptr;
    m_currentInstance = 0;
    m_suppressEdits = false;
}

void MapTableWidget::showMap(const BinFile *originalBin,
                             const BinFile *modifiedBin,
                             const QString &schemaId,
                             const MapDefinition *map,
                             int instanceIndex)
{
    m_suppressEdits = true;
    m_currentMap = map;
    m_currentInstance = instanceIndex;

    if (!map || (!originalBin && !modifiedBin)) {
        clearMap();
        return;
    }
    // Use the EFFECTIVE dimensions (DriverNames override may rescue maps
    // the .drt records as 0x0). Only bail when even the override yields
    // no cells.
    const int effDimX = DriverNames::effectiveDimX(schemaId, *map);
    const int effDimY = DriverNames::effectiveDimY(schemaId, *map);
    if (effDimX <= 0 || effDimY <= 0) {
        clearMap();
        m_titleLabel->setText(
            QStringLiteral("%1 (no displayable cells)")
                .arg(DriverNames::displayName(schemaId, *map)));
        return;
    }

    if (instanceIndex < 0 || instanceIndex >= map->addresses.size())
        instanceIndex = 0;
    const quint32 addr = map->addresses.at(instanceIndex);

    // The bin we draw values from. Modified takes precedence so edits flow
    // through; original is used for diff comparison only.
    const BinFile *primary = modifiedBin ? modifiedBin : originalBin;

    // Effective instance count: clamp to maxInstances when set so the
    // title matches the tree (the reference tool hides extra instances we still parse).
    int effInstances = map->addresses.size();
    {
        const int cap = DriverNames::maxInstances(schemaId, *map);
        if (cap > 0)
            effInstances = std::min(effInstances, cap);
    }

    const QString humanName = DriverNames::displayName(schemaId, *map);
    m_titleLabel->setText(
        QStringLiteral("%1  |  %2 x %3  |  @ 0x%4%5")
            .arg(humanName)
            .arg(DriverNames::effectiveDimX(schemaId, *map))
            .arg(DriverNames::effectiveDimY(schemaId, *map))
            .arg(addr, 6, 16, QLatin1Char('0')).toUpper()
            .arg(effInstances > 1
                     ? QStringLiteral("  (instance %1/%2)")
                           .arg(instanceIndex + 1).arg(effInstances)
                     : QString()));

    // === Read both bins with override-aware dimensions ===
    MapData modData = readMapInstance(*primary, *map, instanceIndex,
                                      effDimX, effDimY);
    MapData origData;
    if (originalBin) {
        origData = readMapInstance(*originalBin, *map, instanceIndex,
                                   effDimX, effDimY);
        if (origData.cells.size() != modData.cells.size())
            origData.cells.clear();
    }

    if (modData.cells.isEmpty()) {
        clearMap();
        m_statusLabel->setText(QStringLiteral("(read failed)"));
        return;
    }

    // === Layout (reference orientation):
    //   rows    = effective dimX  (axisX, RPM)
    //   columns = effective dimY  (axisY, Load)
    // For some maps the .drt dimensions are wrong - e.g. 0x07ADD2 reports
    // 16x20 but the actual bin stride and the reference tool display are both 16x16, the
    // trailing 4 cells per row in the .drt's 20-wide reading are noise.
    // DriverNames provides per-driver overrides we honour for BOTH the
    // visible grid AND the bin stride (i.e. the override is the truth).
    const int rowCount = effDimX;
    const int colCount = effDimY;
    const int binStride = colCount;  // The override dim IS the true stride.

    // Hard-coded axis overrides take precedence (used when the reference tool
    // embeds the axis in the driver itself rather than the bin). Then we
    // fall back to bin reads, then synthesised Load%.
    QList<int> rowAxis = DriverNames::axisXOverride(schemaId, *map);
    if (rowAxis.isEmpty())
        rowAxis = readAxisValues(primary, map->axisX, rowCount);

    QList<int> colAxis = DriverNames::axisYOverride(schemaId, *map);
    if (colAxis.isEmpty())
        colAxis = readAxisValues(primary, map->axisY, colCount);
    if (colAxis.isEmpty())
        colAxis = synthesizeLoadAxis(colCount);

    m_table->setRowCount(rowCount);
    m_table->setColumnCount(colCount);

    QStringList colHeaders;
    colHeaders.reserve(colCount);
    for (int c = 0; c < colCount; ++c) {
        colHeaders.append(c < colAxis.size()
                              ? QString::number(colAxis.at(c))
                              : QString::number(c));
    }
    m_table->setHorizontalHeaderLabels(colHeaders);

    QStringList rowHeaders;
    rowHeaders.reserve(rowCount);
    for (int r = 0; r < rowCount; ++r) {
        // If we have a real or override RPM axis, use it. Otherwise show
        // a plain row index (no "R" prefix - matches the reference tool's style
        // of just showing breakpoint numbers when present, blank-ish
        // when not).
        rowHeaders.append(r < rowAxis.size()
                              ? QString::number(rowAxis.at(r))
                              : QString::number(r));
    }
    m_table->setVerticalHeaderLabels(rowHeaders);

    // Force a minimum row-header width so the corner label fits even for
    // maps that only show small numbers (0..15) in the row header.
    m_table->verticalHeader()->setMinimumWidth(70);

    // Corner label: the reference tool puts an axis hint in the top-left
    // corner of every map editor. For 2D maps we show "RPM / Load"; for
    // 1D maps (colCount == 1, e.g. torque limiter) only "RPM" makes sense.
    // We re-create the label every showMap to keep the text in sync.
    if (m_cornerLabel) {
        m_cornerLabel->deleteLater();
        m_cornerLabel = nullptr;
    }
    {
        const QString cornerText = (colCount == 1)
            ? QStringLiteral("RPM")
            : QStringLiteral("RPM / Load");
        m_cornerLabel = new QLabel(cornerText, m_table);
        m_cornerLabel->setAlignment(Qt::AlignCenter);
        m_cornerLabel->setStyleSheet(
            QStringLiteral("QLabel {"
                           "  background: #E8ECF2;"
                           "  color: #1F3A8A;"
                           "  font-weight: bold;"
                           "  border-right: 1px solid #C8CFD8;"
                           "  border-bottom: 1px solid #C8CFD8;"
                           "}"));
        // Reposition whenever the headers change size.
        auto reposCorner = [this]() {
            if (!m_cornerLabel) return;
            const int w = m_table->verticalHeader()->width();
            const int h = m_table->horizontalHeader()->height();
            m_cornerLabel->setGeometry(0, 0, w, h);
            m_cornerLabel->raise();
            m_cornerLabel->show();
        };
        connect(m_table->verticalHeader(), &QHeaderView::geometriesChanged,
                this, reposCorner);
        connect(m_table->horizontalHeader(), &QHeaderView::geometriesChanged,
                this, reposCorner);
    }

    // === Fill cells - the reference tool style: light background, only diffs are tinted.
    int diffCount = 0;
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < colCount; ++c) {
            // MapData stores cells row-major with the BIN stride
            // (map->dimY), not the display stride. Always index with the
            // bin stride so we pick the right bytes when the override
            // shrinks the visible area.
            const int idx = r * binStride + c;
            if (idx >= modData.cells.size())
                continue;
            const qint32 v = modData.cells.at(idx);

            auto *item = new QTableWidgetItem(QString::number(v));
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, idx);

            QColor bg = (r % 2) ? palette::kCellAlt : palette::kCellBg;
            QColor fg = palette::kCellText;
            bool differs = false;

            if (!origData.cells.isEmpty()) {
                const qint32 ov = origData.cells.at(idx);
                if (ov != v) {
                    differs = true;
                    ++diffCount;
                    bg = (v > ov) ? palette::kIncreasedTint : palette::kDecreasedTint;
                    fg = palette::kChangedText;
                    item->setToolTip(
                        QStringLiteral("Original: %1\nModified: %2\nDelta: %3")
                            .arg(ov).arg(v).arg(qint64(v) - qint64(ov)));
                }
            }
            item->setBackground(QBrush(bg));
            item->setForeground(QBrush(fg));
            if (differs) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }

            m_table->setItem(r, c, item);
        }
    }

    m_table->resizeColumnsToContents();
    // Spread columns to fill the available horizontal space so there's no
    // big empty gap to the right of the data. For 1D maps (single column,
    // e.g. torque limiter) Stretch would balloon the column to absurd
    // widths, so we leave it at content width in that case.
    if (colCount > 1) {
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    } else {
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        // Slightly larger than auto-fit so the column reads well.
        m_table->setColumnWidth(0, 100);
    }

    // Reposition corner label now that headers know their final size.
    if (m_cornerLabel) {
        const int w = m_table->verticalHeader()->width();
        const int h = m_table->horizontalHeader()->height();
        m_cornerLabel->setGeometry(0, 0, w, h);
        m_cornerLabel->raise();
        m_cornerLabel->show();
    }

    QString diffSummary;
    if (!origData.cells.isEmpty()) {
        diffSummary = QStringLiteral("   diff cells: %1/%2")
                          .arg(diffCount).arg(modData.cells.size());
    }
    m_statusLabel->setText(
        QStringLiteral("min=%1   max=%2   mean=%3   cells=%4%5")
            .arg(modData.minValue()).arg(modData.maxValue())
            .arg(QString::number(modData.meanValue(), 'f', 1))
            .arg(modData.cells.size())
            .arg(diffSummary));

    m_suppressEdits = false;
}

void MapTableWidget::onItemChanged(QTableWidgetItem *item)
{
    if (m_suppressEdits || !item || !m_currentMap)
        return;
    bool ok = false;
    const qint64 raw = item->text().toLongLong(&ok);
    if (!ok) {
        // Reject - revert to old value by re-emitting nothing. The parent
        // owns the bin; let it refresh the cell.
        return;
    }
    qint32 v = qint32(std::clamp<qint64>(raw, 0, 65535));
    if (v != raw) {
        // Clamp - update text.
        m_suppressEdits = true;
        item->setText(QString::number(v));
        m_suppressEdits = false;
    }
    emit cellEdited(m_currentMap, m_currentInstance,
                    item->row(), item->column(), v);
}

void MapTableWidget::onTableContextMenu(const QPoint &pos)
{
    if (!m_currentMap)
        return;

    // Snapshot the selection up-front: a QInputDialog::exec() spins the
    // event loop and Qt has been known to clear selections on focus
    // changes, so we capture the items before showing any UI.
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        // Nothing selected - if the click landed on a cell, treat that
        // single cell as the selection so right-click "just works"
        // without first having to drag-select.
        QTableWidgetItem *item = m_table->itemAt(pos);
        if (!item)
            return;
        QMenu menu(this);
        QAction *setAct = menu.addAction(QStringLiteral("Set value..."));
        if (menu.exec(m_table->viewport()->mapToGlobal(pos)) != setAct)
            return;
        bool ok = false;
        bool numOk = false;
        const int existing = item->text().toInt(&numOk);
        const int v = QInputDialog::getInt(
            this, QStringLiteral("Set cell value"),
            QStringLiteral("New value (0..65535):"),
            numOk ? existing : 0, 0, 65535, 1, &ok);
        if (!ok)
            return;
        // Round-trip through the table so onItemChanged fires and the
        // edit is committed to the modified bin via cellEdited.
        item->setText(QString::number(v));
        return;
    }

    QMenu menu(this);
    QAction *setAct = menu.addAction(
        QStringLiteral("Set value for %1 cells...").arg(selected.size()));
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen != setAct)
        return;

    // Use the first selected item's value as the dialog's initial value
    // so the user can tweak by a small delta rather than retyping.
    bool numOk = false;
    const int seed = selected.first()->text().toInt(&numOk);
    bool ok = false;
    const int v = QInputDialog::getInt(
        this, QStringLiteral("Set value for selection"),
        QStringLiteral("New value (0..65535) for %1 selected cells:")
            .arg(selected.size()),
        numOk ? seed : 0, 0, 65535, 1, &ok);
    if (!ok)
        return;

    // Snapshot the (row, col) pairs BEFORE we touch anything. We must
    // not hold on to the QTableWidgetItem pointers across the loop:
    // the parent reacts to cellEdited by repopulating the table, which
    // invalidates every item pointer. Indices are stable.
    QVector<QPair<int,int>> coords;
    coords.reserve(selected.size());
    for (QTableWidgetItem *it : selected)
        coords.append({it->row(), it->column()});

    // Tell the parent we're starting a bulk edit so it can defer the
    // single refresh to the end. Without this, every cellEdited would
    // trigger a full refreshCurrentMap() that rebuilds the table and
    // invalidates the iterators we depend on.
    emit bulkEditBegin();

    // Drive the writes purely through cellEdited - the parent writes
    // each value to the bin file but skips the refresh. We don't touch
    // QTableWidgetItem* directly here, so even if the parent decided
    // to repopulate the table mid-loop (it shouldn't, but defensively),
    // we'd survive.
    m_suppressEdits = true;
    for (const auto &rc : coords) {
        emit cellEdited(m_currentMap, m_currentInstance,
                        rc.first, rc.second, v);
    }
    m_suppressEdits = false;

    // End of bulk: parent now does one refresh, which will repaint with
    // the new diff highlights, status bar counts, etc.
    emit bulkEditEnd();
}

} // namespace EcuParser
