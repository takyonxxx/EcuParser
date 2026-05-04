#ifndef MAPTABLEWIDGET_H
#define MAPTABLEWIDGET_H

#include "../model/MapDefinition.h"

#include <QWidget>
#include <QTableWidget>

class QLabel;

namespace Titanium {

class BinFile;

// Dual-bin map compare table. Mirrors the orientation used by ECM Titanium's
// "Edit map" window (Image 2 in the spec):
//
//   +----------+-----+-----+-----+-----+-----+
//   | RPM\Load |  6  | 13  | 19  | ... | 100 |   (column header = Load %)
//   +----------+-----+-----+-----+-----+-----+
//   |   500    | 3600| 3600| 3900| ... |
//   |   650    | 3350| 3350| 3650| ... |
//   |   ...                                  |
//   |   4400   | 2400| 2500| ...             |
//   +----------+--------------------+--------+
//   (rows = RPM = X axis breakpoints from bin)
//   (cols = Load %, implicit 0..100/dimY-1 when no Y axis address)
//
// In a normal Bosch driver dimX is RPM (rows in Titanium's display) and
// dimY is load (columns). The breakpoint table addresses are stored in the
// .drt file under axisX / axisY respectively.
class MapTableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapTableWidget(QWidget *parent = nullptr);

    // Show map values from up to two bins. The widget renders the modified
    // bin as the primary value and uses cell colouring to indicate
    // differences from the original. Either pointer may be null. schemaId
    // is used to look up the ECM Titanium canonical name for the title.
    void showMap(const BinFile *originalBin,
                 const BinFile *modifiedBin,
                 const QString &schemaId,
                 const MapDefinition *map,
                 int instanceIndex);

    void clearMap();

    const MapDefinition *currentMap() const { return m_currentMap; }
    int currentInstance() const { return m_currentInstance; }

signals:
    // Emitted when the user types a new value into a cell. Parent listens
    // and writes back into the modified BinFile, then refreshes.
    void cellEdited(const MapDefinition *map, int instanceIndex,
                    int row, int col, qint32 newValue);

    // Bulk-edit lifecycle for "Set value..." on a multi-cell selection.
    // Parent should disable per-cell refreshes between Begin and End,
    // because each refresh re-creates the table's QTableWidgetItem
    // pointers - and the bulk loop is iterating over those exact
    // pointers, so a refresh mid-loop produces dangling pointers and a
    // crash on the next iteration. Single-cell edits (no Begin/End)
    // continue to refresh normally.
    void bulkEditBegin();
    void bulkEditEnd();

private slots:
    void onItemChanged(QTableWidgetItem *item);

    // Right-click context menu on the table. Offers "Set value..." which
    // prompts for a single value and writes it to every selected cell -
    // the standard ECU-edit workflow when raising/lowering a region.
    void onTableContextMenu(const QPoint &pos);

private:
    QList<int> readAxisValues(const BinFile *bin,
                              const AxisDefinition &axis,
                              int count) const;

    // Synthesise a Load % axis when the map has no Y axis breakpoint table.
    // Matches the values Titanium puts above injection-at-part-throttle:
    // for dim=16 -> 6, 13, 19, 25, 31, 38, 44, 50, 56, 63, 69, 75, 81, 88,
    // 94, 100. Formula:  ceil( (i+1) * 100 / dim )
    static QList<int> synthesizeLoadAxis(int count);

    static QColor heatColour(int value, int lo, int hi);

    QLabel       *m_titleLabel  = nullptr;
    QLabel       *m_cornerLabel = nullptr;
    QTableWidget *m_table       = nullptr;
    QLabel       *m_statusLabel = nullptr;

    const MapDefinition *m_currentMap = nullptr;
    int m_currentInstance = 0;

    bool m_suppressEdits = false;
};

} // namespace Titanium

#endif // MAPTABLEWIDGET_H
