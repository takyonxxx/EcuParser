#include "MainWindow.h"

#include "../core/BinFile.h"
#include "../core/Checksum.h"
#include "../core/DrtParser.h"
#include "../core/MapData.h"
#include "../core/StagePackage.h"
#include "../core/XdfParser.h"
#include "../model/DriverModel.h"
#include "../model/DriverNames.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPalette>
#include <QStyleFactory>
#include <QFile>
#include <QTextStream>
#include <cstdio>

namespace {

// Build a dark palette over Qt's Fusion style. We pick the Fusion style
// because it's the only built-in style that obeys QPalette uniformly across
// platforms - on Windows the default "windowsvista" style ignores most
// palette colours, on macOS the native style is similarly opaque.
QPalette makeDarkPalette()
{
    QPalette p;
    const QColor base       (28,  31,  36);
    const QColor baseAlt    (38,  42,  48);
    const QColor surface    (44,  48,  54);
    const QColor surfaceMid (54,  60,  68);
    const QColor border     (74,  80,  88);
    const QColor text       (225, 230, 235);
    const QColor textDim    (170, 175, 182);
    const QColor accent     (95,  155, 230);
    const QColor accentText (235, 240, 248);

    p.setColor(QPalette::Window,           surface);
    p.setColor(QPalette::WindowText,       text);
    p.setColor(QPalette::Base,             base);
    p.setColor(QPalette::AlternateBase,    baseAlt);
    p.setColor(QPalette::ToolTipBase,      surfaceMid);
    p.setColor(QPalette::ToolTipText,      text);
    p.setColor(QPalette::Text,             text);
    p.setColor(QPalette::Button,           surfaceMid);
    p.setColor(QPalette::ButtonText,       text);
    p.setColor(QPalette::BrightText,       QColor(255, 90, 90));
    p.setColor(QPalette::Highlight,        accent);
    p.setColor(QPalette::HighlightedText,  accentText);
    p.setColor(QPalette::Link,             accent);
    p.setColor(QPalette::PlaceholderText,  textDim);

    // Disabled variants for grayed-out controls.
    p.setColor(QPalette::Disabled, QPalette::Text,        textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,  textDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText,  textDim);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, textDim);
    p.setColor(QPalette::Disabled, QPalette::Highlight,   border);

    Q_UNUSED(border);
    return p;
}

