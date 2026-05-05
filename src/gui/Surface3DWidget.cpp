#include "Surface3DWidget.h"

#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverNames.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace EcuParser {

Surface3DWidget::Surface3DWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(20, 30, 40));  // dark cyan-ish
    setPalette(p);
}

void Surface3DWidget::clear()
{
    m_map = nullptr;
    m_origCells.clear();
    m_modCells.clear();
    m_rows = m_cols = 0;
    m_minVal = m_maxVal = 0;
    update();
}

void Surface3DWidget::setMap(const MapDefinition *map,
                             int instanceIndex,
                             const QString &schemaId,
                             const BinFile *origBin,
                             const BinFile *modBin)
{
    if (!map || !modBin) {
        clear();
        return;
    }
    m_map      = map;
    m_instance = instanceIndex;
    m_schemaId = schemaId;
    const int dx = DriverNames::effectiveDimX(schemaId, *map);
    const int dy = DriverNames::effectiveDimY(schemaId, *map);
    if (dx <= 0 || dy <= 0) {
        clear();
        return;
    }
    const auto modData = readMapInstance(*modBin, *map, instanceIndex, dx, dy);
    if (modData.cells.isEmpty()) {
        clear();
        return;
    }
    m_modCells = modData.cells;
    m_rows = dx;
    m_cols = dy;
    m_minVal = modData.minValue();
    m_maxVal = modData.maxValue();
    if (origBin) {
        const auto origData = readMapInstance(*origBin, *map, instanceIndex, dx, dy);
        if (origData.cells.size() == modData.cells.size())
            m_origCells = origData.cells;
        // Expand value range to cover both bins so direct visual compare
        // works (without this, original peaks above modified peaks would
        // be clipped to >1 and rendered as max colour).
        if (!m_origCells.isEmpty()) {
            m_minVal = std::min(m_minVal, origData.minValue());
            m_maxVal = std::max(m_maxVal, origData.maxValue());
        }
    }
    if (m_maxVal == m_minVal) m_maxVal = m_minVal + 1; // avoid div0
    update();
}

QPointF Surface3DWidget::project(double gx, double gy, double z01) const
{
    // Centre the grid around (0,0). Grid extents are (cols-1) x (rows-1).
    const double xC = double(m_cols - 1) * 0.5;
    const double yC = double(m_rows - 1) * 0.5;
    const double X = (gx - xC);
    const double Y = (gy - yC);
    const double Z = (z01 - 0.5) * std::min(double(m_rows), double(m_cols));

    // Standard yaw-pitch ortho projection. Yaw rotates around vertical
    // (Z) axis, pitch tilts the view downward.
    const double yaw   = m_cam.yawDeg   * M_PI / 180.0;
    const double pitch = m_cam.pitchDeg * M_PI / 180.0;
    // Rotate around Z (yaw):
    const double X1 =  X * std::cos(yaw) - Y * std::sin(yaw);
    const double Y1 =  X * std::sin(yaw) + Y * std::cos(yaw);
    // Rotate around X' (pitch):
    const double Y2 =  Y1 * std::cos(pitch) - Z  * std::sin(pitch);
    // Z2 is the depth - we ignore it for ortho but could use for sort.
    // Project: simple orthographic; scale picks up window size.
    const double scale = std::min(width(), height()) * 0.4 * m_cam.zoom
                       / std::max(double(m_rows), double(m_cols));
    const double sx = width()  * 0.5 + (X1 * scale) + m_cam.panX;
    const double sy = height() * 0.5 - (Y2 * scale) + m_cam.panY;
    return {sx, sy};
}

void Surface3DWidget::drawGridFloor(QPainter &p) const
{
    p.setPen(QPen(QColor(60, 80, 95), 1.0));
    // Floor at z01=0. Draw row-major and column-major lines.
    for (int r = 0; r < m_rows; ++r) {
        const QPointF a = project(0,           r, 0.0);
        const QPointF b = project(m_cols - 1, r, 0.0);
        p.drawLine(a, b);
    }
    for (int c = 0; c < m_cols; ++c) {
        const QPointF a = project(c, 0,           0.0);
        const QPointF b = project(c, m_rows - 1, 0.0);
        p.drawLine(a, b);
    }
}

