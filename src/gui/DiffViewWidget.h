#ifndef DIFFVIEWWIDGET_H
#define DIFFVIEWWIDGET_H

#include "../model/MapDefinition.h"

#include <QWidget>

class QTableWidget;
class QLabel;

namespace EcuParser {

class BinFile;
struct DriverModel;

// Bin-against-bin overview tab. Iterates every map in the loaded
// driver, reads the same byte region from both Original and Modified,
// and produces one row per map with:
//
//   map name | instance | cells | changed | %changed | mean Δ raw |
//   max Δ raw | range Δ raw | mean Δ unit | unit
//
// The "mean Δ unit" column converts mean Δ raw through the map's
// scale/offset so the user sees "+38 Nm" rather than just "+760 raw".
// Bottom of the widget shows totals.
//
// Double-click a row -> the parent forwards the (map, instance) into
// the tree's mapSelected signal so the user jumps to that map.
class DiffViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DiffViewWidget(QWidget *parent = nullptr);

    // Recompute the diff. Called whenever bin/driver/edit state
    // changes. Cheap enough to run on every refresh - 11 maps x ~250
    // cells each, single arithmetic pass.
    void refresh(const DriverModel *driver,
                 const BinFile     *origBin,
                 const BinFile     *modBin);

    void clear();

signals:
    // Emitted when the user double-clicks a row. Caller forwards into
    // the tree to highlight + load the target map.
    void mapActivated(const MapDefinition *map, int instance);

private slots:
    void onCellDoubleClicked(int row, int col);

private:
    QLabel       *m_summary = nullptr;
    QTableWidget *m_table   = nullptr;

    // Row -> (map pointer, instance) for double-click navigation. We
    // keep raw pointers because the DriverModel they reference outlives
    // any single refresh call; refresh() rebuilds this list.
    struct RowEntry {
        const MapDefinition *map = nullptr;
        int instance = 0;
    };
    QList<RowEntry> m_rowEntries;
};

} // namespace EcuParser

#endif // DIFFVIEWWIDGET_H
