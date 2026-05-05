#include "MapGraphWidget.h"

#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverNames.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPaintEvent>
#include <algorithm>

namespace EcuParser {

// the reference tool graph colours (Image 3 from the spec):
//   - light cyan plot background (#a5dde9-ish)
//   - dotted dark grey grid
//   - thin solid blue line for Original
//   - thin solid red line for Modified (drawn ON TOP of original so any
//     unchanged stretches show purple-ish overlap; this is what the reference tool does)
namespace graph_colours {
    const QColor kBg            (165, 221, 233);   // the reference tool cyan
    const QColor kGrid          ( 90, 120, 130);
    const QColor kFrame         ( 30,  35,  40);
    const QColor kAxisText      ( 25,  30,  40);
    const QColor kTitleText     ( 25,  30,  40);
    const QColor kOrigLine      ( 25,  60, 200);   // the reference tool blue
    const QColor kModLine       (210,  35,  35);   // the reference tool red
    const QColor kLegendBg      (255, 255, 255, 200);
    // Cursor crosshair colour - dark for visibility against the cyan
    // background. Tooltip uses warm cream (matches the reference tool's tooltip box).
    const QColor kCursorLine    ( 30,  40,  60);
    const QColor kTooltipBg     (255, 246, 210);
    const QColor kTooltipBorder (140, 110,  60);
    const QColor kTooltipText   ( 25,  30,  40);
}

// File-local helpers that mirror MapTableWidget's axis logic, so the
// graph cursor can show the same RPM/Load values the table shows.
static QList<int> readAxisValuesLE(const BinFile *bin,
                                   const AxisDefinition &axis,
                                   int count)
{
    QList<int> out;
    if (!bin || !axis.isPresent() || count <= 0)
        return out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const quint32 off = axis.address + quint32(i * 2);
        bool ok = false;
        const quint16 v = bin->readU16LE(off, &ok);
        out.append(ok ? int(v) : 0);
    }
    return out;
}

static QList<int> synthLoadAxis(int count)
{
    QList<int> out;
    if (count <= 0) return out;
    out.reserve(count);
    for (int i = 0; i < count; ++i)
        out.append(int((double(i + 1) * 100.0 / double(count)) + 0.5));
    return out;
}

MapGraphWidget::MapGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setAutoFillBackground(true);
    // Enable mouseMoveEvent without requiring a button press, so the
    // hover crosshair tracks the cursor in real time (matches the reference tool
    // reference's "show RPM/Load/address at cursor" behaviour).
    setMouseTracking(true);
}

QSize MapGraphWidget::sizeHint() const
{
    return QSize(800, 400);
}

void MapGraphWidget::clear()
{
    m_map = nullptr;
    m_origValues.clear();
    m_modValues.clear();
    m_yMin = 0;
    m_yMax = 0;
    update();
}

static QVector<int> collectCells(const BinFile *bin, const MapDefinition &m,
                                 int inst, int dxOverride, int dyOverride)
{
    QVector<int> out;
    const int dx = (dxOverride > 0) ? dxOverride : m.dimX;
    const int dy = (dyOverride > 0) ? dyOverride : m.dimY;
    if (!bin || dx <= 0 || dy <= 0)
        return out;
    if (inst < 0 || inst >= m.addresses.size())
        return out;
    const MapData d = readMapInstance(*bin, m, inst, dx, dy);
    out.reserve(d.cells.size());
    for (qint32 v : d.cells)
        out.append(int(v));
    return out;
}

