#ifndef SURFACE3DWIDGET_H
#define SURFACE3DWIDGET_H

// Lightweight 3D surface plot of a 2D map. Rendered with QPainter and
// a hand-rolled orthographic projection - no QtDataVisualization
// dependency. Both Original and Modified are drawn: Original as a
// translucent grey wireframe, Modified as a colour-graded filled
// surface. The user can pan/zoom/rotate via mouse:
//
//   left drag  : rotate about pitch + yaw
//   right drag : pan
//   wheel      : zoom
//
// This is a "supplementary view" - the Table tab is the primary edit
// surface. The 3D view's value is in spotting wave-like patterns,
// step-edges, and ridges that the flat table makes invisible.

#include "../model/MapDefinition.h"

#include <QWidget>

namespace EcuParser {

class BinFile;

class Surface3DWidget : public QWidget
{
    Q_OBJECT
public:
    explicit Surface3DWidget(QWidget *parent = nullptr);

    void setMap(const MapDefinition *map,
                int instanceIndex,
                const QString &schemaId,
                const BinFile *origBin,
                const BinFile *modBin);

    void clear();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    // Projection helper. Maps (gridX, gridY, value01) -> screen point.
    // value01 is the cell value normalised to 0..1 across the
    // current map's min/max range.
    struct Camera {
        double yawDeg   = -35.0;
        double pitchDeg =  28.0;
        double zoom     =  1.0;
        double panX     =  0.0;
        double panY     =  0.0;
    };
    QPointF project(double gx, double gy, double z01) const;
    void    drawGridFloor(QPainter &p) const;

    const MapDefinition *m_map = nullptr;
    int                  m_instance = 0;
    QString              m_schemaId;
    QList<int>           m_origCells;
    QList<int>           m_modCells;
    int                  m_rows  = 0;
    int                  m_cols  = 0;
    int                  m_minVal = 0;
    int                  m_maxVal = 0;

    Camera               m_cam;
    QPoint               m_lastMouse;
    Qt::MouseButton      m_dragButton = Qt::NoButton;
};

} // namespace EcuParser

#endif // SURFACE3DWIDGET_H
