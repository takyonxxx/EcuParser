#include "MainWindow.h"

#include "AppPaths.h"
#include "ChecksumDialog.h"
#include "CustomTuneDialog.h"
#include "DiffViewWidget.h"
#include "DriverTreeWidget.h"
#include "HexEditorWidget.h"
#include "../model/DriverNames.h"
#include "MapGraphWidget.h"
#include "MapTableWidget.h"
#include "StagePreviewDialog.h"
#include "Surface3DWidget.h"
#include "TuneLogDialog.h"
#include "UndoCommands.h"
#include "../core/DrtParser.h"
#include "../core/Checksum.h"
#include "../core/MapData.h"
#include "../core/StagePackage.h"
#include "../core/TuneLogger.h"
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
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUndoStack>
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
    toolsMenu->addSeparator();
    auto *checksumAct = toolsMenu->addAction(QStringLiteral("&Checksum verify / repair..."));
    checksumAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(checksumAct, &QAction::triggered,
            this, &MainWindow::onVerifyChecksum);
    auto *customAct = toolsMenu->addAction(QStringLiteral("Custom &tune editor..."));
    customAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+M")));
    connect(customAct, &QAction::triggered,
            this, &MainWindow::onCustomTuneEditor);
    auto *logAct = toolsMenu->addAction(QStringLiteral("Tune &log..."));
    logAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(logAct, &QAction::triggered,
            this, &MainWindow::onShowTuneLog);

    // ====== View menu ======
    // Toggle whether shadow map instances are exposed. Off by default
    // (matches the reference tool, which hides the duplicate addresses
    // for "(Boost x RPM)" maps). Turning this on lets the user inspect
    // the second copy that the .drt file records but the reference tool
    // doesn't display - useful for verifying the two addresses really
    // hold identical bytes, or for editing the shadow if it diverges.
    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto *expertAct = viewMenu->addAction(QStringLiteral("&Expert mode (show shadow instances)"));
    expertAct->setCheckable(true);
    expertAct->setChecked(DriverNames::expertMode());
    connect(expertAct, &QAction::toggled,
            this, [this](bool on) {
                DriverNames::setExpertMode(on);
                // Rebuild the tree - maxInstances now answers differently,
                // so the per-map instance children must be regenerated.
                if (m_driver) m_tree->setDriver(m_driver.get());
                refreshCurrentMap();
            });

    // ====== Edit menu (Undo/Redo) ======
    // Single per-window undo stack. Cleared when a modified bin is
    // (re)loaded - the underlying bytes change, so old commands no
    // longer make sense. Stage applies push a single BulkRegionCommand
    // for the whole bin so one Ctrl+Z reverts an entire stage in one
    // step (otherwise undoing a stage would require N keystrokes for
    // N edited cells).
    m_undoStack = new QUndoStack(this);
    m_undoStack->setUndoLimit(200);
    auto *editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    m_undoAct = m_undoStack->createUndoAction(this, QStringLiteral("&Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    editMenu->addAction(m_undoAct);
    m_redoAct = m_undoStack->createRedoAction(this, QStringLiteral("&Redo"));
    m_redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addAction(m_redoAct);

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
    m_diffView  = new DiffViewWidget(m_tabs);
    m_surfaceView = new Surface3DWidget(m_tabs);
    m_hexView   = new HexEditorWidget(m_tabs);
    m_hexView->setMainWindow(this);
    m_tabs->addTab(m_tableView,   QStringLiteral("Table"));
    m_tabs->addTab(m_graphView,   QStringLiteral("Graph"));
    m_tabs->addTab(m_surfaceView, QStringLiteral("3D"));
    m_tabs->addTab(m_diffView,    QStringLiteral("Diff"));
    m_tabs->addTab(m_hexView,     QStringLiteral("Hex"));
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

    // Diff tab: double-click jumps the user back to the Table tab on
    // that map. We forward through onMapSelected so all the existing
    // tree/table/graph plumbing fires identically to clicking the tree.
    connect(m_diffView, &DiffViewWidget::mapActivated,
            this, [this](const MapDefinition *m, int inst) {
                onMapSelected(m, inst);
                if (m_tabs) m_tabs->setCurrentWidget(m_tableView);
            });
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

    // Apply Stage starts disabled; refresh sets the right state once
    // the user loads a driver and original bin.
    refreshApplyStageButton();
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
    // Stitch in physical-unit overrides for known maps in this schema.
    // After this loop, every MapDefinition::{scale,offset,unit} reflects
    // either what the parser already filled (XDF MATH equation, in
    // future) or what DriverNames knows about (rail pressure -> bar,
    // torque limiter -> Nm proxy, etc.).
    //
    // Same pass also injects canonical RPM/Load breakpoint arrays from
    // DriverNames. Some XDF files (e.g. 28F0_100.xdf shipped with this
    // project) describe axes only as <units>RPM</units> without an
    // EMBEDDEDDATA address, so the axis addresses arrive as 0 and the
    // table widget would otherwise fall back to row-index headers
    // (0, 1, 2, ...). Copying the override into MapDefinition.xValues/
    // yValues makes the canonical breakpoints available regardless of
    // whether the driver source was DRT or XDF, and MapTableWidget /
    // MapGraphWidget consult these embedded values first.
    for (MapDefinition &m : m_driver->maps) {
        DriverNames::applyUnitOverride(m_driver->schemaId, &m);
        const QList<int> xOver = DriverNames::axisXOverride(
            m_driver->schemaId, m);
        if (!xOver.isEmpty())
            m.xValues = xOver;
        const QList<int> yOver = DriverNames::axisYOverride(
            m_driver->schemaId, m);
        if (!yOver.isEmpty())
            m.yValues = yOver;
    }
    m_tree->setDriver(m_driver.get());
    m_tableView->clearMap();
    m_graphView->clear();
    refreshTitle();
    // Driver change can change the schema and therefore which protected
    // regions apply. Refresh the modified bin's snapshot list so the
    // save-time guard uses the new schema's profile.
    refreshProtectedSnapshots();
    statusBar()->showMessage(
        QStringLiteral("Driver loaded: %1 (%2 maps, format: %3)")
            .arg(QFileInfo(path).fileName())
            .arg(m_driver->maps.size())
            .arg(ext.toUpper()),
        4000);
    refreshApplyStageButton();
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
    if (m_hexView)
        m_hexView->setBins(m_origBin.get(), m_modBin.get());
    refreshCurrentMap();

    // === Driver auto-detect ===
    // If no driver is loaded yet, peek at the bin to guess the schema
    // and auto-pick a matching .drt from the data dir. We skip this if
    // a driver IS loaded and matches - the user has already curated
    // their setup. We also skip if multiple .drt candidates match: in
    // that case the user must choose, since picking blindly could
    // misrepresent the bin (J293_822/J094_704/J409_438 all use the
    // same schema 28F0_100 but represent different SW revisions).
    const QString detected = m_origBin->detectSchema();

    // Family-aware compatibility check: a driver is "compatible enough"
    // with the bin if either:
    //   - schema strings match exactly (28F0_100 driver + 28F0_100 bin)
    //   - both belong to the same ECU family. For GM PCM 0411 the
    //     family is identified by the bin's "GM_0411_OS<N>" prefix,
    //     and any XDF whose schema id contains an 8-digit GM OS number
    //     (typical filename pattern, e.g. "12587604_OS_2023-06-23")
    //     counts as a 0411 driver. Cross-OS-revision XDFs are imperfect
    //     - some addresses will be wrong - but the user knows that and
    //     wants to use what they have rather than be blocked.
    auto isFamilyCompatible = [](const QString &driverSchema,
                                 const QString &binSchema) {
        if (driverSchema == binSchema) return true;
        // GM family heuristic: a bin schema like "GM_0411_OS<n>" or
        // "GM_E40_OS<n>" is compatible with any driver schema id that
        // contains an 8-digit GM OS number (typical XDF filename
        // pattern, e.g. "12598977_OS_V1_0_BETA"). Cross-OS-revision
        // XDFs are imperfect - some addresses can shift between
        // revisions - but they're useful enough that we let the user
        // proceed rather than blocking the load. The GUI surfaces a
        // warning so the user knows to verify.
        if (binSchema.startsWith(QStringLiteral("GM_0411"))
         || binSchema.startsWith(QStringLiteral("GM_E40"))) {
            const QRegularExpression re(QStringLiteral("\\b(12\\d{6})\\b"));
            return re.match(driverSchema).hasMatch();
        }
        // Chrysler JTEC heuristic: bin schema "Chrysler_JTEC_<partno>"
        // is compatible with any driver schema whose id contains the
        // Chrysler service-number prefix "5604" followed by 6 chars.
        // Cross-OS JTEC drivers are even less reliable than cross-OS
        // GM drivers (the calibration layout reorganises between
        // service revisions) but the user can still browse and edit
        // - we just hint that values may be wrong.
        if (binSchema.startsWith(QStringLiteral("Chrysler_JTEC_"))) {
            const QRegularExpression re(QStringLiteral("\\b(5604[0-9A-Z]{6})\\b"));
            return re.match(driverSchema).hasMatch();
        }
        return false;
    };

    const bool needAutoPick = !m_driver
        || (m_driver && !detected.isEmpty()
            && !isFamilyCompatible(m_driver->schemaId, detected));
    if (needAutoPick && !detected.isEmpty()) {
        const QStringList drvs = AppPaths::listDrivers();
        // Walk every available driver and check which ones have schema
        // == detected. We do NOT auto-load if 0 or multiple match - we
        // surface the suggestion in the status bar instead so the user
        // can decide.
        // Walk every available driver and check which ones are
        // compatible with the detected schema (exact match OR family
        // match - see isFamilyCompatible above).
        QStringList matching;
        for (const QString &drvPath : drvs) {
            const QString ext = QFileInfo(drvPath).suffix().toLower();
            std::optional<DriverModel> p;
            QString perr;
            if (ext == QStringLiteral("xdf"))
                p = XdfParser::parseFile(drvPath, &perr);
            else
                p = DrtParser::parseFile(drvPath, &perr);
            if (p && isFamilyCompatible(p->schemaId, detected))
                matching.append(drvPath);
        }
        if (matching.size() == 1) {
            // Single match: load it silently. The user can revert via
            // the driver combo if they want a different one.
            loadDriver(matching.first());
            const int idx = m_driverCombo->findData(matching.first());
            if (idx >= 0) m_driverCombo->setCurrentIndex(idx);

            // For cross-revision driver matches in any of the
            // multi-revision families (GM 0411, GM E40, Chrysler
            // JTEC), warn about possible address shifts. The driver
            // may come from a different OS / part number than the
            // bin. Map definitions are still useful but some
            // addresses will be wrong - the user should verify
            // before relying on edits.
            const bool isMultiRevFamily =
                detected.startsWith(QStringLiteral("GM_0411"))
             || detected.startsWith(QStringLiteral("GM_E40"))
             || detected.startsWith(QStringLiteral("Chrysler_JTEC_"));
            if (isMultiRevFamily
                && m_driver
                && m_driver->schemaId != detected) {
                const QString familyName =
                    detected.startsWith(QStringLiteral("Chrysler_JTEC_"))
                        ? QStringLiteral("Chrysler JTEC")
                        : (detected.startsWith(QStringLiteral("GM_E40"))
                            ? QStringLiteral("GM E40")
                            : QStringLiteral("GM PCM 0411"));
                QMessageBox::information(this,
                    QStringLiteral("Driver loaded"),
                    QStringLiteral(
                        "Loaded driver '%1' (schema %2) for a bin from "
                        "a different revision (%3). Both belong to "
                        "the %4 family so most map definitions should "
                        "be useful, but some map addresses can shift "
                        "between revisions - verify map values look "
                        "reasonable before saving edits.")
                        .arg(QFileInfo(matching.first()).fileName(),
                             m_driver->schemaId,
                             detected,
                             familyName));
            }
            statusBar()->showMessage(
                QStringLiteral("Auto-loaded driver %1 (schema %2 detected)")
                    .arg(QFileInfo(matching.first()).fileName(), detected),
                5000);
        } else if (matching.size() > 1) {
            // Multiple candidates: just hint, don't pick.
            statusBar()->showMessage(
                QStringLiteral("Schema %1 detected - %2 drivers match. "
                               "Pick one from the Driver combo.")
                    .arg(detected).arg(matching.size()),
                6000);
        } else {
            // No driver matches the detected schema. Build a family-
            // aware message so the user knows what's needed - generic
            // bins should not pretend to be EDC15C even if the size
            // happens to match.
            QString familyHint;
            if (detected.startsWith(QStringLiteral("GM_0411"))
             || detected.startsWith(QStringLiteral("GM_E40"))) {
                const QString family =
                    detected.startsWith(QStringLiteral("GM_E40"))
                        ? QStringLiteral("GM E40")
                        : QStringLiteral("GM PCM 0411");
                familyHint = QStringLiteral(
                    "This bin appears to be a %1 calibration (%2). "
                    "EcuParser ships drivers for Bosch EDC15C 28F0_100 "
                    "only. To work with this bin, supply a compatible "
                    "XML XDF or DRT defining its maps and load it from "
                    "the Driver combo. Community XDFs are available "
                    "from PCMHacking / LS1Tech / GearheadEFI forums.")
                    .arg(family, detected);
            } else if (detected.startsWith(QStringLiteral("Chrysler_JTEC_"))) {
                familyHint = QStringLiteral(
                    "This bin appears to be a Chrysler JTEC PCM "
                    "calibration (%1). EcuParser detects the part "
                    "number but does not ship a JTEC driver - "
                    "publicly available XDFs for JTEC are scarce and "
                    "fragmented across Chrysler service revisions. "
                    "To work with this bin you'll need to supply your "
                    "own XML XDF or DRT (try forums.jeepforum.com, "
                    "jeepstrokers.com, or commercial tuners like FRP "
                    "Tuning, Syked, B&G). Note: JTEC tunes are "
                    "unrelated to the Stage 1 / Stage 2 packages we "
                    "ship - those are diesel-specific and don't apply "
                    "to gasoline JTEC platforms.")
                    .arg(detected);
            } else {
                familyHint = QStringLiteral(
                    "Schema %1 detected, but no shipped driver matches. "
                    "Load a compatible XDF or DRT from the Driver combo.")
                    .arg(detected);
            }
            // Show as a non-blocking dialog so the user reads it
            // explicitly - the status bar message would scroll past
            // unnoticed on first load.
            QMessageBox::information(this,
                QStringLiteral("Bin loaded"),
                familyHint);
            statusBar()->showMessage(
                QStringLiteral("Schema %1 detected - no driver loaded")
                    .arg(detected),
                8000);
        }
    }

    // The auto-mirror of Modified <- Original happens in
    // onOriginalBinComboChanged() (where the path is bound to the combo
    // index), not here. loadOriginalBin can also be called from the
    // browse dialog and doesn't need the mirror behaviour itself.
    //
    // If a modified bin is already loaded, refresh its protected
    // snapshots from the new original. This keeps the snapshot bytes
    // consistent with whatever the user considers the "stock" - even
    // if they reload original after modified.
    refreshProtectedSnapshots();
    refreshApplyStageButton();
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
    // Reset undo history: previous commands referenced bytes that no
    // longer exist (the file changed). Without this clear, hitting
    // Ctrl+Z after loading a different bin would write the OLD bin's
    // pre-edit bytes into NEW bin offsets - silent, dangerous corruption.
    if (m_undoStack) m_undoStack->clear();
    // Set up protected-region snapshots so the calibration checksum
    // word at 0x07BD7C and the ECU module ID stamps are preserved
    // verbatim from the ORIGINAL bin into any saved modified bin.
    // Source of truth for the snapshot bytes is the original (stock)
    // bin - that's the value the ECU was running on, so by definition
    // the ECU has accepted it. See Checksum.cpp profile and BinFile.h
    // for the rationale (Bosch CRC algorithm not reverse-engineered).
    refreshProtectedSnapshots();
    statusBar()->showMessage(
        QStringLiteral("Modified bin: %1 (%2 bytes)")
            .arg(QFileInfo(path).fileName())
            .arg(m_modBin->size()),
        4000);
    refreshTitle();
    m_tree->setBins(m_origBin.get(), m_modBin.get());
    if (m_hexView)
        m_hexView->setBins(m_origBin.get(), m_modBin.get());
    refreshCurrentMap();
    return true;
}

void MainWindow::refreshProtectedSnapshots()
{
    // Wire the originating bin's bytes into the modified bin's
    // ProtectedSnapshot list. Called from both loadOriginalBin (when
    // the orig bin changes) and loadModifiedBin (when the mod bin
    // changes). Both call sites need to stay consistent: the snapshot
    // bytes are taken from the ORIGINAL bin, not the modified one.
    if (!m_modBin || !m_origBin || !m_driver) {
        if (m_modBin) m_modBin->clearProtectedSnapshots();
        return;
    }
    const ChecksumProfile prof = Checksum::profileForSchema(m_driver->schemaId);
    if (prof.protectedRegions.isEmpty()) {
        m_modBin->clearProtectedSnapshots();
        return;
    }
    QList<QPair<quint32, quint32>> regions;
    QStringList descs;
    for (const ProtectedRegion &pr : prof.protectedRegions) {
        regions.append({pr.startOffset, pr.endOffset});
        descs.append(pr.description);
    }
    m_modBin->setProtectedSnapshots(m_origBin->raw(), regions, descs);
}

void MainWindow::refreshApplyStageButton()
{
    if (!m_applyStageBtn) return;
    // Apply Stage works only when:
    //   - a driver is loaded (we know the schema id)
    //   - the schema id matches a calibration we ship stages for
    //     (currently only 28F0_100 - the WJ 2.7 CRD OM612 EDC15C
    //     calibration). Stage JSONs reference maps by name and
    //     assume a particular schema's address layout / value
    //     ranges, so applying them to other ECUs is unsafe.
    //   - an original bin is loaded (stages need a stock baseline
    //     to compute deltas against).
    //
    // When any of these is missing we disable the button and put
    // the reason in the tooltip so the user can hover to find out
    // why it's greyed out.
    const bool haveDriver = (m_driver != nullptr);
    const bool haveOrig   = (m_origBin != nullptr);
    const QString schema  = haveDriver ? m_driver->schemaId : QString();
    const bool schemaSupportsStages =
        (schema == QStringLiteral("28F0_100"));

    const bool enabled = haveDriver && haveOrig && schemaSupportsStages;
    m_applyStageBtn->setEnabled(enabled);

    QString tip;
    if (!haveDriver) {
        tip = QStringLiteral(
            "Apply Stage is unavailable: load a driver first so "
            "the schema is known.");
    } else if (!haveOrig) {
        tip = QStringLiteral(
            "Apply Stage is unavailable: load an original bin "
            "first - stages need a stock baseline.");
    } else if (!schemaSupportsStages) {
        tip = QStringLiteral(
            "Apply Stage is unavailable for schema '%1'. The "
            "shipped stage packages (Stage 1, Stage 2, Economy "
            "Soft, Economy Hard) target schema 28F0_100 only "
            "(Jeep WJ 2.7 CRD, Mercedes OM612, Bosch EDC15C). "
            "Other schemas can browse and edit maps but cannot "
            "apply stages.")
            .arg(schema);
    } else {
        // Default tooltip when enabled.
        tip = QStringLiteral(
            "Apply a pre-defined Stage 1 / Stage 2 / Economy "
            "tune to the modified bin using the original as the "
            "source. The original is unchanged.");
    }
    m_applyStageBtn->setToolTip(tip);
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
    if (m_surfaceView) {
        m_surfaceView->setMap(map, addressIndex, schema,
                              m_origBin.get(), m_modBin.get());
    }
}

void MainWindow::refreshCurrentMap()
{
    // Always refresh the diff overview - it doesn't depend on a
    // selected map and reflects the global state of MOD vs ORI.
    if (m_diffView)
        m_diffView->refresh(m_driver.get(), m_origBin.get(), m_modBin.get());

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

    // Skip the undo machinery when the call is itself a redo/undo
    // replay - the QUndoCommand drives the bin write directly via
    // undoRedoWriteU16LE() so we must NOT push another command here
    // (would result in a stack that recurses forever on Ctrl+Y).
    bool ok = false;
    if (map->cellSize == 2) {
        const quint16 clamped = quint16(qBound(0, int(newValue), 65535));
        if (m_replayingUndo) {
            // Direct write, no command push.
            ok = m_modBin->writeU16LE(off, clamped);
        } else {
            // Read OLD value first so the command can carry undo state.
            // If the read fails the offset is invalid - bail before pushing.
            bool readOk = false;
            const quint16 oldV = m_modBin->readU16LE(off, &readOk);
            if (!readOk) {
                statusBar()->showMessage(
                    QStringLiteral("Read failed at 0x%1")
                        .arg(off, 6, 16, QLatin1Char('0')).toUpper(),
                    4000);
                return;
            }
            if (oldV == clamped) {
                // No-op edit (user typed the same value back). Don't
                // pollute the undo stack with empty commands.
                ok = true;
            } else if (m_undoStack) {
                const QString humanName = DriverNames::displayName(schema, *map);
                auto *cmd = new CellEditCommand(this, off, oldV, clamped,
                                                humanName, row, col,
                                                instanceIndex);
                // QUndoStack::push calls redo() which performs the write
                // and refresh internally - no separate writeU16LE needed.
                m_undoStack->push(cmd);
                ok = true;
            } else {
                ok = m_modBin->writeU16LE(off, clamped);
            }
        }
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
    // once in onBulkEditEnd. Replay also skips refresh because the
    // QUndoCommand calls undoRedoRefresh() itself once per command.
    if (!m_bulkEditInProgress && !m_replayingUndo) {
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
    // Capture the OLD bytes for undo before we overwrite them.
    const QByteArray prevBytes = m_modBin->readBytes(base, len);
    if (prevBytes.size() != len) {
        statusBar()->showMessage(QStringLiteral("Copy failed: cannot snapshot mod region"), 4000);
        return;
    }
    if (m_undoStack) {
        auto *cmd = new BulkRegionCommand(
            this, base, prevBytes, src,
            QStringLiteral("Copy ORI -> MOD: %1 (%2 bytes)").arg(humanName).arg(len));
        m_undoStack->push(cmd);
    } else {
        if (!m_modBin->writeBytes(base, src)) {
            statusBar()->showMessage(QStringLiteral("Copy failed: dest out of range"), 4000);
            return;
        }
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

    // === Dry-run preview ===
    // Walk the stage in preview mode (no writes). Show a per-edit summary
    // so the user sees if any edit will be silently neutered by a cap
    // (this is the Stage 2 turbo cap=1900 / torque cap=9500 problem).
    {
        const StagePreview prev = previewStage(pkg, *m_driver, *m_origBin,
                                               enabledOptionIds);
        StagePreviewDialog dlg(pkg, prev,
                               m_origBinCombo->currentText(),
                               m_modBinPath, this);
        if (dlg.exec() != QDialog::Accepted)
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

    // Snapshot the modified bin BEFORE any change so undo can restore
    // it in one Ctrl+Z. Then compute the post-stage bytes in a scratch
    // BinFile and push the whole transformation as a single command.
    const QByteArray prevBytes = m_modBin->raw();

    // Scratch target: copy of the original (idempotency reset) plus the
    // effective stage edits. We never touch m_modBin during this
    // computation - the undo command's redo() does the write.
    BinFile scratch(m_origBin->raw());
    QStringList warnings;
    const int written = applyStage(effective, *m_driver, *m_origBin,
                                   &scratch, &warnings);
    const QByteArray newBytes = scratch.raw();

    if (m_undoStack) {
        QString desc = QStringLiteral("Apply stage: %1").arg(pkg.name);
        if (!enabledOptionIds.isEmpty())
            desc += QStringLiteral(" (+%1 options)").arg(enabledOptionIds.size());
        auto *cmd = new BulkRegionCommand(this, 0, prevBytes, newBytes, desc);
        m_undoStack->push(cmd);  // redo() runs - writes scratch bytes into m_modBin.
    } else {
        // Fallback if undo stack isn't initialised for any reason.
        m_modBin->writeBytes(0, newBytes);
        refreshTitle();
        refreshCurrentMap();
        m_tree->refreshDiffHighlights();
    }
    m_modDirty = (written > 0);
    // Persistent log entry: written count, stage name, schema, mod bin
    // path. Survives across launches via SQLite. Users can review under
    // Tools -> Tune log... and add ratings/notes once the tune has been
    // road-tested.
    if (written > 0) {
        TuneLogger::recordApply(
            m_driver ? m_driver->schemaId : QString(),
            m_modBinPath,
            pkg.name,
            written);
    }
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

void MainWindow::onShowTuneLog()
{
    TuneLogDialog dlg(this);
    dlg.exec();
}

void MainWindow::onVerifyChecksum()
{
    if (!m_modBin) {
        QMessageBox::information(this, QStringLiteral("Checksum"),
            QStringLiteral("Load a Modified bin first - checksums operate on the\n"
                           "bin you intend to flash, not on the read-only Original."));
        return;
    }
    const QString schema = m_driver ? m_driver->schemaId : QString();
    ChecksumDialog dlg(this, m_modBin.get(), m_modBinPath, schema, this);
    dlg.exec();
}

void MainWindow::onCustomTuneEditor()
{
    if (!m_driver || !m_origBin || !m_modBin) {
        QMessageBox::information(this, QStringLiteral("Custom tune"),
            QStringLiteral("Load a driver and both Original + Modified bins first."));
        return;
    }
    CustomTuneDialog dlg(this, m_driver.get(), m_origBin.get(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    const StagePackage pkg = dlg.resultPackage();
    if (pkg.edits.isEmpty()) return;

    // Same pattern as onApplyStage's tail: snapshot mod, apply to a
    // scratch copy of orig, push BulkRegionCommand for undo.
    const QByteArray prevBytes = m_modBin->raw();
    BinFile scratch(m_origBin->raw());
    QStringList warnings;
    const int written = applyStage(pkg, *m_driver, *m_origBin,
                                   &scratch, &warnings);
    if (written == 0) {
        QMessageBox::information(this, QStringLiteral("Custom tune"),
            QStringLiteral("Tune touched 0 cells.%1")
                .arg(warnings.isEmpty()
                         ? QString()
                         : QStringLiteral("\n\nWarnings:\n  - %1")
                               .arg(warnings.join(QStringLiteral("\n  - ")))));
        return;
    }
    const QByteArray newBytes = scratch.raw();
    auto *cmd = new BulkRegionCommand(
        this, 0, prevBytes, newBytes,
        QStringLiteral("Custom tune: %1 (%2 cells)")
            .arg(pkg.name.isEmpty() ? QStringLiteral("unnamed") : pkg.name)
            .arg(written));
    pushUndoCommand(cmd);
    m_modDirty = true;
    TuneLogger::recordApply(
        m_driver ? m_driver->schemaId : QString(),
        m_modBinPath,
        pkg.name.isEmpty() ? QStringLiteral("(custom)") : pkg.name,
        written);
    statusBar()->showMessage(
        QStringLiteral("Custom tune '%1' applied (%2 cells)")
            .arg(pkg.name).arg(written),
        5000);
    if (!warnings.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Custom tune"),
            QStringLiteral("Applied with warnings:\n  - %1")
                .arg(warnings.join(QStringLiteral("\n  - "))));
    }
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
        suggested = AppPaths::binsDir();
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
        this, QStringLiteral("Open driver"), AppPaths::driversDir(),
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
        this, QStringLiteral("Open original bin"), AppPaths::binsDir(),
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
        this, QStringLiteral("Open modified bin"), AppPaths::binsDir(),
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

bool MainWindow::undoRedoWriteU16LE(quint32 offset, quint16 value)
{
    if (!m_modBin) return false;
    m_replayingUndo = true;
    const bool ok = m_modBin->writeU16LE(offset, value);
    m_replayingUndo = false;
    if (ok) m_modDirty = true;
    return ok;
}

bool MainWindow::undoRedoWriteBytes(quint32 offset, const QByteArray &bytes)
{
    if (!m_modBin) return false;
    m_replayingUndo = true;
    const bool ok = m_modBin->writeBytes(offset, bytes);
    m_replayingUndo = false;
    if (ok) m_modDirty = true;
    return ok;
}

void MainWindow::undoRedoRefresh()
{
    refreshTitle();
    refreshCurrentMap();
    if (m_tree) m_tree->refreshDiffHighlights();
    if (m_hexView) m_hexView->refresh();
}

void MainWindow::pushUndoCommand(QUndoCommand *cmd)
{
    if (!cmd) return;
    if (m_undoStack) {
        // QUndoStack::push calls cmd->redo() and takes ownership.
        m_undoStack->push(cmd);
    } else {
        // No stack: replay redo manually then leak the command. This
        // shouldn't happen in practice (m_undoStack is created in
        // buildUi()) but the fallback keeps callers safe.
        cmd->redo();
        delete cmd;
    }
}

} // namespace EcuParser
