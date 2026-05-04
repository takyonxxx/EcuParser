#ifndef DRIVERTREEWIDGET_H
#define DRIVERTREEWIDGET_H

#include "../model/DriverModel.h"

#include <QTreeWidget>

namespace Titanium {

// Tree view of a driver's maps grouped by category, mirroring ECM Titanium's
// "Available maps" pane on the right side of the main window.
//
// Emits mapSelected() with a pointer into the held DriverModel; the caller
// must not let the model outlive the tree (we do this by keeping the
// DriverModel as a member of MainWindow and rebuilding the tree on each
// driver swap).
class DriverTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit DriverTreeWidget(QWidget *parent = nullptr);

    // Replace contents with maps from the given driver. instanceIdx hints
    // which address replica to highlight when a map has multiple addresses
    // (currently unused; kept for future extension).
    void setDriver(const DriverModel *driver);

signals:
    // Fired when the user clicks on a leaf (map) item. addressIndex is the
    // index into MapDefinition::addresses for which sub-instance the user
    // picked; when the user clicks the parent map (not a sub-instance) the
    // index is 0.
    void mapSelected(const MapDefinition *map, int addressIndex);

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    const DriverModel *m_driver = nullptr;

    // Custom roles for QTreeWidgetItem data() so we can map clicks back to
    // the underlying MapDefinition without walking the model again.
    enum ItemRole {
        MapPtrRole       = Qt::UserRole + 1,  // QVariant<void*> -> const MapDefinition*
        AddressIndexRole = Qt::UserRole + 2,  // int (0 by default, 0..N for sub-instances)
    };
};

} // namespace Titanium

#endif // DRIVERTREEWIDGET_H