void MapGraphWidget::setMap(const MapDefinition *map,
                            int instanceIndex,
                            const QString &schemaId,
                            const BinFile *originalBin,
                            const BinFile *modifiedBin)
{
    m_map = map;
    m_instance = instanceIndex;
    m_schemaId = schemaId;

    // Apply DriverNames overrides for dimensions so 1D / 0x0-recorded maps
    // (e.g. torque limiter at 0x076D82, recorded as 0x0 in the .drt but
    // really 19x1) read correctly.
    int effDimX = 0, effDimY = 0;
    if (map) {
        effDimX = DriverNames::effectiveDimX(schemaId, *map);
        effDimY = DriverNames::effectiveDimY(schemaId, *map);
    }

    m_origValues = collectCells(originalBin, map ? *map : MapDefinition{},
                                instanceIndex, effDimX, effDimY);
    m_modValues  = collectCells(modifiedBin, map ? *map : MapDefinition{},
                                instanceIndex, effDimX, effDimY);

    // Cache RPM (axisX, rows) and Load (axisY, cols) breakpoint arrays.
    // Priority: MapDefinition.xValues/yValues (injected from
    // DriverNames in MainWindow::loadDriver) -> direct DriverNames
    // lookup -> bin read -> synthesised (Load only). Same logic the
    // table widget uses, kept in sync so the cursor tooltip shows the
    // same values the table does.
    m_rpmAxis.clear();
    m_loadAxis.clear();
    if (map) {
        const BinFile *primary = modifiedBin ? modifiedBin : originalBin;
        if (!map->xValues.isEmpty())
            m_rpmAxis = map->xValues;
        if (m_rpmAxis.isEmpty())
            m_rpmAxis = DriverNames::axisXOverride(schemaId, *map);
        if (m_rpmAxis.isEmpty())
            m_rpmAxis = readAxisValuesLE(primary, map->axisX, effDimX);
        if (!map->yValues.isEmpty())
            m_loadAxis = map->yValues;
        if (m_loadAxis.isEmpty())
            m_loadAxis = DriverNames::axisYOverride(schemaId, *map);
        if (m_loadAxis.isEmpty())
            m_loadAxis = readAxisValuesLE(primary, map->axisY, effDimY);
        if (m_loadAxis.isEmpty())
            m_loadAxis = synthLoadAxis(effDimY);
    }

    // Reset hover state so the previous map's tooltip doesn't linger.
    m_hoverIndex = -1;

    // Y axis range matches the reference tool: it always uses the full u16 range
    // (0..65535) regardless of the actual data span, so cell heights stay
    // consistent across maps and the user can compare scales between
    // different maps. The data values are u16 so this is the natural
    // domain. (We used to autoscale to [min,max] but that meant tiny
    // edits dominated the plot and you couldn't tell two maps' scales
    // apart visually.)
    m_yMin = 0;
    m_yMax = 65535;

    update();
}

void MapGraphWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), graph_colours::kBg);

    if (!m_map || (m_origValues.isEmpty() && m_modValues.isEmpty())) {
        p.setPen(graph_colours::kAxisText);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("(no map / no data)"));
        return;
    }

    const int W = width();
    const int H = height();
    // Margins: extra room on the bottom for two-line X axis (hex addresses
    // on top tick row, then a small "address - N cells / M rows" caption).
    const int plotL = kMargin + 18;
    const int plotR = W - (kMargin + 18);
    const int plotT = kMargin / 2 + 22;
    const int plotB = H - (kMargin + 18);

    if (plotR <= plotL || plotB <= plotT)
        return;

    // Cache the plot rectangle and cell count so mouseMoveEvent can map
    // (cursor x) -> (cell index) without reproducing the layout maths.
    m_plotRect = QRect(plotL, plotT, plotR - plotL, plotB - plotT);

    const int n = std::max(m_origValues.size(), m_modValues.size());
    m_lastCellCount = n;
    // For 1D maps (single cell or single row) we still want a visible line
    // so spread the points across the plot width. With n=1 we draw a flat
    // mark; with n>=2 we use the standard cell-spacing.
    const double dx = (n > 1)
                          ? double(plotR - plotL) / double(n - 1)
                          : double(plotR - plotL);

    const double yRange = double(m_yMax - m_yMin);
    auto valueToY = [&](int v) {
        const double t = double(v - m_yMin) / yRange;
        return plotB - t * double(plotB - plotT);
    };

    // === Title (the reference tool uses canonical map name) ===
    p.setPen(graph_colours::kTitleText);
    QFont titleFont = p.font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    if (!m_map->addresses.isEmpty()) {
        const quint32 a = m_map->addresses.at(
            std::min(m_instance, int(m_map->addresses.size()) - 1));
        const QString name = DriverNames::displayName(m_schemaId, *m_map);
        const int effDX = DriverNames::effectiveDimX(m_schemaId, *m_map);
        const int effDY = DriverNames::effectiveDimY(m_schemaId, *m_map);
        p.drawText(QRectF(0, 4, W, 18), Qt::AlignCenter,
                   QStringLiteral("%1  -  %2x%3  -  0x%4")
                       .arg(name)
                       .arg(effDX).arg(effDY)
                       .arg(a, 6, 16, QLatin1Char('0')).toUpper());
    }

    // Smaller font for axes/legend.
    QFont small = p.font();
    small.setBold(false);
    small.setPointSizeF(small.pointSizeF() * 0.85);
    p.setFont(small);

    // === Grid: 5 horizontal lines, 1 line per row of the map ===
    p.setPen(QPen(graph_colours::kGrid, 1, Qt::DotLine));
    for (int i = 0; i <= 5; ++i) {
        const double y = plotT + double(plotB - plotT) * i / 5.0;
        p.drawLine(QPointF(plotL, y), QPointF(plotR, y));
    }
    const int effDY = DriverNames::effectiveDimY(m_schemaId, *m_map);
    if (effDY > 0) {
        for (int i = 0; i <= effDY; ++i) {
            const double x = plotL + double(plotR - plotL) * i / double(effDY);
            p.drawLine(QPointF(x, plotT), QPointF(x, plotB));
        }
    }

    // === Y axis labels (the reference tool puts them on BOTH sides) ===
    p.setPen(graph_colours::kAxisText);
    for (int i = 0; i <= 5; ++i) {
        const double y = plotT + double(plotB - plotT) * i / 5.0;
        const int v = int(m_yMax - yRange * i / 5.0);
        const QString lbl = QString::number(v);
        // Left side
        p.drawText(QRectF(0, y - 8, plotL - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
        // Right side
        p.drawText(QRectF(plotR + 4, y - 8, W - plotR - 4, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, lbl);
    }

    // === X axis labels: hex addresses at 10 evenly-spaced ticks. Each
    // cell occupies cellSize bytes (typically 2), so the address at cell
    // index i is base + i*cellSize. the reference tool shows these in its "Go
    // to ..." box and cursor tooltip; we put them along the X axis so
    // they're visible at a glance. We also draw small tick marks above
    // the labels so each label clearly maps to a position on the plot.
    if (!m_map->addresses.isEmpty() && n > 0) {
        const quint32 base = m_map->addresses.at(
            std::min(m_instance, int(m_map->addresses.size()) - 1));
        const int cellSize = (m_map->cellSize > 0) ? m_map->cellSize : 2;
        const int targetTicks = 10;
        const int step = std::max(1, n / targetTicks);

        // Loop ticks - skip any that would land within ~50 px of the
        // rightmost edge so they don't collide with the always-shown
        // last-address label below.
        const double rightExclusion = 50.0;
        for (int i = 0; i < n; i += step) {
            const double x = plotL + dx * i;
            if (x > plotR - rightExclusion)
                break;
            const quint32 a = base + quint32(i * cellSize);
            const QString lbl = QStringLiteral("0x%1")
                                    .arg(a, 5, 16, QLatin1Char('0')).toUpper();
            // Tick mark
            p.setPen(QPen(graph_colours::kFrame, 1));
            p.drawLine(QPointF(x, plotB), QPointF(x, plotB + 3));
            // Label
            p.setPen(graph_colours::kAxisText);
            p.drawText(QRectF(x - 30, plotB + 4, 60, 14),
                       Qt::AlignCenter, lbl);
        }
        // Always show the last address as the rightmost label.
        {
            const double x = plotR;
            const quint32 a = base + quint32((n - 1) * cellSize);
            const QString lbl = QStringLiteral("0x%1")
                                    .arg(a, 5, 16, QLatin1Char('0')).toUpper();
            p.setPen(QPen(graph_colours::kFrame, 1));
            p.drawLine(QPointF(x, plotB), QPointF(x, plotB + 3));
            p.setPen(graph_colours::kAxisText);
            p.drawText(QRectF(x - 60, plotB + 4, 60, 14),
                       Qt::AlignRight | Qt::AlignVCenter, lbl);
        }
    }
    // Caption under the axis: small explanatory line.
    p.setPen(graph_colours::kAxisText);
    p.drawText(QRectF(plotL, plotB + 20, plotR - plotL, 14),
               Qt::AlignCenter,
               QStringLiteral("address  -  %1 cells / %2 rows")
                   .arg(n).arg(effDY));

    // === Frame ===
    p.setPen(QPen(graph_colours::kFrame, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(plotL, plotT, plotR - plotL, plotB - plotT));

    // === Plot z-order: Modified (red) UNDER, Original (blue) ON TOP.
    // the reference tool does the same. With this order:
    //   - if orig == mod, the user sees a clean blue line and the
    //     "(Original = Modified)" hint above tells them why
    //   - if they differ, the modified line peeks out underneath wherever
    //     it's higher or lower than the original
    // We use opaque pens (no alpha) and 1px cosmetic lines so things
    // render crisply even at high zoom levels - matches the reference tool's behaviour.
    auto plotSeries = [&](const QVector<int> &series, const QColor &c, int width) {
        if (series.isEmpty())
            return;
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(c);
        pen.setWidth(width);
        pen.setCosmetic(true);
        p.setPen(pen);

        if (series.size() == 1) {
            // 1D special-case: draw a horizontal mark at the value so the
            // user sees something meaningful even for single-cell maps.
            const double y = valueToY(series.first());
            p.drawLine(QPointF(plotL, y), QPointF(plotR, y));
        } else {
            QPointF prev(plotL, valueToY(series.first()));
            for (int i = 1; i < series.size(); ++i) {
                QPointF cur(plotL + dx * i, valueToY(series.at(i)));
                p.drawLine(prev, cur);
                prev = cur;
            }
        }
        p.setRenderHint(QPainter::Antialiasing, true);
    };
    // Modified first (drawn underneath, so it shows where it differs from
    // original), then Original on top.
    plotSeries(m_modValues,  graph_colours::kModLine,  1);
    plotSeries(m_origValues, graph_colours::kOrigLine, 1);

    // === Legend ===
    const int lw = 110;
    const int lh = 38;
    const int lx = plotR - lw - 6;
    const int ly = plotT + 6;
    p.setPen(QPen(graph_colours::kFrame, 1));
    p.setBrush(QBrush(graph_colours::kLegendBg));
    p.drawRect(QRectF(lx, ly, lw, lh));

    p.setPen(QPen(graph_colours::kOrigLine, 1));
    p.drawLine(lx + 6, ly + 12, lx + 28, ly + 12);
    p.setPen(graph_colours::kAxisText);
    p.drawText(QPointF(lx + 34, ly + 16), QStringLiteral("Original"));

    p.setPen(QPen(graph_colours::kModLine, 1));
    p.drawLine(lx + 6, ly + 28, lx + 28, ly + 28);
    p.setPen(graph_colours::kAxisText);
    p.drawText(QPointF(lx + 34, ly + 32), QStringLiteral("Modified"));

    // === "No differences" hint when the two series are identical ===
    // Without this the user can't tell whether the red is hidden behind
    // the blue or whether the modified bin happens to be identical.
    if (!m_origValues.isEmpty() && !m_modValues.isEmpty()
        && m_origValues == m_modValues) {
        p.setPen(QColor(120, 50, 50));
        QFont f = p.font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(plotL, plotT + 6, plotR - plotL, 18),
                   Qt::AlignCenter,
                   QStringLiteral("(Original = Modified for this map)"));
    }

    // === Hover crosshair + tooltip (the reference tool-style "RPM / Load / address") ===
    // Snapped to the nearest cell so the values shown match exactly what
    // the user would see in the Table view at that cell. We draw the
    // crosshair on top of the plot lines but UNDER the legend, so the
    // legend stays readable; tooltip goes on top of everything.
    if (m_hoverIndex >= 0 && m_hoverIndex < n) {
        // Snap x to the cell index (so the line lands on a real data
        // point rather than between two interpolated samples).
        const double cellX = plotL + dx * m_hoverIndex;

        // Vertical crosshair across the plot area.
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen cur(graph_colours::kCursorLine);
        cur.setWidth(1);
        cur.setCosmetic(true);
        p.setPen(cur);
        p.drawLine(QPointF(cellX, plotT), QPointF(cellX, plotB));
        p.setRenderHint(QPainter::Antialiasing, true);

        // Resolve cell into RPM (row), Load (col) and bin address.
        // dimX is rows (RPM, axisX), dimY is cols (Load, axisY); cells
        // are stored row-major so:
        //   row = idx / dimY,  col = idx % dimY
        const int dY = std::max(1, DriverNames::effectiveDimY(m_schemaId, *m_map));
        const int row = m_hoverIndex / dY;
        const int col = m_hoverIndex % dY;

        const int rpm  = (row < m_rpmAxis.size())  ? m_rpmAxis.at(row)  : -1;
        const int load = (col < m_loadAxis.size()) ? m_loadAxis.at(col) : -1;

        quint32 cellAddr = 0;
        const int cellSize = (m_map->cellSize > 0) ? m_map->cellSize : 2;
        if (!m_map->addresses.isEmpty()) {
            const quint32 base = m_map->addresses.at(
                std::min(m_instance, int(m_map->addresses.size()) - 1));
            cellAddr = base + quint32(m_hoverIndex * cellSize);
        }

        const int origV = (m_hoverIndex < m_origValues.size())
                              ? m_origValues.at(m_hoverIndex) : -1;
        const int modV  = (m_hoverIndex < m_modValues.size())
                              ? m_modValues.at(m_hoverIndex) : -1;

        // Build tooltip text. Each line shows one piece of info; we
        // include only the lines that have meaningful data. The label
        // stays "RPM" / "Load" even when the driver doesn't supply a
        // breakpoint axis - the user always thinks in those terms; the
        // raw row/col index is what we fall back to as the value.
        QStringList lines;
        if (rpm >= 0)
            lines << QStringLiteral("RPM:     %1").arg(rpm);
        else
            lines << QStringLiteral("RPM:     %1").arg(row);
        if (col < m_loadAxis.size())
            lines << QStringLiteral("Load:    %1").arg(load);
        else
            lines << QStringLiteral("Load:    %1").arg(col);
        lines << QStringLiteral("Addr:    0x%1")
                     .arg(cellAddr, 5, 16, QLatin1Char('0')).toUpper();
        if (origV >= 0 && modV >= 0 && origV != modV)
            lines << QStringLiteral("Ori:     %1").arg(origV)
                  << QStringLiteral("Mod:     %1  (%2%3)")
                         .arg(modV)
                         .arg(modV > origV ? QStringLiteral("+") : QString())
                         .arg(modV - origV);
        else if (origV >= 0)
            lines << QStringLiteral("Value:   %1").arg(origV);
        else if (modV >= 0)
            lines << QStringLiteral("Value:   %1").arg(modV);

        // Compute tooltip box size from text metrics. Use a slightly
        // larger font than the axis labels so RPM/Load/value are easy to
        // read at a glance without zooming.
        QFont tipFont = p.font();
        tipFont.setPointSizeF(tipFont.pointSizeF() * 1.15);
        tipFont.setBold(false);
        p.setFont(tipFont);
        const QFontMetrics fm = p.fontMetrics();
        int textW = 0;
        for (const QString &ln : lines)
            textW = std::max(textW, fm.horizontalAdvance(ln));
        const int padX = 10, padY = 8;
        const int boxW = textW + padX * 2;
        const int boxH = fm.lineSpacing() * lines.size() + padY * 2;

        // Position: prefer to the right of the cursor; if it would clip
        // the right edge, flip to the left. Same logic for top/bottom.
        int boxX = int(cellX) + 12;
        if (boxX + boxW > plotR - 4)
            boxX = int(cellX) - 12 - boxW;
        boxX = std::max(plotL + 2, std::min(plotR - boxW - 2, boxX));

        int boxY = m_hoverPos.y() + 12;
        if (boxY + boxH > plotB - 4)
            boxY = m_hoverPos.y() - 12 - boxH;
        boxY = std::max(plotT + 2, std::min(plotB - boxH - 2, boxY));

        // Draw tooltip box.
        QRectF box(boxX, boxY, boxW, boxH);
        p.setPen(QPen(graph_colours::kTooltipBorder, 1));
        p.setBrush(QBrush(graph_colours::kTooltipBg));
        p.drawRect(box);
        p.setPen(graph_colours::kTooltipText);
        for (int i = 0; i < lines.size(); ++i) {
            const QString &ln = lines.at(i);
            p.drawText(QPointF(boxX + padX,
                               boxY + padY + fm.ascent() + i * fm.lineSpacing()),
                       ln);
        }
    }
}

void MapGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_map || m_lastCellCount <= 0 || m_plotRect.isEmpty()) {
        m_hoverIndex = -1;
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = event->pos();
    m_hoverPos = pos;

    // Outside the plot rectangle? Hide the tooltip.
    if (!m_plotRect.contains(pos)) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Map cursor x to nearest cell index. With n cells stretched across
    // [plotL, plotR], dx = (plotR-plotL)/(n-1) for n>=2; n==1 is a flat
    // line that maps everything to index 0.
    const int n = m_lastCellCount;
    const int plotL = m_plotRect.left();
    const int plotR = m_plotRect.right();
    int newIdx = 0;
    if (n >= 2) {
        const double dx = double(plotR - plotL) / double(n - 1);
        newIdx = int(std::round(double(pos.x() - plotL) / dx));
        newIdx = std::max(0, std::min(n - 1, newIdx));
    }
    if (newIdx != m_hoverIndex) {
        m_hoverIndex = newIdx;
        update();
    } else {
        // Tooltip box position depends on cursor y, so still repaint.
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void MapGraphWidget::leaveEvent(QEvent *event)
{
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace EcuParser
