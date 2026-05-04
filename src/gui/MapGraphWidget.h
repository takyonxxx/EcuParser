#ifndef MAPGRAPHWIDGET_H
#define MAPGRAPHWIDGET_H

#include "../model/MapDefinition.h"

#include <QWidget>
#include <QVector>

namespace EcuParser {

class BinFile;

// Line graph showing all cells of a map laid out in row-major order on a
// single X axis. Mimics the reference tool's "Edit EPROM (graph)" view (Image 3
// in the spec): original bin drawn in blue, modified bin in red, optional
// grid + value labels.
//
// We chose row-major flattening because that's exactly what reference does
// and it visually highlights repeated patterns (each row becomes a sawtooth
// when values are roughly monotonic per row).
class MapGraphWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapGraphWidget(QWidget *parent = nullptr);

    // Display the same map from two bins. Either may be nullptr; if both
    // are nullptr the widget is cleared. schemaId is used for canonical
    // the reference tool-reference naming in the title.
    void setMap(const MapDefinition *map,
                int instanceIndex,
                const QString &schemaId,
                const BinFile *originalBin,
                const BinFile *modifiedBin);

    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    const MapDefinition *m_map = nullptr;
    int m_instance = 0;
    QString m_schemaId;

    // Cached row-major value series for each bin. Empty when the
    // corresponding bin is missing.
    QVector<int> m_origValues;
    QVector<int> m_modValues;

    // Cached axis breakpoints from the bin (or from DriverNames overrides
    // / synthesised fallbacks). RPM = X axis (one entry per row of the
    // map), Load = Y axis (one entry per column). Cached at setMap() time
    // so the mouse tooltip can look them up without re-reading the bin.
    QList<int> m_rpmAxis;
    QList<int> m_loadAxis;

    // Cached min/max across both series for consistent Y scaling.
    int m_yMin = 0;
    int m_yMax = 0;

    // Last computed plot rectangle from paintEvent. Cached so mouseMove
    // can map cursor x to a cell index without re-computing layout.
    QRect m_plotRect;
    int m_lastCellCount = 0;

    // Hover state. m_hoverIndex is the row-major cell index under the
    // mouse, or -1 when the cursor is outside the plot.
    int m_hoverIndex = -1;
    QPoint m_hoverPos;

    // Padding around plot area in pixels.
    static constexpr int kMargin = 40;
};

} // namespace EcuParser

#endif // MAPGRAPHWIDGET_H
