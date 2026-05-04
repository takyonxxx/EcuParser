#include "DriverTreeWidget.h"

#include "../core/BinFile.h"
#include "../model/DriverNames.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <algorithm>
#include <functional>

namespace EcuParser {

// Tree colour for a map item whose Modified bytes differ from Original.
// Warm orange/amber stands out clearly against the dark sidebar without
// reading as an error or warning.
static const QColor kDiffMapColour(245, 165,  60);

DriverTreeWidget::DriverTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderLabels(QStringList() << QStringLiteral("Available maps"));
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    connect(this, &QTreeWidget::itemClicked,
            this, &DriverTreeWidget::onItemClicked);
}

void DriverTreeWidget::setBins(const BinFile *original, const BinFile *modified)
{
    m_origBin = original;
    m_modBin = modified;
    refreshDiffHighlights();
}

// Walk every map item in the tree, compare its bytes between Original
// and Modified, and recolour the item accordingly. Items whose addresses
// can't be resolved (e.g. category headers, root) are skipped.
void DriverTreeWidget::refreshDiffHighlights()
{
    if (!m_driver)
        return;

    // Default text colour - we restore the palette default by setting
    // an invalid QBrush (a default-constructed QBrush has Qt::NoBrush
    // and no colour, which Qt interprets as "use the palette default").
    const QBrush defaultBrush;

    // Recursive walker.
    std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem *item) {
        if (!item) return;

        const QVariant ptrVar = item->data(0, MapPtrRole);
        if (ptrVar.isValid()) {
            const auto *m = static_cast<const MapDefinition*>(
                ptrVar.value<const void*>());
            if (m && m_origBin && m_modBin) {
                const int effDX = DriverNames::effectiveDimX(
                    m_driver->schemaId, *m);
                const int effDY = DriverNames::effectiveDimY(
                    m_driver->schemaId, *m);
                const int cellSize = (m->cellSize > 0) ? m->cellSize : 2;
                const qsizetype len =
                    qsizetype(effDX) * qsizetype(effDY) * cellSize;

                // Honour maxInstances - extra .drt addresses we don't
                // display also shouldn't count toward the diff status.
                int instances = m->addresses.size();
                const int cap = DriverNames::maxInstances(
                    m_driver->schemaId, *m);
                if (cap > 0)
                    instances = std::min(instances, cap);

                bool anyDiff = false;
                for (int i = 0; i < instances && !anyDiff; ++i) {
                    const quint32 base = m->addresses.at(i);
                    const QByteArray a = m_origBin->readBytes(base, len);
                    const QByteArray b = m_modBin->readBytes(base, len);
                    if (a.size() == len && b.size() == len && a != b)
                        anyDiff = true;
                }

                QFont f = item->font(0);
                if (anyDiff) {
                    item->setForeground(0, QBrush(kDiffMapColour));
                    f.setBold(true);
                } else {
                    item->setForeground(0, defaultBrush);
                    f.setBold(false);
                }
                item->setFont(0, f);
            } else {
                // No bins loaded yet: clear any leftover highlighting
                // from a previous comparison.
                QFont f = item->font(0);
                f.setBold(false);
                item->setFont(0, f);
                item->setForeground(0, defaultBrush);
            }
        }

        for (int i = 0; i < item->childCount(); ++i)
            visit(item->child(i));
    };

    for (int i = 0; i < topLevelItemCount(); ++i)
        visit(topLevelItem(i));
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

        // Sort within the category by the reference tool tree order so the UI
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
            // lists with multiple addresses are displayed by the reference tool
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

    // Initial colour pass so any pre-loaded bins are reflected
    // immediately when the driver tree first appears.
    refreshDiffHighlights();
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

} // namespace EcuParser
