#ifndef DRIVERTREEWIDGET_H
#define DRIVERTREEWIDGET_H

#include "../model/DriverModel.h"

#include <QTreeWidget>

namespace EcuParser {

class BinFile;

// Tree view of a driver's maps grouped by category, mirroring the reference tool's
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

    // Provide the pair of bins the table/graph views are showing so each
    // map item can render in a different colour when its cells differ
    // between Original and Modified. Pass nullptrs to suppress
    // colouring (e.g. before any bin is loaded). Triggers an
    // immediate re-colour of every map item.
    void setBins(const BinFile *original, const BinFile *modified);

    // Re-evaluate every map's diff status and update item colours. Call
    // after edits so the tree highlight follows the user's changes.
    void refreshDiffHighlights();

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
    const BinFile *m_origBin = nullptr;
    const BinFile *m_modBin = nullptr;

    // Custom roles for QTreeWidgetItem data() so we can map clicks back to
    // the underlying MapDefinition without walking the model again.
    enum ItemRole {
        MapPtrRole       = Qt::UserRole + 1,  // QVariant<void*> -> const MapDefinition*
        AddressIndexRole = Qt::UserRole + 2,  // int (0 by default, 0..N for sub-instances)
    };
};

} // namespace EcuParser

#endif // DRIVERTREEWIDGET_H
