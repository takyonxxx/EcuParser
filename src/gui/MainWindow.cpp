#include "MainWindow.h"

#include "AppPaths.h"
#include "DriverTreeWidget.h"
#include "../model/DriverNames.h"
#include "MapGraphWidget.h"
#include "MapTableWidget.h"
#include "../core/DrtParser.h"
#include "../core/MapData.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace Titanium {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("EcuParser"));
    resize(1280, 760);
    buildUi();
    populateDataCombos();
}

void MainWindow::buildUi()
{
    // ====== Top toolbar ======
    auto *bar = addToolBar(QStringLiteral("main"));
    bar->setMovable(false);
    bar->setIconSize(QSize(16, 16));

    bar->addWidget(new QLabel(QStringLiteral("  Driver: "), this));
    m_driverCombo = new QComboBox(this);
    m_driverCombo->setMinimumWidth(220);
    bar->addWidget(m_driverCombo);
    auto *browseDrvBtn = new QPushButton(QStringLiteral("..."), this);
    browseDrvBtn->setFixedWidth(28);
    browseDrvBtn->setToolTip(QStringLiteral("Browse for a .drt file"));
    bar->addWidget(browseDrvBtn);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral("  Original: "), this));
    m_origBinCombo = new QComboBox(this);
    m_origBinCombo->setMinimumWidth(220);
    bar->addWidget(m_origBinCombo);
    auto *browseOrigBtn = new QPushButton(QStringLiteral("..."), this);
    browseOrigBtn->setFixedWidth(28);
    bar->addWidget(browseOrigBtn);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral("  Modified: "), this));
    m_modBinCombo = new QComboBox(this);
    m_modBinCombo->setMinimumWidth(220);
    bar->addWidget(m_modBinCombo);
    auto *browseModBtn = new QPushButton(QStringLiteral("..."), this);
    browseModBtn->setFixedWidth(28);
    bar->addWidget(browseModBtn);

    bar->addSeparator();
    m_copyOriBtn = new QPushButton(QStringLiteral("Copy ORI -> MOD"), this);
    m_copyOriBtn->setToolTip(
        QStringLiteral("Copy original values of the selected map into the modified bin"));
    bar->addWidget(m_copyOriBtn);

    m_exportBtn = new QPushButton(QStringLiteral("Export modified..."), this);
    m_exportBtn->setToolTip(QStringLiteral("Save the modified bin to disk"));
    bar->addWidget(m_exportBtn);

    // ====== Central area ======
    auto *central = new QWidget(this);
    auto *vlay = new QVBoxLayout(central);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(0);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setText(QStringLiteral("(no driver loaded)"));
    m_summaryLabel->setMargin(6);
    m_summaryLabel->setObjectName(QStringLiteral("summaryLabel"));
    vlay->addWidget(m_summaryLabel);

    auto *split = new QSplitter(Qt::Horizontal, this);
    m_tree      = new DriverTreeWidget(split);
    m_tabs      = new QTabWidget(split);
    m_tableView = new MapTableWidget(m_tabs);
    m_graphView = new MapGraphWidget(m_tabs);
    m_tabs->addTab(m_tableView, QStringLiteral("Table"));
    m_tabs->addTab(m_graphView, QStringLiteral("Graph"));
    split->addWidget(m_tree);
    split->addWidget(m_tabs);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes(QList<int>() << 280 << 1000);
    vlay->addWidget(split, 1);

    setCentralWidget(central);

    // ====== Status bar ======
    statusBar()->showMessage(QStringLiteral("Ready"));

    // ====== Wiring ======
    connect(m_driverCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDriverComboChanged);
    connect(m_origBinCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onOriginalBinComboChanged);
    connect(m_modBinCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModifiedBinComboChanged);
    connect(m_tree, &DriverTreeWidget::mapSelected,
            this, &MainWindow::onMapSelected);
    connect(m_copyOriBtn, &QPushButton::clicked,
            this, &MainWindow::onCopyOriginalToModified);
    connect(m_exportBtn, &QPushButton::clicked,
            this, &MainWindow::onExportModifiedBin);
    connect(m_tableView, &MapTableWidget::cellEdited,
            this, &MainWindow::onCellEdited);
    connect(m_tableView, &MapTableWidget::bulkEditBegin,
            this, &MainWindow::onBulkEditBegin);
    connect(m_tableView, &MapTableWidget::bulkEditEnd,
            this, &MainWindow::onBulkEditEnd);
    connect(browseDrvBtn, &QPushButton::clicked,
            this, &MainWindow::onBrowseDriver);
    connect(browseOrigBtn, &QPushButton::clicked,
            this, &MainWindow::onBrowseOriginalBin);
    connect(browseModBtn, &QPushButton::clicked,
            this, &MainWindow::onBrowseModifiedBin);
}