void Surface3DWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (!m_map || m_modCells.isEmpty() || m_rows <= 0 || m_cols <= 0) {
        p.setPen(QColor(180, 195, 210));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("(no map selected)"));
        return;
    }

    const double range = double(m_maxVal - m_minVal);
    auto z01 = [&](int v) -> double {
        return std::clamp((double(v) - m_minVal) / range, 0.0, 1.0);
    };

    // (Floor grid drawn below, after the projection helpers and the
    // 1D-aware effective dimensions are computed - so the floor matches
    // whatever surface we end up rendering above it.)

    // Helper to colour-grade a value 0..1 along a blue->cyan->yellow->red ramp.
    auto colourFor = [](double t) -> QColor {
        // 4 stops, linear interp between adjacent.
        struct S { double t; QColor c; };
        static const S stops[] = {
            {0.00, QColor( 30,  60, 180)},
            {0.33, QColor( 30, 200, 220)},
            {0.66, QColor(240, 220,  30)},
            {1.00, QColor(220,  60,  40)},
        };
        for (size_t i = 0; i + 1 < sizeof(stops)/sizeof(stops[0]); ++i) {
            if (t <= stops[i+1].t) {
                const double u = (t - stops[i].t) / (stops[i+1].t - stops[i].t);
                const QColor &a = stops[i].c, &b = stops[i+1].c;
                return QColor(int(a.red()  + (b.red()  - a.red())*u),
                              int(a.green()+ (b.green()- a.green())*u),
                              int(a.blue() + (b.blue() - a.blue()) *u));
            }
        }
        return stops[3].c;
    };

    // === 1D map support (e.g. torque limiter is 19x1) ===
    // Pure 1D maps would degenerate to a single line of dots in the
    // (rows-1, cols-1) double loop below - the user sees nothing. We
    // synthesise a "ribbon": draw the 1D vector twice along an
    // artificial second axis so the surface reads as a 1-cell-wide
    // strip with both Original wireframe and Modified colouring
    // visible. The ribbon width is hard-coded to ~1/4 of the long
    // axis - wide enough to read, narrow enough that it doesn't
    // pretend to be 2D data.
    const bool is1D = (m_rows == 1 || m_cols == 1);
    int eff_rows = m_rows;
    int eff_cols = m_cols;
    auto cellAt = [&](const QList<int> &cells, int r, int c) -> int {
        // For 1D maps we return the same value across the synthetic
        // axis - the "ribbon" has uniform height in its narrow
        // direction. For 2D maps this is just regular indexing.
        if (m_cols == 1) return cells.at(r);            // 19x1 -> 19xN
        if (m_rows == 1) return cells.at(c);            // 1xN  -> Nx<long>
        return cells.at(r * m_cols + c);
    };
    if (m_cols == 1) {
        // Torque limiter: 19 rows, 1 col -> stretch to 19 rows x 4 cols.
        eff_cols = 4;
    } else if (m_rows == 1) {
        eff_rows = 4;
    }

    // Modified surface: filled quads, colour-graded by value.
    // We iterate from far (high row) to near (low row) so painter's
    // overdraw produces an approximate depth sort. Same for columns.
    // For ortho with our yaw range this produces correct occlusion in
    // the common viewing angles.
    //
    // For 1D maps, the synthesised axis (eff_cols=4 or eff_rows=4)
    // makes the ribbon visible. cellAt() returns the same scalar
    // along the synthetic axis - this means the cell colour and
    // height are identical across the ribbon's width, which is
    // exactly the visual semantics of "this map only varies along
    // one axis".
    // We override project() inline for the 1D case - it's simpler
    // than wiring an extra parameter through. The pattern: for the
    // 1D-extended grid we centre on (eff_cols-1)/2, (eff_rows-1)/2.
    auto project1D = [&](double gx, double gy, double z01v) -> QPointF {
        const double xC = double(eff_cols - 1) * 0.5;
        const double yC = double(eff_rows - 1) * 0.5;
        const double X = (gx - xC);
        const double Y = (gy - yC);
        const double Z = (z01v - 0.5)
                       * std::min(double(eff_rows), double(eff_cols));
        const double yaw   = m_cam.yawDeg   * M_PI / 180.0;
        const double pitch = m_cam.pitchDeg * M_PI / 180.0;
        const double X1 =  X * std::cos(yaw) - Y * std::sin(yaw);
        const double Y1 =  X * std::sin(yaw) + Y * std::cos(yaw);
        const double Y2 =  Y1 * std::cos(pitch) - Z  * std::sin(pitch);
        const double scale = std::min(width(), height()) * 0.4 * m_cam.zoom
                           / std::max(double(eff_rows), double(eff_cols));
        const double sx = width()  * 0.5 + (X1 * scale) + m_cam.panX;
        const double sy = height() * 0.5 - (Y2 * scale) + m_cam.panY;
        return {sx, sy};
    };
    auto proj = [&](double gx, double gy, double z01v) -> QPointF {
        return is1D ? project1D(gx, gy, z01v)
                    : project(gx, gy, z01v);
    };

    // Floor grid (drawn first, surface and wireframe go on top).
    p.setPen(QPen(QColor(60, 80, 95), 1.0));
    for (int r = 0; r < eff_rows; ++r) {
        p.drawLine(proj(0,            r, 0.0),
                   proj(eff_cols - 1, r, 0.0));
    }
    for (int c = 0; c < eff_cols; ++c) {
        p.drawLine(proj(c, 0,            0.0),
                   proj(c, eff_rows - 1, 0.0));
    }

    for (int r = eff_rows - 2; r >= 0; --r) {
        for (int c = eff_cols - 2; c >= 0; --c) {
            const int v00 = cellAt(m_modCells, r,     c    );
            const int v10 = cellAt(m_modCells, r,     c + 1);
            const int v01 = cellAt(m_modCells, r + 1, c    );
            const int v11 = cellAt(m_modCells, r + 1, c + 1);
            const QPointF p00 = proj(c,     r,     z01(v00));
            const QPointF p10 = proj(c + 1, r,     z01(v10));
            const QPointF p01 = proj(c,     r + 1, z01(v01));
            const QPointF p11 = proj(c + 1, r + 1, z01(v11));
            const double tMid = z01((v00 + v10 + v01 + v11) / 4);
            QColor face = colourFor(tMid);
            face.setAlpha(220);
            p.setPen(QPen(QColor(20, 30, 40, 200), 0.6));
            p.setBrush(face);
            QPainterPath path;
            path.moveTo(p00);
            path.lineTo(p10);
            path.lineTo(p11);
            path.lineTo(p01);
            path.closeSubpath();
            p.drawPath(path);
        }
    }

    // Original: vivid cyan-blue wireframe overlay. Both row-major and
    // column-major lines so the grid reads as a proper mesh from any
    // viewing angle. Thicker stroke (1.6 px) and higher alpha (190)
    // make it stand out clearly above the filled Modified surface,
    // even where the colours overlap. The cool cyan-blue contrasts
    // with the warm yellow/orange of the Modified ramp - so the user
    // can tell at a glance "where did Modified push the surface up
    // vs where it stayed at stock".
    if (!m_origCells.isEmpty()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(40, 170, 240, 190), 1.6));
        // Row-major lines (along columns, for each row).
        for (int r = 0; r < eff_rows; ++r) {
            for (int c = 0; c < eff_cols - 1; ++c) {
                const int v0 = cellAt(m_origCells, r, c);
                const int v1 = cellAt(m_origCells, r, c + 1);
                p.drawLine(proj(c,     r, z01(v0)),
                           proj(c + 1, r, z01(v1)));
            }
        }
        // Column-major lines (along rows, for each column).
        for (int c = 0; c < eff_cols; ++c) {
            for (int r = 0; r < eff_rows - 1; ++r) {
                const int v0 = cellAt(m_origCells, r,     c);
                const int v1 = cellAt(m_origCells, r + 1, c);
                p.drawLine(proj(c, r,     z01(v0)),
                           proj(c, r + 1, z01(v1)));
            }
        }
    }

    // Axis legend: labels for X (cols), Y (rows), Z (value).
    p.setPen(QColor(180, 195, 210));
    p.drawText(20, 20,
        QStringLiteral("3D %1   rows=%2  cols=%3  range=%4..%5%6")
            .arg(DriverNames::displayName(m_schemaId, *m_map))
            .arg(m_rows).arg(m_cols).arg(m_minVal).arg(m_maxVal)
            .arg(m_origCells.isEmpty()
                     ? QString()
                     : QStringLiteral("   (Modified filled, Original wire)")));
    p.setPen(QColor(140, 160, 180));
    p.drawText(width() - 220, height() - 12,
        QStringLiteral("L-drag rotate  R-drag pan  wheel zoom"));
}

void Surface3DWidget::mousePressEvent(QMouseEvent *ev)
{
    m_lastMouse = ev->pos();
    m_dragButton = ev->button();
}

void Surface3DWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_dragButton == Qt::NoButton) return;
    const QPoint d = ev->pos() - m_lastMouse;
    if (m_dragButton == Qt::LeftButton) {
        m_cam.yawDeg   += d.x() * 0.5;
        m_cam.pitchDeg = std::clamp(m_cam.pitchDeg + d.y() * 0.4,
                                    -85.0, 85.0);
    } else if (m_dragButton == Qt::RightButton) {
        m_cam.panX += d.x();
        m_cam.panY += d.y();
    }
    m_lastMouse = ev->pos();
    update();
}

void Surface3DWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragButton = Qt::NoButton;
}

void Surface3DWidget::wheelEvent(QWheelEvent *ev)
{
    const int delta = ev->angleDelta().y();
    const double f = std::pow(1.0015, delta);  // smooth log zoom
    m_cam.zoom = std::clamp(m_cam.zoom * f, 0.2, 8.0);
    update();
}

} // namespace EcuParser