// A small stylesheet to polish the bits the palette can't quite reach
// (separator borders, toolbar height, summary banner, headers).
const char kDarkStylesheet[] = R"(
QToolBar {
    background: #2C3036;
    border: 0;
    padding: 4px 6px;
    spacing: 4px;
}
QToolBar QLabel { color: #B6BCC4; }
QPushButton {
    background: #3A4049;
    color: #E2E6EA;
    border: 1px solid #4A5058;
    border-radius: 3px;
    padding: 4px 10px;
}
QPushButton:hover  { background: #475060; }
QPushButton:pressed{ background: #2F3540; }
QPushButton:disabled {
    background: #2A2E33;
    color: #6A707A;
    border-color: #353A40;
}
QComboBox {
    background: #1F2329;
    color: #E2E6EA;
    border: 1px solid #4A5058;
    border-radius: 3px;
    padding: 3px 6px;
}
QComboBox QAbstractItemView {
    background: #2A2F36;
    selection-background-color: #5F9BE6;
    selection-color: #ffffff;
    border: 1px solid #4A5058;
}
QTableWidget QHeaderView::section {
    background: #E8ECF2;
    color: #1F3A8A;
    padding: 4px 6px;
    border: 0;
    border-right: 1px solid #C8CFD8;
    border-bottom: 1px solid #C8CFD8;
    font-weight: bold;
}
QTreeWidget QHeaderView::section {
    background: #3A4049;
    color: #C8CDD3;
    padding: 4px 6px;
    border: 0;
    border-bottom: 1px solid #2A2E33;
    font-weight: bold;
}
QTableWidget {
    background: #F5F6F8;
    alternate-background-color: #ECEEF2;
    gridline-color: #C8CFD8;
    color: #1A202C;
    selection-background-color: #B6CFF7;
    selection-color: #1A202C;
}
QTableCornerButton::section {
    background: #E8ECF2;
    border: 0;
    border-right: 1px solid #C8CFD8;
    border-bottom: 1px solid #C8CFD8;
}
QTreeWidget {
    background: #1F2329;
    color: #E2E6EA;
    border: 0;
    selection-background-color: #5F9BE6;
    selection-color: #ffffff;
}
QTreeWidget::item:hover { background: #2A2F36; }
QSplitter::handle { background: #2A2E33; }
QStatusBar { background: #2C3036; color: #B6BCC4; }
QStatusBar::item { border: 0; }
QTabWidget::pane { border: 1px solid #2A2E33; background: #1F2329; }
QTabBar::tab {
    background: #2C3036;
    color: #B6BCC4;
    padding: 6px 14px;
    border: 1px solid #2A2E33;
    border-bottom: 0;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
}
QTabBar::tab:selected {
    background: #1F2329;
    color: #E2E6EA;
}
QLabel#summaryLabel {
    background: #232830;
    color: #C8CDD3;
    border-bottom: 1px solid #2A2E33;
}
)";

} // namespace

namespace cli {

// Headless mode used when --list / --dump / --apply-stage /
// --verify-checksum is present on the command line. Returns the exit
// code; main() returns this directly without showing the window.
//
// Notes:
//   - --apply-stage writes to --out (defaults to <bin>_modified.bin).
//   - --verify-checksum reports per-range OK/MISMATCH.
//   - --repair-checksum applies repair to --out (or in-place if --out
//     not given AND --inplace is set).
//   - All operations use the same StagePackage / Checksum / DriverNames
//     code as the GUI - this is the entire point of putting the CLI in
//     the same translation unit, no behaviour drift.
int run(const QCoreApplication &app)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("EcuParser CLI: parse drivers, dump maps, apply stages, "
                       "verify/repair checksums."));
    cli.addHelpOption();
    cli.addVersionOption();

    QCommandLineOption optDriver({QStringLiteral("d"), QStringLiteral("driver")},
        QStringLiteral("Path to .drt or .xdf driver file"), QStringLiteral("file"));
    QCommandLineOption optBin({QStringLiteral("b"), QStringLiteral("bin")},
        QStringLiteral("Path to ECU .bin file"), QStringLiteral("file"));
    QCommandLineOption optList(QStringLiteral("list"),
        QStringLiteral("List all maps in the driver and exit"));
    QCommandLineOption optDump(QStringLiteral("dump"),
        QStringLiteral("Dump every map's first instance (8x8 preview)"));
    QCommandLineOption optApplyStage(QStringLiteral("apply-stage"),
        QStringLiteral("Apply a stage JSON file to the bin"), QStringLiteral("file"));
    QCommandLineOption optOut({QStringLiteral("o"), QStringLiteral("out")},
        QStringLiteral("Output bin path (default: <bin>_modified.bin)"),
        QStringLiteral("file"));
    QCommandLineOption optVerify(QStringLiteral("verify-checksum"),
        QStringLiteral("Verify every checksum range in the schema's profile"));
    QCommandLineOption optRepair(QStringLiteral("repair-checksum"),
        QStringLiteral("Repair every checksum range and write to --out"));
    QCommandLineOption optInplace(QStringLiteral("inplace"),
        QStringLiteral("With --repair-checksum, overwrite the input bin"));
    cli.addOption(optDriver);
    cli.addOption(optBin);
    cli.addOption(optList);
    cli.addOption(optDump);
    cli.addOption(optApplyStage);
    cli.addOption(optOut);
    cli.addOption(optVerify);
    cli.addOption(optRepair);
    cli.addOption(optInplace);
    cli.process(app);

    const QString driverPath = cli.value(optDriver);
    const QString binPath    = cli.value(optBin);
    const QString stagePath  = cli.value(optApplyStage);
    const QString outPath    = cli.value(optOut);

    // Parse driver if given. Required for --list, --dump, --apply-stage.
    std::optional<EcuParser::DriverModel> driver;
    if (!driverPath.isEmpty()) {
        QString perr;
        const QString ext = QFileInfo(driverPath).suffix().toLower();
        if (ext == QStringLiteral("xdf"))
            driver = EcuParser::XdfParser::parseFile(driverPath, &perr);
        else
            driver = EcuParser::DrtParser::parseFile(driverPath, &perr);
        if (!driver) {
            err << "driver parse failed: " << perr << "\n";
            return 2;
        }
        // Stitch in physical-unit overrides so --dump / --list show
        // unit info next to raw values.
        for (auto &m : driver->maps)
            EcuParser::DriverNames::applyUnitOverride(driver->schemaId, &m);
    }

    EcuParser::BinFile bin;
    if (!binPath.isEmpty()) {
        QString lerr;
        if (!bin.loadFile(binPath, &lerr)) {
            err << "bin load failed: " << lerr << "\n";
            return 2;
        }
    }

    // --list
    if (cli.isSet(optList)) {
        if (!driver) {
            err << "--list requires --driver\n";
            return 2;
        }
        out << "Schema:    " << driver->schemaId << "\n"
            << "Maps:      " << driver->maps.size() << "\n";
        for (const auto &m : driver->maps) {
            const auto name = EcuParser::DriverNames::displayName(driver->schemaId, m);
            const int dx = EcuParser::DriverNames::effectiveDimX(driver->schemaId, m);
            const int dy = EcuParser::DriverNames::effectiveDimY(driver->schemaId, m);
            QStringList addrs;
            for (auto a : m.addresses) {
                const QString hex = QStringLiteral("%1")
                    .arg(a, 6, 16, QLatin1Char('0')).toUpper();
                addrs.append(QStringLiteral("0x") + hex);
            }
            out << QStringLiteral("  %1   %2x%3   %4   unit=%5\n")
                       .arg(name, -42)
                       .arg(dx, 2).arg(dy, -2)
                       .arg(addrs.join(QStringLiteral(",")), -22)
                       .arg(m.unit.isEmpty() ? QStringLiteral("-") : m.unit);
        }
        return 0;
    }

    // --dump
    if (cli.isSet(optDump)) {
        if (!driver || binPath.isEmpty()) {
            err << "--dump requires --driver and --bin\n";
            return 2;
        }
        for (const auto &m : driver->maps) {
            const auto name = EcuParser::DriverNames::displayName(driver->schemaId, m);
            const int dx = EcuParser::DriverNames::effectiveDimX(driver->schemaId, m);
            const int dy = EcuParser::DriverNames::effectiveDimY(driver->schemaId, m);
            if (m.addresses.isEmpty() || dx <= 0 || dy <= 0) continue;
            const auto data = EcuParser::readMapInstance(bin, m, 0, dx, dy);
            if (data.cells.isEmpty()) continue;
            out << "\n" << name << "  " << dx << "x" << dy
                << "  min=" << data.minValue()
                << "  max=" << data.maxValue()
                << "  mean=" << QString::number(data.meanValue(), 'f', 1) << "\n";
            const int rmax = qMin(dx, 8);
            const int cmax = qMin(dy, 8);
            for (int r = 0; r < rmax; ++r) {
                out << "  ";
                for (int c = 0; c < cmax; ++c)
                    out << QStringLiteral("%1 ").arg(data.cells.at(r * dy + c), 6);
                if (cmax < dy) out << "...";
                out << "\n";
            }
            if (rmax < dx) out << "  ...\n";
        }
        return 0;
    }

    // --apply-stage
    if (cli.isSet(optApplyStage)) {
        if (!driver || binPath.isEmpty() || stagePath.isEmpty()) {
            err << "--apply-stage requires --driver, --bin and --apply-stage <json>\n";
            return 2;
        }
        EcuParser::StagePackage pkg;
        QString perr;
        if (!EcuParser::StagePackage::loadFromJson(stagePath, &pkg, &perr)) {
            err << "stage load failed: " << perr << "\n";
            return 2;
        }
        // Fold default-on options into the effective edits list, the
        // same way the GUI's option checkboxes do (defaulted on). The
        // CLI doesn't currently let users opt-out per option - that's
        // a future flag (e.g. --no-option egr_off). For now the rule
        // is: every option with default=true is applied. This matches
        // what the bundled reference outputs (293-822_stage1_egr_off.bin
        // includes the default-on egr_off block).
        EcuParser::StagePackage effective = pkg;
        for (const EcuParser::StageOption &opt : pkg.options) {
            if (opt.defaultOn)
                effective.edits.append(opt.edits);
        }
        effective.options.clear();
        // Always start from a fresh copy of the original (idempotent).
        EcuParser::BinFile target(bin.raw());
        // Set up protected-region snapshots so the calibration
        // checksum word at 0x07BD7C and other identification stamps
        // are preserved verbatim from the source bin into the saved
        // modified bin. Same logic the GUI uses (see
        // MainWindow::refreshProtectedSnapshots). Without this the
        // CLI-produced bins would have stock byte content for the
        // checksum word - which is the desired outcome - but only by
        // coincidence (since stage edits don't address that range).
        // The explicit setup makes the guarantee robust against future
        // stage authors who add edits in that area, and against any
        // future ChecksumDialog-style "repair" path that might also
        // be invoked from the CLI.
        const EcuParser::ChecksumProfile prof =
            EcuParser::Checksum::profileForSchema(driver->schemaId);
        if (!prof.protectedRegions.isEmpty()) {
            QList<QPair<quint32, quint32>> regions;
            QStringList descs;
            for (const EcuParser::ProtectedRegion &pr : prof.protectedRegions) {
                regions.append({pr.startOffset, pr.endOffset});
                descs.append(pr.description);
            }
            target.setProtectedSnapshots(bin.raw(), regions, descs);
        }
        QStringList warns;
        const int written = EcuParser::applyStage(effective, *driver, bin, &target, &warns);
        for (const auto &w : warns) err << "warn: " << w << "\n";
        QString destPath = outPath;
        if (destPath.isEmpty()) {
            QFileInfo fi(binPath);
            destPath = fi.absolutePath() + QStringLiteral("/")
                       + fi.completeBaseName() + QStringLiteral("_modified.")
                       + fi.suffix();
        }
        QString serr;
        if (!target.saveFile(destPath, &serr)) {
            err << "save failed: " << serr << "\n";
            return 2;
        }
        out << "Applied '" << pkg.name << "': " << written
            << " cells written to " << destPath << "\n";
        return 0;
    }

    // --verify-checksum
    if (cli.isSet(optVerify)) {
        if (binPath.isEmpty()) {
            err << "--verify-checksum requires --bin\n";
            return 2;
        }
        const QString schema = driver ? driver->schemaId : QString();
        const auto profile = EcuParser::Checksum::profileForSchema(schema);
        if (profile.ranges.isEmpty()) {
            err << "no checksum profile for schema '" << schema << "'\n";
            return 3;
        }
        const auto status = EcuParser::Checksum::verify(bin, profile);
        for (int i = 0; i < profile.ranges.size(); ++i) {
            const auto &r = profile.ranges.at(i);
            const QString hexStored = QStringLiteral("%1")
                .arg(status.stored.at(i), 8, 16, QLatin1Char('0')).toUpper();
            const QString hexComputed = QStringLiteral("%1")
                .arg(status.computed.at(i), 8, 16, QLatin1Char('0')).toUpper();
            out << QStringLiteral("  [%1] %2  stored=0x%3 computed=0x%4  %5\n")
                       .arg(i)
                       .arg(r.description, -42)
                       .arg(hexStored, hexComputed,
                            status.ok.at(i) ? QStringLiteral("OK")
                                            : QStringLiteral("MISMATCH"));
        }
        return status.allOk() ? 0 : 1;
    }

    // --repair-checksum
    if (cli.isSet(optRepair)) {
        if (binPath.isEmpty()) {
            err << "--repair-checksum requires --bin\n";
            return 2;
        }
        const QString schema = driver ? driver->schemaId : QString();
        const auto profile = EcuParser::Checksum::profileForSchema(schema);
        if (profile.ranges.isEmpty()) {
            err << "no checksum profile for schema '" << schema << "'\n";
            return 3;
        }
        QStringList logLines;
        const int n = EcuParser::Checksum::repair(&bin, profile, /*dryRun=*/false, &logLines);
        for (const auto &ln : logLines) out << "  " << ln << "\n";
        QString destPath = outPath;
        if (destPath.isEmpty()) {
            if (cli.isSet(optInplace)) destPath = binPath;
            else {
                err << "--repair-checksum: pass --out or --inplace\n";
                return 2;
            }
        }
        QString serr;
        if (!bin.saveFile(destPath, &serr)) {
            err << "save failed: " << serr << "\n";
            return 2;
        }
        out << "Repaired " << n << " ranges, written to " << destPath << "\n";
        return 0;
    }

    // No CLI verb chosen but --bin / --driver given without --list / --dump /
    // --apply-stage / --verify / --repair: probably user error. We bail
    // rather than silently dropping into the GUI.
    err << "No action specified. See --help.\n";
    return 2;
}

// Returns true if any CLI verb is on the command line. Used to decide
// whether to launch the GUI or run headless. We check argv directly
// rather than building a QCommandLineParser here because the parser
// hard-exits on --help, and we want the GUI to keep working when
// double-clicked with no args.
bool wantsCliMode(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromUtf8(argv[i]);
        if (a == QStringLiteral("--list")
            || a == QStringLiteral("--dump")
            || a == QStringLiteral("--apply-stage")
            || a == QStringLiteral("--verify-checksum")
            || a == QStringLiteral("--repair-checksum")
            || a == QStringLiteral("--help")
            || a == QStringLiteral("-h")
            || a == QStringLiteral("--version"))
            return true;
    }
    return false;
}

} // namespace cli

int main(int argc, char *argv[])
{
    // CLI mode: if any --list/--dump/--apply-stage/--verify-checksum/
    // --repair-checksum/--help is present, run headless via
    // QCoreApplication and exit before showing the GUI window.
    // Useful for batch tune apply, smoke tests, and CI regression.
    if (cli::wantsCliMode(argc, argv)) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("EcuParser"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.3"));
        return cli::run(app);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EcuParser"));
    QApplication::setApplicationVersion(QStringLiteral("0.3"));
    QApplication::setOrganizationName(QStringLiteral("EcuParser"));

    // Force Fusion + dark palette so the look is consistent across
    // Windows / macOS / Linux.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(makeDarkPalette());
    qApp->setStyleSheet(QString::fromLatin1(kDarkStylesheet));

    EcuParser::MainWindow w;
    w.show();
    return app.exec();
}
