#include "MainWindow.h"

#include "AppPaths.h"
#include "DriverTreeWidget.h"
#include "../model/DriverNames.h"
#include "MapGraphWidget.h"
#include "MapTableWidget.h"
#include "../core/DrtParser.h"
#include "../core/MapData.h"
#include "../core/StagePackage.h"
#include "../core/XdfParser.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace EcuParser {

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
    // Narrower combos so all three (Driver, Original, Modified) plus
    // the action buttons fit on a typical 1366-1600 px wide window
    // without the right-most buttons overflowing into invisibility.
    constexpr int kComboW = 180;

    bar->addWidget(new QLabel(QStringLiteral("  Driver: "), this));
    m_driverCombo = new QComboBox(this);
    m_driverCombo->setMinimumWidth(kComboW);
    bar->addWidget(m_driverCombo);
    auto *browseDrvBtn = new QPushButton(QStringLiteral("..."), this);
    browseDrvBtn->setFixedWidth(28);
    browseDrvBtn->setToolTip(QStringLiteral("Browse for a .drt file"));
    bar->addWidget(browseDrvBtn);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral("  Original: "), this));
    m_origBinCombo = new QComboBox(this);
    m_origBinCombo->setMinimumWidth(kComboW);
    bar->addWidget(m_origBinCombo);
    auto *browseOrigBtn = new QPushButton(QStringLiteral("..."), this);
    browseOrigBtn->setFixedWidth(28);
    bar->addWidget(browseOrigBtn);

    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral("  Modified: "), this));
    m_modBinCombo = new QComboBox(this);
    m_modBinCombo->setMinimumWidth(kComboW);
    bar->addWidget(m_modBinCombo);
    auto *browseModBtn = new QPushButton(QStringLiteral("..."), this);
    browseModBtn->setFixedWidth(28);
    bar->addWidget(browseModBtn);

    bar->addSeparator();
    m_copyOriBtn = new QPushButton(QStringLiteral("Copy ORI -> MOD"), this);
    m_copyOriBtn->setToolTip(
        QStringLiteral("Copy original values of the selected map into the modified bin"));
    bar->addWidget(m_copyOriBtn);

    m_applyStageBtn = new QPushButton(QStringLiteral("Apply Stage..."), this);
    m_applyStageBtn->setToolTip(QStringLiteral(
        "Apply a pre-defined Stage1 / Stage2 tune to the modified bin "
        "using the original as the source. The original is unchanged."));
    bar->addWidget(m_applyStageBtn);

    m_exportBtn = new QPushButton(QStringLiteral("Export modified..."), this);
    m_exportBtn->setToolTip(QStringLiteral("Save the modified bin to disk (Ctrl+E)"));
    bar->addWidget(m_exportBtn);

    // ====== Menu bar ======
    // We mirror every toolbar action under File / Tools menus too,
    // because if the user's window is narrow the toolbar can overflow
    // and the right-most button (Export) becomes hidden. The menu
    // entries are always reachable, and they expose keyboard
    // shortcuts: Ctrl+E to export, Ctrl+T to apply a stage, Ctrl+R
    // to copy original into modified.
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *exportAct = fileMenu->addAction(QStringLiteral("&Export modified..."));
    exportAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAct, &QAction::triggered,
            this, &MainWindow::onExportModifiedBin);
    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QMainWindow::close);

    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    auto *stageAct = toolsMenu->addAction(QStringLiteral("Apply &Stage..."));
    stageAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(stageAct, &QAction::triggered,
            this, &MainWindow::onApplyStage);
    auto *copyAct = toolsMenu->addAction(QStringLiteral("Copy O&riginal -> Modified"));
    copyAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(copyAct, &QAction::triggered,
            this, &MainWindow::onCopyOriginalToModified);

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
    connect(m_applyStageBtn, &QPushButton::clicked,
            this, &MainWindow::onApplyStage);
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
    // (matching the reference tool's "Driver" workflow). Subsequent edits go
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

    // the reference tool-style "Driver" workflow: when Original is loaded and
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
    std::optional<DriverModel> parsed;
    // Pick the parser based on extension. .xdf files come from
    // TunerPro and the wider open-source tuning community; .drt are
    // the format we reverse-engineered ourselves. Both produce the
    // same DriverModel, so the rest of the app stays unchanged.
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("xdf")) {
        parsed = XdfParser::parseFile(path, &err);
    } else {
        parsed = DrtParser::parseFile(path, &err);
    }
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
        QStringLiteral("Driver loaded: %1 (%2 maps, format: %3)")
            .arg(QFileInfo(path).fileName())
            .arg(m_driver->maps.size())
            .arg(ext.toUpper()),
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
    m_tree->setBins(m_origBin.get(), m_modBin.get());
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
    m_tree->setBins(m_origBin.get(), m_modBin.get());
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
        m_tree->refreshDiffHighlights();
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
    m_tree->refreshDiffHighlights();
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
    m_tree->refreshDiffHighlights();
    statusBar()->showMessage(
        QStringLiteral("Copied %1 bytes of '%2' from ORI to MOD")
            .arg(len).arg(humanName),
        4000);
}