void MainWindow::populateDataCombos()
{
    // Each combo starts with a "(none)" placeholder so the user begins
    // with nothing loaded - they pick a driver, then an Original bin,
    // and at that point Modified is auto-mirrored to the same path
    // (matching ECM Titanium's "Driver" workflow). Subsequent edits go
    // into Modified; saving exports a renamed copy.
    auto fillBinCombo = [](QComboBox *cb, const QStringList &paths) {
        cb->blockSignals(true);
        cb->clear();
        cb->addItem(QStringLiteral("(none)"), QString());
        for (const QString &p : paths)
            cb->addItem(QFileInfo(p).fileName(), p);
        cb->blockSignals(false);
        cb->setCurrentIndex(0);
    };

    m_driverCombo->blockSignals(true);
    m_driverCombo->clear();
    m_driverCombo->addItem(QStringLiteral("(none)"), QString());
    for (const QString &p : AppPaths::listDrivers())
        m_driverCombo->addItem(QFileInfo(p).fileName(), p);
    m_driverCombo->blockSignals(false);
    m_driverCombo->setCurrentIndex(0);

    const QStringList bins = AppPaths::listBins();
    fillBinCombo(m_origBinCombo, bins);
    fillBinCombo(m_modBinCombo, bins);
}

void MainWindow::onDriverComboChanged(int index)
{
    if (index < 0)
        return;
    const QString path = m_driverCombo->itemData(index).toString();
    if (!path.isEmpty())
        loadDriver(path);
}

void MainWindow::onOriginalBinComboChanged(int index)
{
    if (index < 0) return;
    const QString p = m_origBinCombo->itemData(index).toString();
    if (p.isEmpty()) return;
    if (!loadOriginalBin(p))
        return;

    // ECM Titanium-style "Driver" workflow: when Original is loaded and
    // Modified is currently empty, auto-mirror Modified to the same
    // file. The two BinFile instances are SEPARATE in memory so edits
    // to Modified don't bleed into Original. The user can swap Modified
    // out via the combo or "..." button afterwards.
    if (!m_modBin) {
        // Find this same path in the modified combo (it should exist
        // because both combos are populated from the same listBins()
        // result; if not, skip silently).
        const int modIdx = m_modBinCombo->findData(p);
        if (modIdx >= 0) {
            m_modBinCombo->blockSignals(true);
            m_modBinCombo->setCurrentIndex(modIdx);
            m_modBinCombo->blockSignals(false);
            loadModifiedBin(p);
        }
    }
}

void MainWindow::onModifiedBinComboChanged(int index)
{
    if (index < 0) return;
    const QString p = m_modBinCombo->itemData(index).toString();
    if (!p.isEmpty()) loadModifiedBin(p);
}

