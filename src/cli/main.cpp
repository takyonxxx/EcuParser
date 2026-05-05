#include "../core/DrtParser.h"
#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverModel.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <cstdio>

using namespace Titanium;

// Look for the bundled data/ directory next to the executable. During
// development the binary lives in <project>/build/ while data/ sits in
// <project>/data/, so we walk up one level. In a deployed build the user
// is expected to ship data/ next to the .exe.
static QString findDataDir()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        exeDir + QStringLiteral("/data"),
        exeDir + QStringLiteral("/../data"),
        exeDir + QStringLiteral("/../../data"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo(c).isDir())
            return QDir(c).absolutePath();
    }
    return QString();
}

static void printDriver(const DriverModel &drv, QTextStream &out)
{
    out << "Driver: " << drv.sourcePath << "\n";
    out << "  schemaId    : " << drv.schemaId << "\n";
    out << "  declared maps: " << drv.mapCount << "\n";
    out << "  parsed maps  : " << drv.maps.size() << "\n";
    out << "  ECU info    : type=" << drv.ecuTypeCode
        << " defaultDim=" << drv.defaultDimX << "x" << drv.defaultDimY << "\n";
    out << "\n";

    out << "Maps grouped by category:\n";
    const auto grouped = drv.mapsByCategory();
    for (const auto &pair : grouped) {
        out << "  [" << categoryDisplayName(pair.first) << "]\n";
        for (const MapDefinition *m : pair.second) {
            QString addrs;
            for (int i = 0; i < m->addresses.size(); ++i) {
                if (i) addrs += QLatin1Char(',');
                addrs += QStringLiteral("0x%1")
                            .arg(m->addresses.at(i), 6, 16, QLatin1Char('0'))
                            .toUpper();
            }
            out << "    "
                << QStringLiteral("%1 %2x%3 cell=%4 addr=%5  axisX=%6 axisY=%7  -> %8")
                       .arg(m->typeCode, -4)
                       .arg(m->dimX, 3).arg(m->dimY, 3)
                       .arg(m->cellSize)
                       .arg(addrs, -22)
                       .arg(m->axisX.toDebugString(), -16)
                       .arg(m->axisY.toDebugString(), -16)
                       .arg(m->displayName())
                << "\n";
        }
    }
}

static void dumpMap(const BinFile &bin, const MapDefinition &m,
                    int instance, QTextStream &out)
{
    if (instance < 0 || instance >= m.addresses.size()) {
        out << "  (no instance " << instance << ")\n";
        return;
    }
    const MapData data = readMapInstance(bin, m, instance);
    if (data.cells.isEmpty()) {
        out << "  (read failed - check bin size and address)\n";
        return;
    }
    out << "  Instance " << instance
        << " @ 0x" << QStringLiteral("%1").arg(m.addresses.at(instance), 6, 16, QLatin1Char('0')).toUpper()
        << "  min=" << data.minValue()
        << "  max=" << data.maxValue()
        << "  mean=" << QString::number(data.meanValue(), 'f', 1) << "\n";

    // Print up to 8x8 preview to keep terminal output readable.
    const int rmax = qMin(data.rows, 8);
    const int cmax = qMin(data.cols, 8);
    for (int r = 0; r < rmax; ++r) {
        out << "    ";
        for (int c = 0; c < cmax; ++c) {
            out << QStringLiteral("%1 ").arg(data.at(r, c), 6);
        }
        if (cmax < data.cols) out << "...";
        out << "\n";
    }
    if (rmax < data.rows) out << "    ...\n";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("EcuParser"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser cli;
    cli.setApplicationDescription(
        QStringLiteral("Parses ECM Titanium .drt drivers and dumps maps from .bin"));
    cli.addHelpOption();
    cli.addVersionOption();

    QCommandLineOption drtOpt({ QStringLiteral("d"), QStringLiteral("drt") },
                              QStringLiteral("Path to .drt driver file (required)"),
                              QStringLiteral("file"));
    QCommandLineOption binOpt({ QStringLiteral("b"), QStringLiteral("bin") },
                              QStringLiteral("Path to ECU .bin file (optional, enables dumps)"),
                              QStringLiteral("file"));
    QCommandLineOption dumpOpt(QStringLiteral("dump"),
                               QStringLiteral("Dump map instances after listing"));
    cli.addOption(drtOpt);
    cli.addOption(binOpt);
    cli.addOption(dumpOpt);
    cli.process(app);

    QTextStream out(stdout);

    // Resolve inputs: explicit args win; otherwise fall back to bundled test
    // data so the project runs out of the box in Qt Creator without manually
    // setting "Command line arguments" in the Run settings.
    QString drtPath = cli.value(drtOpt);
    QString binPath = cli.value(binOpt);
    bool wantDump   = cli.isSet(dumpOpt);

    if (drtPath.isEmpty()) {
        const QString dataDir = findDataDir();
        if (!dataDir.isEmpty()) {
            // Canonical layout puts drivers under data/drivers/ and bins
            // under data/bin/, with the legacy flat layout (everything
            // at the root of data/) as a fallback for old checkouts.
            auto firstExisting = [](const QStringList &candidates) {
                for (const QString &c : candidates)
                    if (QFileInfo(c).isFile())
                        return c;
                return QString();
            };
            const QString defDrt = firstExisting({
                dataDir + QStringLiteral("/drivers/J293_822.drt"),
                dataDir + QStringLiteral("/J293_822.drt"),
            });
            const QString defBin = firstExisting({
                dataDir + QStringLiteral("/bin/293-822.bin"),
                dataDir + QStringLiteral("/293-822.bin"),
            });
            if (!defDrt.isEmpty()) {
                drtPath = defDrt;
                if (binPath.isEmpty() && !defBin.isEmpty())
                    binPath = defBin;
                wantDump = true;
                out << "No --drt given, using bundled test data:\n";
                out << "  drt: " << drtPath << "\n";
                if (!binPath.isEmpty())
                    out << "  bin: " << binPath << "\n";
                out << "\n";
            }
        }
    }

    if (drtPath.isEmpty()) {
        QTextStream(stderr) << "error: --drt is required (and bundled data/ not found)\n";
        return 2;
    }

    QString err;
    auto drv = DrtParser::parseFile(drtPath, &err);
    if (!drv) {
        QTextStream(stderr) << "parse failed: " << err << "\n";
        return 1;
    }

    printDriver(*drv, out);

    if (!binPath.isEmpty()) {
        BinFile bin;
        QString binErr;
        if (!bin.loadFile(binPath, &binErr)) {
            QTextStream(stderr) << "bin load failed: " << binErr << "\n";
            return 1;
        }
        out << "\nBin loaded: " << bin.size() << " bytes\n";

        if (wantDump) {
            out << "\nMap dumps (first instance, up to 8x8 preview):\n";
            for (const MapDefinition &m : drv->maps) {
                out << "\n" << m.typeCode << " " << m.dimX << "x" << m.dimY
                    << " - " << m.displayName() << "\n";
                for (int i = 0; i < m.addresses.size(); ++i)
                    dumpMap(bin, m, i, out);
            }
        }
    }

    return 0;
}
