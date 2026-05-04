#include "DriverTreeWidget.h"

#include "../model/DriverNames.h"

#include <algorithm>

namespace Titanium {

DriverTreeWidget::DriverTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderLabels(QStringList() << QStringLiteral("Available maps"));
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    connect(this, &QTreeWidget::itemClicked,
            this, &DriverTreeWidget::onItemClicked);
}

void DriverTreeWidget::setDriver(const DriverModel *driver)
{
    m_driver = driver;
    clear();
    if (!driver)
        return;

    auto *root = new QTreeWidgetItem(this);
    root->setText(0, driver->schemaId);
    root->setExpanded(true);

    // Group maps using DriverNames::effectiveCategory rather than the DRT
    // type code's first letter, because the .drt sometimes mis-buckets
    // (e.g. J293_822 stores turbo pressure under L*, torque limiter
    // under "?"). For unknown drivers we fall back to type-code dispatch.
    QHash<MapCategory, QList<const MapDefinition*>> byCat;
    for (const MapDefinition &m : driver->maps) {
        byCat[DriverNames::effectiveCategory(driver->schemaId, m)].append(&m);
    }

    // Iterate categories in a fixed display order (INJECTION first, OTHER
    // last) regardless of QHash internal ordering.
    const QList<MapCategory> catOrder {
        MapCategory::Injection,
        MapCategory::Turbo,
        MapCategory::Limiters,
        MapCategory::Timing,
        MapCategory::Other,
    };

    for (MapCategory cat : catOrder) {
        if (!byCat.contains(cat))
            continue;

        auto *catItem = new QTreeWidgetItem(root);
        catItem->setText(0, categoryDisplayName(cat));
        catItem->setExpanded(true);

        // Sort within the category by ECM Titanium tree order so the UI
        // matches Image 1 from the spec exactly.
        QList<const MapDefinition*> sorted = byCat.value(cat);
        std::sort(sorted.begin(), sorted.end(),
                  [&](const MapDefinition *a, const MapDefinition *b) {
                      return DriverNames::sortKey(driver->schemaId, *a)
                             < DriverNames::sortKey(driver->schemaId, *b);
                  });

        for (const MapDefinition *m : sorted) {
            const QString name = DriverNames::displayName(driver->schemaId, *m);

            // Honour DriverNames::maxInstances - some maps that the .drt
            // lists with multiple addresses are displayed by ECM Titanium
            // as a single instance. Clamp the displayed count accordingly.
            int displayedInstances = m->addresses.size();
            const int cap = DriverNames::maxInstances(driver->schemaId, *m);
            if (cap > 0)
                displayedInstances = std::min(displayedInstances, cap);

            const QString label = QStringLiteral("%1 (%2)")
                                      .arg(name)
                                      .arg(displayedInstances);
            auto *mapItem = new QTreeWidgetItem(catItem);
            mapItem->setText(0, label);
            mapItem->setData(0, MapPtrRole,
                             QVariant::fromValue(static_cast<const void*>(m)));
            mapItem->setData(0, AddressIndexRole, 0);

            // Only expose per-instance leaves when displayedInstances > 1.
            if (displayedInstances > 1) {
                for (int i = 0; i < displayedInstances; ++i) {
                    auto *sub = new QTreeWidgetItem(mapItem);
                    const quint32 addr = m->addresses.at(i);
                    sub->setText(0, QStringLiteral("instance %1 @ 0x%2")
                                        .arg(i + 1)
                                        .arg(addr, 6, 16, QLatin1Char('0')).toUpper());
                    sub->setData(0, MapPtrRole,
                                 QVariant::fromValue(static_cast<const void*>(m)));
                    sub->setData(0, AddressIndexRole, i);
                }
            }
        }
    }
}

void DriverTreeWidget::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!item)
        return;
    const QVariant ptrVar = item->data(0, MapPtrRole);
    if (!ptrVar.isValid())
        return;
    const auto *m = static_cast<const MapDefinition*>(ptrVar.value<const void*>());
    const int idx = item->data(0, AddressIndexRole).toInt();
    if (m)
        emit mapSelected(m, idx);
}

} // namespace Titanium
