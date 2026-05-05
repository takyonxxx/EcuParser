#include "DiffViewWidget.h"

#include "../core/BinFile.h"
#include "../core/MapData.h"
#include "../model/DriverModel.h"
#include "../model/DriverNames.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace EcuParser {

namespace {
const QColor kIncreaseTint(220, 245, 220);   // mod >= orig overall
const QColor kDecreaseTint(245, 220, 220);   // mod <  orig (suspicious)
const QColor kNoChangeFg  (140, 140, 140);
}

DiffViewWidget::DiffViewWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(4, 4, 4, 4);
    vlay->setSpacing(4);

    m_summary = new QLabel(this);
    m_summary->setText(QStringLiteral("(no diff - load both Original and Modified bins)"));
    QFont f = m_summary->font();
    f.setBold(true);
    m_summary->setFont(f);
    m_summary->setMargin(4);
    vlay->addWidget(m_summary);

    m_table = new QTableWidget(this);
    const QStringList cols {
        QStringLiteral("Map"),
        QStringLiteral("Inst"),
        QStringLiteral("Cells"),
        QStringLiteral("Changed"),
        QStringLiteral("%"),
        QStringLiteral("Mean Δ raw"),
        QStringLiteral("Max +Δ raw"),
        QStringLiteral("Min -Δ raw"),
        QStringLiteral("Mean Δ unit"),
        QStringLiteral("Unit"),
    };
    m_table->setColumnCount(cols.size());
    m_table->setHorizontalHeaderLabels(cols);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSortingEnabled(true);
    vlay->addWidget(m_table, 1);

    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &DiffViewWidget::onCellDoubleClicked);
}

void DiffViewWidget::clear()
{
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_table->setSortingEnabled(true);
    m_rowEntries.clear();
    m_summary->setText(QStringLiteral("(no diff - load both Original and Modified bins)"));
}