bool MainWindow::loadDriver(const QString &path)
{
    QString err;
    auto parsed = DrtParser::parseFile(path, &err);
    if (!parsed) {
        QMessageBox::warning(this, QStringLiteral("Driver load"),
                             QStringLiteral("Failed to parse %1:\n%2").arg(path, err));
        return false;
    }
    m_driver = std::make_unique<DriverModel>(std::move(*parsed));
    m_tree->setDriver(m_driver.get());
    m_tableView->clearMap();
    m_graphView->clear();
    refreshTitle();
    statusBar()->showMessage(
        QStringLiteral("Driver loaded: %1 (%2 maps)")
            .arg(QFileInfo(path).fileName())
            .arg(m_driver->maps.size()),
        4000);
    return true;
}

bool MainWindow::loadOriginalBin(const QString &path)
{
    auto bin = std::make_unique<BinFile>();
    QString err;
    if (!bin->loadFile(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Original bin load"),
                             QStringLiteral("Failed: %1").arg(err));
        return false;
    }
    m_origBin = std::move(bin);
    statusBar()->showMessage(
        QStringLiteral("Original bin: %1 (%2 bytes)")
            .arg(QFileInfo(path).fileName())
            .arg(m_origBin->size()),
        4000);
    refreshTitle();
    refreshCurrentMap();
    return true;
}

bool MainWindow::loadModifiedBin(const QString &path)
{
    auto bin = std::make_unique<BinFile>();
    QString err;
    if (!bin->loadFile(path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Modified bin load"),
                             QStringLiteral("Failed: %1").arg(err));
        return false;
    }
    m_modBin = std::move(bin);
    m_modBinPath = path;
    m_modDirty = false;
    statusBar()->showMessage(
        QStringLiteral("Modified bin: %1 (%2 bytes)")
            .arg(QFileInfo(path).fileName())
            .arg(m_modBin->size()),
        4000);
    refreshTitle();
    refreshCurrentMap();
    return true;
}

void MainWindow::onMapSelected(const MapDefinition *map, int addressIndex)
{
    if (!map)
        return;
    const QString schema = m_driver ? m_driver->schemaId : QString();
    m_tableView->showMap(m_origBin.get(), m_modBin.get(),
                         schema, map, addressIndex);
    m_graphView->setMap(map, addressIndex,
                        schema, m_origBin.get(), m_modBin.get());
}

void MainWindow::refreshCurrentMap()
{
    const MapDefinition *m = m_tableView->currentMap();
    if (!m)
        return;
    onMapSelected(m, m_tableView->currentInstance());
}

void MainWindow::onCellEdited(const MapDefinition *map, int instanceIndex,
                              int row, int col, qint32 newValue)
{
    if (!map || !m_modBin)
        return;
    if (instanceIndex < 0 || instanceIndex >= map->addresses.size())
        return;
    if (row < 0 || col < 0)
        return;

    // Translate (row,col) back into the underlying byte offset. We use
    // the EFFECTIVE dimensions (DriverNames overrides) for both the
    // stride and the bound check, because some maps - the torque
    // limiter at 0x076D82 in particular - have 0x0 dimensions in the
    // .drt and only a non-zero size via the override. Without this,
    // every edit on those maps was silently rejected as "out of range".
    const QString schema = m_driver ? m_driver->schemaId : QString();
    const int effDX = DriverNames::effectiveDimX(schema, *map);
    const int effDY = DriverNames::effectiveDimY(schema, *map);
    const int idx = row * effDY + col;
    if (idx < 0 || idx >= effDX * effDY)
        return;
    const quint32 base = map->addresses.at(instanceIndex);
    const quint32 off  = base + quint32(idx * map->cellSize);

    bool ok = false;
    if (map->cellSize == 2) {
        // Cells are little-endian in EDC15C bins (matches readU16LE used in
        // MapData::readMapInstance).
        ok = m_modBin->writeU16LE(off, quint16(qBound(0, int(newValue), 65535)));
    } else {
        ok = false;
    }
    if (!ok) {
        statusBar()->showMessage(QStringLiteral("Write failed at 0x%1")
                                     .arg(off, 6, 16, QLatin1Char('0')).toUpper(),
                                 4000);
        return;
    }
    m_modDirty = true;

    // During bulk edit we only write bytes - the table widget is busy
    // iterating over its own QTableWidgetItem pointers and a refresh
    // here would invalidate them and crash. The single refresh happens
    // once in onBulkEditEnd.
    if (!m_bulkEditInProgress) {
        refreshTitle();
        refreshCurrentMap();
    }
}