void MainWindow::onApplyStage()
{
    if (!m_driver) {
        QMessageBox::information(this, QStringLiteral("Apply stage"),
            QStringLiteral("Load a driver first."));
        return;
    }
    if (!m_origBin) {
        QMessageBox::information(this, QStringLiteral("Apply stage"),
            QStringLiteral("Load an Original bin first - the stage uses it "
                           "as the source. Modified will receive the result."));
        return;
    }
    if (!m_modBin) {
        QMessageBox::information(this, QStringLiteral("Apply stage"),
            QStringLiteral("Load a Modified bin first (or pick an Original; "
                           "Modified is auto-mirrored)."));
        return;
    }

    // Discover the available stage packages under data/stages/.
    const auto stages = StagePackage::listAvailable();
    if (stages.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Apply stage"),
            QStringLiteral("No stage packages found in data/stages/."));
        return;
    }

    // Build a small picker dialog: a label with names + descriptions
    // and Yes/No buttons. We use QInputDialog::getItem for a simple
    // single-pick UI - good enough for the current handful of stages.
    QStringList items;
    for (const auto &s : stages)
        items.append(s.first);
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("Apply stage"),
        QStringLiteral("Select a stage to apply (Original is the source, "
                       "Modified is overwritten):"),
        items, 0, false, &ok);
    if (!ok || chosen.isEmpty())
        return;

    // Resolve back to file path.
    QString path;
    for (const auto &s : stages) {
        if (s.first == chosen) { path = s.second; break; }
    }
    if (path.isEmpty())
        return;

    // Load the stage package now (we already loaded once for the
    // listing, but loading is cheap and keeps this slot self-contained).
    StagePackage pkg;
    QString err;
    if (!StagePackage::loadFromJson(path, &pkg, &err)) {
        QMessageBox::warning(this, QStringLiteral("Apply stage"),
            QStringLiteral("Failed to load %1:\n%2").arg(QFileInfo(path).fileName(), err));
        return;
    }

    // Confirmation dialog: if the package has any options[], present
    // them as checkboxes so the user can opt-in/out of each (e.g. EGR
    // off). If there are no options, fall back to a plain Yes/No
    // confirmation. This is built inline rather than as a separate
    // class because it's small, self-contained and only used here.
    QSet<QString> enabledOptionIds;
    {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Apply stage"));
        auto *vlay = new QVBoxLayout(&dlg);

        auto *header = new QLabel(QStringLiteral("<b>Apply '%1' to the modified bin?</b>")
                                       .arg(pkg.name), &dlg);
        header->setWordWrap(true);
        vlay->addWidget(header);

        if (!pkg.description.isEmpty()) {
            auto *desc = new QLabel(pkg.description, &dlg);
            desc->setWordWrap(true);
            desc->setStyleSheet(QStringLiteral("color: #555; margin-bottom: 6px;"));
            vlay->addWidget(desc);
        }

        auto *paths = new QLabel(
            QStringLiteral("Source: Original (%1)<br>Target: Modified (%2)<br><br>"
                           "<i>The original bin is not changed.</i>")
                .arg(QFileInfo(m_origBinCombo->currentText()).fileName())
                .arg(QFileInfo(m_modBinPath).fileName()),
            &dlg);
        paths->setWordWrap(true);
        vlay->addWidget(paths);

        // One checkbox per option, with description below
        QList<QCheckBox*> checkBoxes;
        if (!pkg.options.isEmpty()) {
            auto *grp = new QGroupBox(QStringLiteral("Options"), &dlg);
            auto *grpLay = new QVBoxLayout(grp);
            grpLay->setSpacing(8);
            for (const StageOption &opt : pkg.options) {
                auto *cb = new QCheckBox(opt.label.isEmpty() ? opt.id : opt.label, grp);
                cb->setChecked(opt.defaultOn);
                cb->setProperty("optionId", opt.id);
                grpLay->addWidget(cb);
                if (!opt.description.isEmpty()) {
                    auto *od = new QLabel(opt.description, grp);
                    od->setWordWrap(true);
                    od->setStyleSheet(QStringLiteral(
                        "color: #666; font-size: 11px; margin-left: 22px;"
                        " margin-bottom: 6px;"));
                    grpLay->addWidget(od);
                }
                checkBoxes.append(cb);
            }
            vlay->addWidget(grp);
        }

        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply"));
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        vlay->addWidget(btns);

        dlg.setMinimumWidth(480);
        if (dlg.exec() != QDialog::Accepted)
            return;

        for (QCheckBox *cb : checkBoxes) {
            if (cb->isChecked())
                enabledOptionIds.insert(cb->property("optionId").toString());
        }
    }

    // First reset Modified to a copy of Original so re-applying a stage
    // is idempotent (otherwise applying Stage1 then Stage2 would
    // compound percentages on already-edited cells). writeBytes is a
    // simple bulk overwrite at offset 0.
    if (!m_modBin->writeBytes(0, m_origBin->raw())) {
        QMessageBox::warning(this, QStringLiteral("Apply stage"),
            QStringLiteral("Could not reset Modified bin (size mismatch?)"));
        return;
    }

    // Build the effective stage: core edits plus the edits of any
    // option the user kept checked. We mutate a local copy so the
    // on-disk JSON is untouched.
    StagePackage effective = pkg;
    for (const StageOption &opt : pkg.options) {
        if (enabledOptionIds.contains(opt.id))
            effective.edits.append(opt.edits);
    }
    effective.options.clear(); // already merged

    QStringList warnings;
    const int written = applyStage(effective, *m_driver, *m_origBin,
                                   m_modBin.get(), &warnings);
    m_modDirty = (written > 0);
    refreshTitle();
    refreshCurrentMap();
    m_tree->refreshDiffHighlights();

    // Build a small summary for the user: which options got applied,
    // how many cells were written. Useful for confirming "yes, EGR
    // off was indeed included" after the fact.
    QStringList appliedOpts;
    for (const StageOption &opt : pkg.options) {
        if (enabledOptionIds.contains(opt.id))
            appliedOpts.append(opt.label.isEmpty() ? opt.id : opt.label);
    }
    QString summary = QStringLiteral("'%1' applied: %2 cells written.")
                          .arg(pkg.name).arg(written);
    if (!appliedOpts.isEmpty())
        summary += QStringLiteral("\n\nOptions enabled:\n  - ")
                   + appliedOpts.join(QStringLiteral("\n  - "));
    if (!warnings.isEmpty())
        summary += QStringLiteral("\n\nWarnings:\n  - ")
                   + warnings.join(QStringLiteral("\n  - "));
    statusBar()->showMessage(QStringLiteral("Applied stage '%1' (%2 cells)")
                                 .arg(pkg.name).arg(written), 5000);
    QMessageBox::information(this, QStringLiteral("Apply stage"), summary);
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
        QStringLiteral("Driver files (*.drt *.xdf);;DRT files (*.drt);;TunerPro XDF (*.xdf);;All files (*)"));
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

} // namespace EcuParser