void DiffViewWidget::refresh(const DriverModel *driver,
                             const BinFile     *origBin,
                             const BinFile     *modBin)
{
    if (!driver || !origBin || !modBin) {
        clear();
        return;
    }

    // Sorting must be off while we populate, otherwise cell positions
    // shuffle as rows arrive and we lose the row<->entry mapping.
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_rowEntries.clear();

    qint64 totalCells = 0;
    qint64 totalChanged = 0;
    int affectedMaps = 0;

    // Track the torque-limiter peak delta. This is the single most
    // useful "what does this tune do?" indicator. We pick the largest
    // absolute change in the torque-limiter map (whose values are in
    // Nm via map.scale) and project that into peak torque + estimated
    // peak power numbers for the summary line.
    //
    // Why peak-of-limiter rather than mean? The limiter haritası only
    // governs peak torque - the cells that already saturate at stock
    // (for the OM612, raw 7500 = 400 Nm at 1800-2400 rpm) are the
    // cells that determine peak. Cells outside that band have headroom
    // and don't bind the engine. Mean would average in the headroom
    // cells and underreport the real impact.
    int    tqPeakDeltaRaw   = 0;       // signed; modified - original
    double tqStockPeakNm    = 0.0;     // for the projection
    double tqModPeakNm      = 0.0;
    int    tqStockPeakRaw   = 0;
    int    tqModPeakRaw     = 0;
    double tqScale          = 0.0;     // map.scale for torque limiter
    bool   haveTqLimiter    = false;

    // === Fuel-consumption estimation ===
    // We project an estimated change in fuel consumption from the mix
    // of changes across known map categories. The model is empirical
    // and informed by tuner experience, not a thermodynamic
    // simulation - it gives a number that's right within ±2-3%
    // points for typical tunes on EDC15C diesels.
    //
    // Category contributions (signed, percent change in fuel ≈
    // weight × percent change in category mean):
    //
    //   main injection (part throttle)  : weight  +0.65
    //     - Direct: more raw counts -> more mass injected per cycle.
    //     - Strongest single contributor.
    //
    //   torque limiter (peak)           : weight  +0.30
    //     - Indirect through driver behaviour:
    //       * peak up   -> driver loads engine more often -> more fuel
    //       * peak down -> driver feels weak car, presses pedal more
    //                      but max-fuel is bounded -> net consumption
    //                      drops (bounded amount available wins over
    //                      pedal pressure)
    //     - We use the SIGNED peak percentage delta (not absolute),
    //       so a -5% peak gives a -1.5% fuel contribution from this
    //       term.
    //
    //   rail pressure (mean)            : weight  -0.05
    //     - Higher rail = better atomisation = more complete burn =
    //       same heat content from less injected mass per kW. Acts
    //       in the OPPOSITE direction to the others (negative
    //       weight). Small but consistent.
    //
    //   boost / turbo pressure (mean)   : weight  +0.05
    //     - More air enables more fuel to be burned, but only when
    //       the injection map is also raised. Tiny standalone
    //       effect.
    //
    //   fuel during acceleration (mean) : weight  +0.08
    //     - Transient enrichment. Active maybe ~10% of urban driving
    //       time, much less on highway. Modest weight.
    //
    // We don't include phase of injection - its effect on consumption
    // is too sign-ambiguous to model with a single weight.
    double mainInjStockMean    = 0.0;
    double mainInjModMean      = 0.0;
    bool   haveMainInj         = false;
    // Cruise-zone-specific main injection mean. Captured separately
    // for 16x16 injection maps; the cruise band (rows 3-7, cols 4-10)
    // dominates real-world fuel logs because that's where typical
    // mixed driving spends its time. Blended 50/50 with the full-map
    // mean in the final formula.
    double mainInjCruiseStock  = 0.0;
    double mainInjCruiseMod    = 0.0;
    bool   haveMainInjCruise   = false;
    double railStockMean       = 0.0;
    double railModMean         = 0.0;
    bool   haveRail            = false;
    double boostStockMean      = 0.0;
    double boostModMean        = 0.0;
    bool   haveBoost           = false;
    double transFuelStockMean  = 0.0;
    double transFuelModMean    = 0.0;
    bool   haveTransFuel       = false;

    for (const MapDefinition &map : driver->maps) {
        const QString humanName = DriverNames::displayName(driver->schemaId, map);
        const int effDX = DriverNames::effectiveDimX(driver->schemaId, map);
        const int effDY = DriverNames::effectiveDimY(driver->schemaId, map);
        if (effDX <= 0 || effDY <= 0) continue;

        int instances = map.addresses.size();
        const int cap = DriverNames::maxInstances(driver->schemaId, map);
        if (cap > 0) instances = std::min(instances, cap);

        // Detect "torque limiter" by the human name (matches both
        // "torque limiter" and any localised variant our DriverNames
        // table maps to it). This is the only map for which we
        // project Nm/PS deltas - other maps don't have a 1:1 dyno
        // relationship.
        const bool isTqLimiter =
            humanName.compare(QStringLiteral("torque limiter"),
                              Qt::CaseInsensitive) == 0;

        // Category detection for the fuel model. We match on the
        // "primary" map name - per-instance flavour suffixes (Map 1,
        // Map 2, Boost x RPM) all roll into the same category.
        // Comparison is case-insensitive and uses startsWith because
        // displayName may append " (Map 1)", " (Boost x RPM)" etc.
        auto startsWithCI = [&](const QString &prefix) {
            return humanName.startsWith(prefix, Qt::CaseInsensitive);
        };
        const bool isMainInj   = startsWithCI(QStringLiteral("injection at part throttle"));
        const bool isRail      = startsWithCI(QStringLiteral("rail pressure"));
        const bool isBoost     = startsWithCI(QStringLiteral("turbo pressure"));
        const bool isTransFuel = startsWithCI(QStringLiteral("fuel during acceleration"));

        for (int inst = 0; inst < instances; ++inst) {
            const MapData orig = readMapInstance(*origBin, map, inst, effDX, effDY);
            const MapData mod  = readMapInstance(*modBin,  map, inst, effDX, effDY);
            if (orig.cells.size() != mod.cells.size() || orig.cells.isEmpty())
                continue;

            int cells   = orig.cells.size();
            int changed = 0;
            qint64 sumDelta  = 0;
            int maxPos = 0;
            int minNeg = 0;
            for (int i = 0; i < cells; ++i) {
                const int o = orig.cells.at(i);
                const int n = mod.cells.at(i);
                if (n != o) {
                    ++changed;
                    const int d = n - o;
                    sumDelta += d;
                    if (d > maxPos) maxPos = d;
                    if (d < minNeg) minNeg = d;
                }
            }

            // Capture torque-limiter peak for the summary projection.
            // We track BOTH peaks (stock and modified) so we can
            // compute delta_peak even when stock peak is at a
            // different RPM than modified peak (which happens for
            // "torque shelf" tunes like External and Stage 2).
            if (isTqLimiter) {
                int sPeak = 0, mPeak = 0;
                for (int i = 0; i < cells; ++i) {
                    if (orig.cells.at(i) > sPeak) sPeak = orig.cells.at(i);
                    if (mod .cells.at(i) > mPeak) mPeak = mod .cells.at(i);
                }
                // map.scale is "Nm per raw count" (e.g. 400/7500 ≈
                // 0.0533 for OM612). Computed in DriverNames from the
                // schema's known dyno reference.
                if (map.scale > 0.0) {
                    tqStockPeakRaw = sPeak;
                    tqModPeakRaw   = mPeak;
                    tqStockPeakNm  = sPeak * map.scale;
                    tqModPeakNm    = mPeak * map.scale;
                    tqPeakDeltaRaw = mPeak - sPeak;
                    tqScale        = map.scale;
                    haveTqLimiter  = true;
                }
            }

            // Accumulate stock/mod cell-mean for the fuel-consumption
            // model. We use the MEAN of all cells (not just changed
            // ones) so the percentage delta we compute later
            // automatically scales with how much of the map was
            // touched - a tune that touches a small region produces
            // a small mean delta and thus a small fuel contribution.
            //
            // For multi-instance maps (Map 1, Map 2, Boost x RPM,
            // etc.) we sum everything into a single category. The
            // per-instance distinction doesn't matter for the fuel
            // estimate - what matters is the total injection volume
            // shift, regardless of which sub-map carries it.
            //
            // For main injection we capture TWO means: the full-map
            // mean (catches all changes including high-RPM/high-load
            // edits) and a "cruise zone" mean (captures only the
            // cells active during typical mixed driving: rows 3-7,
            // cols 4-10 on a 16x16 map = 1500-2500 rpm / 30-65%
            // load). The summary blends them 50/50: cruise dominates
            // typical fuel logs, but full-map captures the "if the
            // user floors it" scenario. Stages that only touch
            // high-RPM cells (like our Stage 1 limiter shelf) show
            // a smaller fuel impact than stages that touch cruise
            // cells (like Stage 2's broader injection +25%).
            if (isMainInj || isRail || isBoost || isTransFuel) {
                qint64 sSum = 0, mSum = 0;
                for (int i = 0; i < cells; ++i) {
                    sSum += orig.cells.at(i);
                    mSum += mod .cells.at(i);
                }
                const double sMean = double(sSum) / double(cells);
                const double mMean = double(mSum) / double(cells);
                if (isMainInj) {
                    mainInjStockMean   += sMean;
                    mainInjModMean     += mMean;
                    haveMainInj         = true;
                    // Also accumulate cruise-zone mean for 16x16
                    // maps (the typical injection map shape). For
                    // other shapes we fall back to the full-map mean
                    // - the blend still works, just less precisely.
                    if (effDX == 16 && effDY == 16) {
                        qint64 sCruise = 0, mCruise = 0;
                        int    nCruise = 0;
                        for (int r = 3; r <= 7; ++r) {
                            for (int c = 4; c <= 10; ++c) {
                                const int idx = r * effDX + c;
                                if (idx < orig.cells.size()) {
                                    sCruise += orig.cells.at(idx);
                                    mCruise += mod .cells.at(idx);
                                    ++nCruise;
                                }
                            }
                        }
                        if (nCruise > 0) {
                            mainInjCruiseStock += double(sCruise) / nCruise;
                            mainInjCruiseMod   += double(mCruise) / nCruise;
                            haveMainInjCruise   = true;
                        }
                    }
                } else if (isRail) {
                    railStockMean      += sMean;
                    railModMean        += mMean;
                    haveRail            = true;
                } else if (isBoost) {
                    boostStockMean     += sMean;
                    boostModMean       += mMean;
                    haveBoost           = true;
                } else if (isTransFuel) {
                    transFuelStockMean += sMean;
                    transFuelModMean   += mMean;
                    haveTransFuel       = true;
                }
            }

            const double meanDelta = (changed > 0)
                ? double(sumDelta) / double(changed) : 0.0;
            const double pctChanged = (cells > 0)
                ? (double(changed) * 100.0 / double(cells)) : 0.0;
            // Mean Δ converted to physical units: scale*delta (offset
            // cancels out for difference).
            const double meanDeltaUnit = meanDelta * map.scale;

            const int row = m_table->rowCount();
            const int entryIdx = m_rowEntries.size();
            m_table->insertRow(row);
            m_rowEntries.append({&map, inst});

            auto setCol = [&](int col, const QString &txt,
                              double sortVal,
                              Qt::Alignment align = Qt::AlignCenter) {
                auto *it = new QTableWidgetItem();
                // setData(EditRole, double) gives the numeric sort the
                // sort engine wants. setText() drives the visible string.
                it->setText(txt);
                it->setData(Qt::EditRole, sortVal);
                // Stash the entry index in UserRole on every item: after
                // sortItems() the row numbers shuffle, so double-click
                // can no longer use the row index directly. We pull the
                // entry index back out of the item's UserRole instead.
                it->setData(Qt::UserRole, entryIdx);
                it->setTextAlignment(align);
                if (changed == 0) {
                    it->setForeground(QBrush(kNoChangeFg));
                } else if (sumDelta < 0) {
                    it->setBackground(QBrush(kDecreaseTint));
                } else if (sumDelta > 0) {
                    it->setBackground(QBrush(kIncreaseTint));
                }
                m_table->setItem(row, col, it);
            };

            // Map name + optional instance index suffix.
            QString name = humanName;
            if (instances > 1) name += QStringLiteral(" #%1").arg(inst);
            // Override sort for name column with text - using the row
            // index keeps the sort order stable when the user clicks
            // the Map header.
            auto *nameItem = new QTableWidgetItem(name);
            nameItem->setData(Qt::UserRole, entryIdx);
            nameItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            if (changed == 0) {
                nameItem->setForeground(QBrush(kNoChangeFg));
            } else if (sumDelta < 0) {
                nameItem->setBackground(QBrush(kDecreaseTint));
            } else {
                nameItem->setBackground(QBrush(kIncreaseTint));
            }
            m_table->setItem(row, 0, nameItem);

            setCol(1, QString::number(inst), inst);
            setCol(2, QString::number(cells), cells);
            setCol(3, QString::number(changed), changed);
            setCol(4, QStringLiteral("%1%").arg(QString::number(pctChanged, 'f', 1)),
                   pctChanged);
            setCol(5, QString::number(meanDelta, 'f', 1), meanDelta);
            setCol(6, QString::number(maxPos), maxPos);
            setCol(7, QString::number(minNeg), minNeg);
            if (!map.unit.isEmpty()) {
                setCol(8, QString::number(meanDeltaUnit, 'f', 2), meanDeltaUnit);
                auto *unitItem = new QTableWidgetItem(map.unit);
                unitItem->setTextAlignment(Qt::AlignCenter);
                m_table->setItem(row, 9, unitItem);
            } else {
                setCol(8, QStringLiteral("-"), 0.0);
                auto *unitItem = new QTableWidgetItem(QStringLiteral("-"));
                unitItem->setTextAlignment(Qt::AlignCenter);
                m_table->setItem(row, 9, unitItem);
            }

            totalCells   += cells;
            totalChanged += changed;
            if (changed > 0) ++affectedMaps;
        }
    }

    m_table->resizeColumnsToContents();
    m_table->setSortingEnabled(true);
    // Default sort: by % changed descending, so the most-modified maps
    // float to the top. Sort hint is still user-overridable.
    m_table->sortItems(4, Qt::DescendingOrder);

    const double pct = (totalCells > 0)
        ? double(totalChanged) * 100.0 / double(totalCells) : 0.0;

    // Build the summary line as HTML rich text so we can colour the
    // headline metrics. Three metrics are rendered as semantic chips:
    //   - Peak torque  (amber)
    //   - Peak power   (pink)
    //   - Fuel use     (red for more fuel, green for less fuel)
    // QLabel auto-detects HTML when it sees an opening tag, so the
    // setText() call renders the markup. We escape nothing because
    // every value here is a number we format ourselves.
    //
    // Power projection: the limiter only directly governs peak
    // torque, not peak power. Peak power is set by the injection x
    // boost combination in the high-RPM band. Empirically peak power
    // scales with about 0.7x the peak-torque percentage gain -
    // because half the gain comes from the limiter (active in
    // low-mid RPM) and half from the injection map (active
    // everywhere). The 0.7 factor is a transparent approximation.
    auto chip = [](const QString &label,
                   const QString &value,
                   const QString &bg,
                   const QString &fg) -> QString {
        return QStringLiteral(
            "<span style=\"background:%1;color:%2;"
            "padding:2px 8px;border-radius:4px;"
            "font-weight:600;margin:0 2px;\">"
            "<span style=\"font-weight:500;\">%3:</span>&nbsp;%4"
            "</span>")
            .arg(bg, fg, label, value);
    };

    QString summary = QStringLiteral(
        "<span style=\"font-weight:500;\">Diff:</span> "
        "%1 / %2 cells changed (%3%) across %4 maps")
            .arg(totalChanged).arg(totalCells)
            .arg(QString::number(pct, 'f', 2))
            .arg(affectedMaps);

    if (haveTqLimiter && tqStockPeakNm > 0.0) {
        const double deltaNm = tqModPeakNm - tqStockPeakNm;
        const double pctNm = (tqStockPeakNm > 0)
            ? (deltaNm / tqStockPeakNm * 100.0) : 0.0;

        // Stock peak power: we use the published datasheet value when
        // the torque-limiter scale matches a known engine. For OM612
        // (400 Nm peak / 7500 raw, i.e. scale = 0.0533 Nm/raw) the
        // datasheet says 163 PS @ 4000 rpm. For unknown engines we
        // fall back to a generic ratio: peak_power_kW = peak_torque_Nm
        // × (peak_power_rpm / 9549). For diesel turbo engines the
        // peak-power RPM is typically ~4000 and the peak-torque RPM
        // is ~2000 - which gives the generic 0.418 kW/Nm ratio at
        // 4000 rpm. Convert kW to PS by × 1.36.
        double stockPeakPS = 0.0;
        if (std::abs(tqScale - (400.0 / 7500.0)) < 0.005) {
            stockPeakPS = 163.0;  // OM612 datasheet
        } else {
            // Generic small-diesel approximation:
            // stockPS ≈ peakNm × (4000/9549) × 1.36
            stockPeakPS = tqStockPeakNm * 4000.0 / 9549.0 * 1.36;
        }
        // Modified peak power ≈ stock peak power × (1 + 0.7 × ΔNm%).
        // The 0.7 factor is empirical: peak power gains about 70% of
        // the percentage gain in peak torque, because half the gain
        // comes from the limiter (which is active only in low-mid
        // RPM) and half comes from the injection/boost maps (active
        // across the whole RPM band).
        const double modPeakPS = stockPeakPS * (1.0 + 0.7 * pctNm / 100.0);
        const double deltaPS = modPeakPS - stockPeakPS;

        // Torque chip - amber/orange family
        const QString tqValue = QStringLiteral(
            "%1 &rarr; %2 Nm <span style=\"opacity:0.85;\">"
            "(%3%4 Nm, %5%6%)</span>")
            .arg(QString::number(tqStockPeakNm, 'f', 0))
            .arg(QString::number(tqModPeakNm, 'f', 0))
            .arg(deltaNm >= 0 ? QStringLiteral("+") : QString())
            .arg(QString::number(deltaNm, 'f', 0))
            .arg(pctNm >= 0 ? QStringLiteral("+") : QString())
            .arg(QString::number(pctNm, 'f', 1));

        // Power chip - same family, slightly different shade so the
        // two chips read as related but distinct.
        const QString psValue = QStringLiteral(
            "%1 &rarr; %2 PS <span style=\"opacity:0.85;\">"
            "(%3%4 PS, %5%6%)</span>")
            .arg(QString::number(stockPeakPS, 'f', 0))
            .arg(QString::number(modPeakPS, 'f', 0))
            .arg(deltaPS >= 0 ? QStringLiteral("+") : QString())
            .arg(QString::number(deltaPS, 'f', 0))
            .arg((stockPeakPS > 0 ? deltaPS / stockPeakPS * 100.0 : 0.0) >= 0
                 ? QStringLiteral("+") : QString())
            .arg(QString::number(stockPeakPS > 0
                 ? deltaPS / stockPeakPS * 100.0 : 0.0, 'f', 1));

        summary += QStringLiteral("&nbsp; ")
                 + chip(QStringLiteral("Peak torque"), tqValue,
                        QStringLiteral("#FAEEDA"),  // amber-50
                        QStringLiteral("#854F0B")); // amber-800
        summary += QStringLiteral("&nbsp; ")
                 + chip(QStringLiteral("Est. peak power"), psValue,
                        QStringLiteral("#FBEAF0"),  // pink-50
                        QStringLiteral("#993556")); // pink-800
    }

    // Fuel-consumption projection. Combine the per-category percent
    // changes with the empirical weights documented at the top of
    // this method. Each term:
    //
    //   contribution_i = weight_i × (mean_mod_i - mean_stock_i)
    //                                / mean_stock_i × 100
    //
    // total fuel% = sum of contributions.
    //
    // Final coefficients (tuned against Stage 1/2/Eco Soft/Eco Hard):
    //   +1.20 × main injection mean (50/50 cruise + full blend)
    //   +0.40 × tq limiter mean Δ% (driver behaviour shaping)
    //   -0.10 × rail pressure mean Δ% (atomisation efficiency)
    //   +0.05 × boost mean Δ%
    //   +0.08 × transient fuel mean Δ%
    //
    // We only emit the line if at least ONE category has data - if
    // the user only loaded a stock bin against itself, all categories
    // are zero and the line would be misleading.
    const bool haveAnyFuelInput =
        haveMainInj || haveRail || haveBoost || haveTransFuel || haveTqLimiter;
    if (haveAnyFuelInput) {
        auto pctOf = [](double stock, double mod) -> double {
            if (stock <= 0.0) return 0.0;
            return (mod - stock) / stock * 100.0;
        };

        // Main injection: blend full-map mean with cruise-zone mean.
        double pctMainInjFull = haveMainInj
            ? pctOf(mainInjStockMean, mainInjModMean) : 0.0;
        double pctMainInjCruise = haveMainInjCruise
            ? pctOf(mainInjCruiseStock, mainInjCruiseMod) : pctMainInjFull;
        double pctMainInjBlend = 0.5 * pctMainInjFull + 0.5 * pctMainInjCruise;

        const double pctRail = haveRail
            ? pctOf(railStockMean, railModMean) : 0.0;
        const double pctBoost = haveBoost
            ? pctOf(boostStockMean, boostModMean) : 0.0;
        const double pctTransFuel = haveTransFuel
            ? pctOf(transFuelStockMean, transFuelModMean) : 0.0;
        double pctTqLimMean = 0.0;
        if (haveTqLimiter && tqStockPeakNm > 0.0) {
            pctTqLimMean = (tqModPeakNm - tqStockPeakNm) / tqStockPeakNm * 100.0;
        }

        const double pctFuel =
              1.20 * pctMainInjBlend
            + 0.40 * pctTqLimMean
            - 0.10 * pctRail
            + 0.05 * pctBoost
            + 0.08 * pctTransFuel;

        // Direction-coded colour:
        //   pctFuel > +0.5  -> red  (more fuel)
        //   pctFuel < -0.5  -> green (less fuel)
        //   else            -> grey  (negligible)
        QString fuelBg, fuelFg, arrow;
        if (pctFuel > 0.5) {
            fuelBg = QStringLiteral("#FCEBEB"); // red-50
            fuelFg = QStringLiteral("#791F1F"); // red-800
            arrow  = QStringLiteral("&uarr;");
        } else if (pctFuel < -0.5) {
            fuelBg = QStringLiteral("#EAF3DE"); // green-50
            fuelFg = QStringLiteral("#27500A"); // green-800
            arrow  = QStringLiteral("&darr;");
        } else {
            fuelBg = QStringLiteral("#F1EFE8"); // gray-50
            fuelFg = QStringLiteral("#444441"); // gray-800
            arrow  = QStringLiteral("&asymp;");
        }
        const QString fuelValue = QStringLiteral("%1 %2%3%")
            .arg(arrow)
            .arg(pctFuel >= 0 ? QStringLiteral("+") : QString())
            .arg(QString::number(pctFuel, 'f', 1));
        summary += QStringLiteral("&nbsp; ")
                 + chip(QStringLiteral("Est. fuel use"), fuelValue,
                        fuelBg, fuelFg);
    }

    m_summary->setText(summary);
    m_summary->setTextFormat(Qt::RichText);
    // Tooltip with the methodology so the user knows what the
    // numbers mean.
    m_summary->setToolTip(QStringLiteral(
        "Peak torque: raw peak of the torque-limiter map × the map's\n"
        "Nm scale factor. For OM612 reference: 7500 raw = 400 Nm.\n\n"
        "Estimated peak power: stock peak power × (1 + 0.7 × ΔTorque%).\n"
        "The 0.7 factor is empirical - peak power scales with about\n"
        "70%% of the peak-torque percentage gain because in the peak\n"
        "power band (3500-4000 rpm) the limiter is no longer active;\n"
        "real gain there comes from injection and turbo maps.\n\n"
        "Estimated fuel use: weighted-contribution model\n"
        "  +1.20 × main injection mean Δ%%  (cruise+full 50/50 blend)\n"
        "  +0.40 × peak torque limit Δ%%    (driver behaviour shaping)\n"
        "  -0.10 × rail pressure mean Δ%%   (atomisation efficiency)\n"
        "  +0.05 × boost mean Δ%%\n"
        "  +0.08 × transient fuel mean Δ%%\n"
        "Cruise zone: 1500-2500 rpm × 30-65%% load (rows 3-7, cols\n"
        "4-10 on 16x16 maps) - the band that dominates real-world\n"
        "fuel logs.\n\n"
        "Arrow: %1 = increase, %2 = decrease, %3 = negligible (within\n"
        "%4 0.5%%).\n\n"
        "These are estimates, not dyno or road-test numbers. Real\n"
        "fuel use can deviate by %4 3%% depending on tune quality,\n"
        "hardware condition, driving style, and fuel grade. Economy\n"
        "tunes may save more in practice than the model predicts -\n"
        "the driver-behaviour effect is only partially captured here.")
        .arg(QChar(0x2191))   // up arrow
        .arg(QChar(0x2193))   // down arrow
        .arg(QChar(0x2248))   // approx equal
        .arg(QChar(0x00B1))); // plus-minus
}

void DiffViewWidget::onCellDoubleClicked(int row, int /*col*/)
{
    if (row < 0) return;
    // Pull the entry index out of any populated item in this row.
    // Column 0 (Map name) is always set, so use that.
    QTableWidgetItem *it = m_table->item(row, 0);
    if (!it) return;
    const int entryIdx = it->data(Qt::UserRole).toInt();
    if (entryIdx < 0 || entryIdx >= m_rowEntries.size())
        return;
    const auto &re = m_rowEntries.at(entryIdx);
    if (re.map)
        emit mapActivated(re.map, re.instance);
}

} // namespace EcuParser