void MainWindow::onBulkEditBegin()
{
    m_bulkEditInProgress = true;
}

void MainWindow::onBulkEditEnd()
{
    m_bulkEditInProgress = false;
    refreshTitle();
    refreshCurrentMap();
}

void MainWindow::onCopyOriginalToModified()
{
    if (!m_origBin || !m_modBin) {
        QMessageBox::information(this, QStringLiteral("Copy ORI"),
                                 QStringLiteral("Both Original and Modified bins must be loaded."));
        return;
    }
    const MapDefinition *m = m_tableView->currentMap();
    if (!m) {
        QMessageBox::information(this, QStringLiteral("Copy ORI"),
                                 QStringLiteral("Select a map first."));
        return;
    }
    const int inst = m_tableView->currentInstance();
    if (inst < 0 || inst >= m->addresses.size())
        return;

    // Use the canonical (DriverNames) name and the effective dimensions
    // rather than the .drt's raw fields. Some maps - notably the torque
    // limiter at 0x076D82 - have an empty name and 0x0 dimensions in the
    // .drt; without these overrides the dialog would say "UNNAMED (?)"
    // and "0 cells" and the copy would silently no-op.
    const QString schema = m_driver ? m_driver->schemaId : QString();
    const QString humanName = DriverNames::displayName(schema, *m);
    const int effDX = DriverNames::effectiveDimX(schema, *m);
    const int effDY = DriverNames::effectiveDimY(schema, *m);
    const int effCellCount = effDX * effDY;
    if (effCellCount <= 0) {
        QMessageBox::information(this, QStringLiteral("Copy ORI"),
                                 QStringLiteral("Map has no displayable cells."));
        return;
    }

    // Confirm to avoid accidental overwrite.
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Copy ORI -> MOD"),
        QStringLiteral("Copy the original values of '%1' into the modified bin?\n"
                       "This overwrites %2 cells at 0x%3.")
            .arg(humanName)
            .arg(effCellCount)
            .arg(m->addresses.at(inst), 6, 16, QLatin1Char('0')).toUpper(),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const quint32 base = m->addresses.at(inst);
    const qsizetype len = qsizetype(effCellCount * m->cellSize);
    const QByteArray src = m_origBin->readBytes(base, len);
    if (src.size() != len) {
        statusBar()->showMessage(QStringLiteral("Copy failed: source out of range"), 4000);
        return;
    }
    if (!m_modBin->writeBytes(base, src)) {
        statusBar()->showMessage(QStringLiteral("Copy failed: dest out of range"), 4000);
        return;
    }
    m_modDirty = true;
    refreshTitle();
    refreshCurrentMap();
    statusBar()->showMessage(
        QStringLiteral("Copied %1 bytes of '%2' from ORI to MOD")
            .arg(len).arg(humanName),
        4000);
}

