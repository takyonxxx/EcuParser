#include "MapTableWidget.h"

#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverNames.h"

#include <QAction>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cmath>

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
    const QString unitTitleSuffix = map->unit.isEmpty()
        ? QString()
        : QStringLiteral("  |  unit: %1 (raw * %2 + %3)")
              .arg(map->unit,
                   QString::number(map->scale, 'g', 4),
                   QString::number(map->offset, 'g', 4));
    m_titleLabel->setText(
        QStringLiteral("%1  |  %2 x %3  |  @ 0x%4%5%6")
            .arg(humanName)
            .arg(DriverNames::effectiveDimX(schemaId, *map))
            .arg(DriverNames::effectiveDimY(schemaId, *map))
            .arg(addr, 6, 16, QLatin1Char('0')).toUpper()
            .arg(effInstances > 1
                     ? QStringLiteral("  (instance %1/%2)")
                           .arg(instanceIndex + 1).arg(effInstances)
                     : QString())
            .arg(unitTitleSuffix));

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

    // Axis resolution priority:
    //   1. Embedded MapDefinition.xValues/yValues (filled by
    //      MainWindow::loadDriver from DriverNames overrides) - this is
    //      the canonical source and works for both DRT and XDF.
    //   2. DriverNames::axisXOverride/axisYOverride direct lookup - kept
    //      as a safety net in case a future call path constructs a
    //      MapDefinition without going through loadDriver's injection.
    //   3. Bin-embedded axis at the address in MapDefinition.axisX/axisY.
    //   4. For Y only: synthesised 0..100 Load% scale.
    QList<int> rowAxis;
    if (!map->xValues.isEmpty())
        rowAxis = map->xValues;
    if (rowAxis.isEmpty())
        rowAxis = DriverNames::axisXOverride(schemaId, *map);
    if (rowAxis.isEmpty())
        rowAxis = readAxisValues(primary, map->axisX, rowCount);

    QList<int> colAxis;
    if (!map->yValues.isEmpty())
        colAxis = map->yValues;
    if (colAxis.isEmpty())
        colAxis = DriverNames::axisYOverride(schemaId, *map);
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
    // Resolve physical-unit conversion once. When unit is empty,
    // hasUnit() is false and we skip every per-cell unit conversion.
    const double unitScale  = (m_currentMap ? m_currentMap->scale : 1.0);
    const double unitOffset = (m_currentMap ? m_currentMap->offset : 0.0);
    const QString unitText  = (m_currentMap ? m_currentMap->unit : QString());
    const bool hasUnit = !unitText.isEmpty();
    auto toPhysical = [&](qint64 raw) -> double {
        return double(raw) * unitScale + unitOffset;
    };
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
                    if (hasUnit) {
                        item->setToolTip(
                            QStringLiteral("Original: %1 (%2 %3)\nModified: %4 (%5 %6)\nDelta: %7")
                                .arg(ov).arg(QString::number(toPhysical(ov), 'f', 2), unitText)
                                .arg(v).arg(QString::number(toPhysical(v), 'f', 2), unitText)
                                .arg(qint64(v) - qint64(ov)));
                    } else {
                        item->setToolTip(
                            QStringLiteral("Original: %1\nModified: %2\nDelta: %3")
                                .arg(ov).arg(v).arg(qint64(v) - qint64(ov)));
                    }
                } else if (hasUnit) {
                    item->setToolTip(
                        QStringLiteral("%1 = %2 %3")
                            .arg(v).arg(QString::number(toPhysical(v), 'f', 2), unitText));
                }
            } else if (hasUnit) {
                item->setToolTip(
                    QStringLiteral("%1 = %2 %3")
                        .arg(v).arg(QString::number(toPhysical(v), 'f', 2), unitText));
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
    // When a physical unit is defined for this map, append a parenthetic
    // showing min/max/mean in real-world units. Editing remains raw u16,
    // but the user sees what 7500 -> 400 Nm means alongside the raw count.
    QString unitSuffix;
    if (hasUnit && !modData.cells.isEmpty()) {
        unitSuffix = QStringLiteral("   (%1..%2 %3, mean %4)")
                         .arg(QString::number(toPhysical(modData.minValue()), 'f', 2),
                              QString::number(toPhysical(modData.maxValue()), 'f', 2),
                              unitText,
                              QString::number(toPhysical(qint64(modData.meanValue())), 'f', 2));
    }
    m_statusLabel->setText(
        QStringLiteral("min=%1   max=%2   mean=%3   cells=%4%5%6")
            .arg(modData.minValue()).arg(modData.maxValue())
            .arg(QString::number(modData.meanValue(), 'f', 1))
            .arg(modData.cells.size())
            .arg(unitSuffix)
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
    QAction *smoothAct = menu.addAction(
        QStringLiteral("Smooth %1 cells (3x3 blur)...").arg(selected.size()));
    QAction *rampAct = menu.addAction(
        QStringLiteral("Linear ramp from boundary..."));
    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    // Snapshot the (row, col) pairs and current values BEFORE we touch
    // anything. We must not hold on to QTableWidgetItem pointers across
    // the loop: the parent reacts to cellEdited by repopulating the
    // table, which invalidates every item pointer. Indices are stable.
    struct CellSnap { int row; int col; int oldValue; };
    QVector<CellSnap> snaps;
    snaps.reserve(selected.size());
    int rowMin = INT_MAX, rowMax = INT_MIN, colMin = INT_MAX, colMax = INT_MIN;
    for (QTableWidgetItem *it : selected) {
        bool ok = false;
        const int v = it->text().toInt(&ok);
        snaps.append({it->row(), it->column(), ok ? v : 0});
        rowMin = std::min(rowMin, it->row());
        rowMax = std::max(rowMax, it->row());
        colMin = std::min(colMin, it->column());
        colMax = std::max(colMax, it->column());
    }

    if (chosen == setAct) {
        // === Set value: simple constant write to every selected cell ===
        // Use the first selected item's value as the dialog's initial value
        // so the user can tweak by a small delta rather than retyping.
        bool ok = false;
        const int v = QInputDialog::getInt(
            this, QStringLiteral("Set value for selection"),
            QStringLiteral("New value (0..65535) for %1 selected cells:")
                .arg(selected.size()),
            snaps.first().oldValue, 0, 65535, 1, &ok);
        if (!ok) return;

        emit bulkEditBegin();
        m_suppressEdits = true;
        for (const auto &s : snaps) {
            emit cellEdited(m_currentMap, m_currentInstance,
                            s.row, s.col, v);
        }
        m_suppressEdits = false;
        emit bulkEditEnd();
        return;
    }

    if (chosen == smoothAct) {
        // === 3x3 average blur, weighted by user-chosen strength ===
        // For each selected cell, the new value is:
        //   v_new = round(strength% * mean_3x3 + (1-strength%) * v_old)
        // Where mean_3x3 averages the cell + its 8 neighbours, clamped
        // to selection bounds. This removes "step edges" that stage
        // applies introduce on row_min boundaries (e.g. Stage 1's
        // injection map jumping from 0% to +18% between row 6 and 7).
        bool ok = false;
        const int strength = QInputDialog::getInt(
            this, QStringLiteral("Smooth selection"),
            QStringLiteral("Blur strength %% (0 = no change, 100 = pure 3x3 mean):"),
            50, 0, 100, 5, &ok);
        if (!ok || strength <= 0) return;

        // Build a position->oldValue lookup for the selection so the
        // 3x3 mean only averages over selected cells (cells outside the
        // selection are excluded - keeps the smoothing scoped).
        QHash<qint64, int> selValueAt;
        auto key = [](int r, int c) -> qint64 {
            return (qint64(r) << 32) | quint32(c);
        };
        for (const auto &s : snaps)
            selValueAt.insert(key(s.row, s.col), s.oldValue);

        const double w = strength / 100.0;
        emit bulkEditBegin();
        m_suppressEdits = true;
        for (const auto &s : snaps) {
            qint64 sum = 0;
            int cnt = 0;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    auto it = selValueAt.constFind(key(s.row + dr, s.col + dc));
                    if (it != selValueAt.constEnd()) {
                        sum += it.value();
                        ++cnt;
                    }
                }
            }
            const double mean = (cnt > 0) ? double(sum) / double(cnt) : s.oldValue;
            const int newV = int(std::round(w * mean + (1.0 - w) * s.oldValue));
            emit cellEdited(m_currentMap, m_currentInstance,
                            s.row, s.col, std::clamp(newV, 0, 65535));
        }
        m_suppressEdits = false;
        emit bulkEditEnd();
        return;
    }

    if (chosen == rampAct) {
        // === Linear ramp from boundary cell to interior ===
        // For each ROW in the selection, interpolate linearly from the
        // cell just OUTSIDE the selection's left edge (col = colMin-1)
        // to the cell at the selection's right edge (col = colMax). If
        // the left boundary is not present (selection touches col 0),
        // we ramp instead from selection right boundary out to
        // colMax+1. This is what tuners want when stage 1/2 created a
        // jump on a single column boundary - smooth across N cells.
        const bool haveLeftBoundary  = (colMin > 0);
        const bool haveRightBoundary = (colMax < m_table->columnCount() - 1);
        if (!haveLeftBoundary && !haveRightBoundary) {
            QMessageBox::information(
                this, QStringLiteral("Ramp"),
                QStringLiteral("Selection spans the full column range -\n"
                               "no boundary cell available to ramp from."));
            return;
        }

        // Read the boundary values (one per selected row) once, BEFORE
        // we start emitting edits (which would refresh the table from
        // under us in the non-bulk case).
        struct BoundaryVals { int row; int leftV; int rightV; };
        QVector<BoundaryVals> bvs;
        bvs.reserve(rowMax - rowMin + 1);
        for (int r = rowMin; r <= rowMax; ++r) {
            int leftV = 0, rightV = 0;
            if (haveLeftBoundary) {
                if (auto *it = m_table->item(r, colMin - 1))
                    leftV = it->text().toInt();
            }
            if (haveRightBoundary) {
                if (auto *it = m_table->item(r, colMax + 1))
                    rightV = it->text().toInt();
            }
            bvs.append({r, leftV, rightV});
        }

        // Selection->row map for row lookup, so we can grab boundary
        // values for the right row inside the snap loop.
        QHash<int, BoundaryVals> bvByRow;
        for (const auto &b : bvs) bvByRow.insert(b.row, b);

        emit bulkEditBegin();
        m_suppressEdits = true;
        const int span = colMax - colMin;
        for (const auto &s : snaps) {
            const auto bv = bvByRow.value(s.row, BoundaryVals{s.row, s.oldValue, s.oldValue});
            // Position within selection, 0..span. t in [0,1] across span.
            const double t = (span > 0) ? double(s.col - colMin) / double(span) : 0.0;
            // Choose endpoints: prefer real boundaries; if only one
            // side is bounded, the other endpoint is the selection's
            // own end-row value (snapshot's oldValue at colMax/colMin).
            int leftEnd  = haveLeftBoundary  ? bv.leftV  : s.oldValue;
            int rightEnd = haveRightBoundary ? bv.rightV : s.oldValue;
            const double newV = leftEnd + t * (rightEnd - leftEnd);
            emit cellEdited(m_currentMap, m_currentInstance,
                            s.row, s.col, int(std::round(newV)));
        }
        m_suppressEdits = false;
        emit bulkEditEnd();
        return;
    }
}

} // namespace EcuParser