void MainWindow::onExportModifiedBin()
{
    if (!m_modBin) {
        QMessageBox::information(this, QStringLiteral("Export"),
                                 QStringLiteral("No modified bin loaded."));
        return;
    }

    // Suggest a renamed default so we don't clobber the source bin.
    // When Modified was auto-mirrored from Original (same path), basing
    // the suggestion on Original's name still gives the right
    // "<name>_modified.bin" hint - and crucially the user is steered
    // away from overwriting the original file on disk.
    QString suggested;
    const QString basisPath = m_modBinPath.isEmpty() ? QString() : m_modBinPath;
    if (basisPath.isEmpty()) {
        suggested = AppPaths::dataDir();
        if (!suggested.isEmpty())
            suggested += QStringLiteral("/modified.bin");
    } else {
        const QFileInfo fi(basisPath);
        QString stem = fi.completeBaseName();
        // Avoid stacking "_modified_modified" if the user re-exports an
        // already-renamed file: only append the suffix when it's not
        // already there.
        if (!stem.endsWith(QStringLiteral("_modified"),
                           Qt::CaseInsensitive)) {
            stem += QStringLiteral("_modified");
        }
        suggested = fi.absolutePath() + QStringLiteral("/")
                    + stem + QStringLiteral(".") + fi.suffix();
    }

    const QString p = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export modified bin"), suggested,
        QStringLiteral("Bin files (*.bin);;All files (*)"));
    if (p.isEmpty())
        return;
    QString err;
    if (!m_modBin->saveFile(p, &err)) {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("Save failed: %1").arg(err));
        return;
    }
    m_modBinPath = p;
    m_modDirty = false;
    refreshTitle();
    statusBar()->showMessage(
        QStringLiteral("Exported: %1").arg(QFileInfo(p).fileName()), 4000);
}

void MainWindow::onBrowseDriver()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open driver"), AppPaths::dataDir(),
        QStringLiteral("Driver files (*.drt);;All files (*)"));
    if (p.isEmpty()) return;
    if (!loadDriver(p)) return;
    int idx = m_driverCombo->findData(p);
    if (idx < 0) {
        m_driverCombo->blockSignals(true);
        m_driverCombo->addItem(QFileInfo(p).fileName(), p);
        idx = m_driverCombo->count() - 1;
        m_driverCombo->blockSignals(false);
    }
    m_driverCombo->setCurrentIndex(idx);
}

void MainWindow::onBrowseOriginalBin()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open original bin"), AppPaths::dataDir(),
        QStringLiteral("Bin files (*.bin);;All files (*)"));
    if (p.isEmpty()) return;
    if (!loadOriginalBin(p)) return;
    int idx = m_origBinCombo->findData(p);
    if (idx < 0) {
        m_origBinCombo->blockSignals(true);
        m_origBinCombo->addItem(QFileInfo(p).fileName(), p);
        idx = m_origBinCombo->count() - 1;
        m_origBinCombo->blockSignals(false);
    }
    m_origBinCombo->setCurrentIndex(idx);
}

void MainWindow::onBrowseModifiedBin()
{
    const QString p = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open modified bin"), AppPaths::dataDir(),
        QStringLiteral("Bin files (*.bin);;All files (*)"));
    if (p.isEmpty()) return;
    if (!loadModifiedBin(p)) return;
    int idx = m_modBinCombo->findData(p);
    if (idx < 0) {
        m_modBinCombo->blockSignals(true);
        m_modBinCombo->addItem(QFileInfo(p).fileName(), p);
        idx = m_modBinCombo->count() - 1;
        m_modBinCombo->blockSignals(false);
    }
    m_modBinCombo->setCurrentIndex(idx);
}

void MainWindow::refreshTitle()
{
    QString origInfo = m_origBin
        ? QStringLiteral("%1b").arg(m_origBin->size())
        : QStringLiteral("(none)");
    QString modInfo = m_modBin
        ? QStringLiteral("%1b%2").arg(m_modBin->size())
              .arg(m_modDirty ? QStringLiteral(" *") : QString())
        : QStringLiteral("(none)");

    if (m_driver) {
        m_summaryLabel->setText(
            QStringLiteral("Driver: %1   |   ECU: %2   |   maps: %3   "
                           "|   ORI: %4   |   MOD: %5")
                .arg(m_driver->schemaId,
                     m_driver->ecuTypeCode.isEmpty() ? QStringLiteral("?")
                                                     : m_driver->ecuTypeCode)
                .arg(m_driver->maps.size())
                .arg(origInfo, modInfo));
    } else {
        m_summaryLabel->setText(QStringLiteral("(no driver loaded)"));
    }

    QString t = QStringLiteral("EcuParser");
    if (m_modDirty)
        t += QStringLiteral(" *");
    setWindowTitle(t);
}

} // namespace Titanium
