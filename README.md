# EcuParser

A Qt/C++ desktop tool for inspecting and editing Bosch EDC15C diesel ECU calibration files for the **Jeep Grand Cherokee WJ 2.7 CRD** (Mercedes OM612 5-cylinder diesel, schema `28F0_100`).

EcuParser parses calibration drivers (`.drt` and `.xdf` formats), reads and edits the maps they describe inside a binary firmware dump, and writes a modified bin while preserving the bytes that matter for ECU validation.

---

## Supported ECU

Only one ECU is supported: the **Bosch EDC15C** as fitted to the 1999-2004 Jeep Grand Cherokee WJ 2.7 CRD with the Mercedes OM612 five-cylinder diesel engine (sold mostly in European markets, 163 PS / 400 Nm).

| Property | Value |
|---|---|
| Vehicle | Jeep Grand Cherokee WJ 2.7 CRD (1999-2004) |
| Engine | Mercedes OM612 5-cyl diesel, 163 PS / 400 Nm |
| ECU | Bosch EDC15C |
| Bin size | 512 KiB |
| Schema id | `28F0_100` |
| Shipped driver | `data/drivers/28F0_100.xdf` (also `J293_822.drt`) |
| Shipped stages | Stage 1, Stage 2, Economy Soft, Economy Hard |

The three known sub-revisions of the WJ 2.7 CRD calibration (`J293_822`, `J094_704`, `J409_438`) all share schema `28F0_100` and the same map addresses, so a single driver covers all of them. The differences between sub-revisions live in the **code region** of the ECU image (bytes outside the calibration table), which EcuParser doesn't try to interpret.

EcuParser autodetects schema `28F0_100` from a loaded bin by checking the file size (must be exactly 512 KiB) and sampling four known map addresses — when at least three values fall in the expected ranges, the schema is recognised and the matching driver is auto-picked from `data/drivers/`.

---

## Capabilities

### Driver parsing
- Native parser for `.drt` files (custom format with map name, address, dimensions, and axes).
- TunerPro `.xdf` parser, including the `MATH` equation field for physical-unit conversion.
- Auto-detects the schema from the loaded bin and suggests a matching driver from the data directory.

### Map editing
- Browse all maps the driver describes in a tree grouped by category (Injection, Turbo, Limiters).
- Edit cells in a 2D table view with min/max/mean readouts and an optional physical-unit overlay (e.g. `7500 raw → 400 Nm`).
- Smoothing tools: linear gradient between selected cells, scale by percentage, fill region with a constant.
- Full undo/redo stack.
- Original vs Modified mirror: any edit shows a coloured delta against the original bin.

### Visualisation
- 2D graph view per map (line for 1D, contour for 2D).
- 3D surface view: filled colour-graded mesh for the modified map plus a vivid blue wireframe overlay for the original. 1D maps are drawn as a four-cell-wide ribbon so torque-limiter shape changes are visible. Mouse: left-drag to rotate, right-drag to pan, wheel to zoom.
- Hex tab: ECM Titanium-style raw byte editor with inline edit, ASCII gutter, Original-vs-Modified diff highlight, Go to address (Ctrl+G), and Find with hex pattern + wildcard (Ctrl+F / F3).

### Diff overview
- Tab listing every map and how much the modified bin diverges from the original: cells touched, percentage changed, mean delta, max/min delta, mean delta in physical units.
- Coloured summary line at the top with three semantic chips:
  - **Peak torque** — stock → modified Nm with absolute and percentage delta (amber).
  - **Est. peak power** — stock → modified PS, projected from the torque-limiter peak using `stockPS × (1 + 0.7 × ΔTorque%)` (pink).
  - **Est. fuel use** — projected fuel consumption change (red for increase, green for decrease, gray for negligible). Computed from a weighted-contribution model:

| Contribution | Weight |
|---|---|
| Main injection mean Δ% (50/50 cruise + full-map blend) | +1.20 |
| Peak torque limit Δ% (driver-behaviour shaping) | +0.40 |
| Rail pressure mean Δ% (atomisation efficiency, inverse) | −0.10 |
| Boost mean Δ% | +0.05 |
| Transient fuel mean Δ% | +0.08 |

The cruise zone (rows 3-7, cols 4-10 on a 16×16 map ≈ 1500-2500 rpm × 30-65% load) is the band where typical mixed driving spends its time, so it dominates the fuel projection.

### Stage packages
Pre-built tune profiles that apply a coordinated set of map edits in one click. Four packages ship by default:

| Stage | Peak torque | Est. peak power | Est. fuel | Notes |
|---|---|---|---|---|
| Stage 1 (Black Pearl HO) | 400 → 453 Nm | 163 → 178 PS | ↑ +7% | Hardware-safe, factory-validated target |
| Stage 2 (market level) | 400 → 520 Nm | 163 → 197 PS | ↑ +22% | Intercooler upgrade recommended |
| Economy Soft | 400 → 400 Nm | 163 → 163 PS | ↓ −1% | Hardware-safe, EGR stays on |
| Economy Hard | 400 → 381 Nm | 163 → 158 PS | ↓ −3% | Aggressive eco character, fleet/long-haul |

Each stage exposes optional sub-edits (EGR off, soft rev-limit, highway cruise, etc.) the user can toggle before applying. Stage map names, addresses, value ranges, and tuning logic (torque limiter shape, rail pressure atomisation, cruise-band injection, EGR strategy) are specific to OM612 / 28F0_100.

### Custom tune editor
Build a tune inline by adding edits one at a time: pick a map, pick a region (rows/cols), enter a percentage or absolute target, optional cap. Save as JSON and re-apply later or share with another bin.

### Stage preview
Before committing a stage, see exactly which cells will change and by how much. The diff is rendered with the same coloured delta convention as the table editor.

### Tune logger
Every applied stage and every manual edit is recorded with timestamp, bin path, and edit summary. Persisted across sessions in SQLite. Reviewable from the Tools menu.

---

## Protected regions (checksum preservation)

The 28F0_100 calibration block has a 4-byte checksum word at offset `0x07BD7C` plus two ECU identification stamps at `0x07BFB6..0x07BFE1` (44 bytes) and `0x07FCFC..0x07FD09` (14 bytes). The checksum algorithm is a Bosch proprietary CRC variant that has not been reverse-engineered with the sample bins available; only commercial tools (CHKSuite, WinOLS, Galletto, ECU.design checksum corrector) include it.

EcuParser handles this with a defense-in-depth approach: when a bin is loaded, the bytes in the protected regions are snapshotted from the original. When the modified bin is saved, those bytes are restored verbatim from the snapshot just before the file is written. This means:

- The on-disk modified bin always has stock bytes at `0x07BD7C..0x07BD7F`, `0x07BFB6..0x07BFE1`, and `0x07FCFC..0x07FD09`.
- Stage authors cannot accidentally corrupt the checksum word — even if a stage edit addresses bytes inside a protected region, the save path silently restores them.
- Caveat: this approach works if the ECU validates the checksum word "soft" (read once, trusted) rather than "hard" (re-computed at every boot). EDC15C is generally soft-validated.

The CLI logs the protection at apply time:
```
BinFile: 3 protected snapshot(s) registered
BinFile::saveFile: restored 3 protected region(s) before writing <path>
```

---

## CLI mode

The same binary runs headless for batch workflows:

```
EcuParser --driver J293_822.drt --bin stock.bin \
          --apply-stage stages/stage1.json --out tuned.bin

EcuParser --driver J293_822.drt --bin stock.bin --list
EcuParser --driver J293_822.drt --bin stock.bin --dump 0x076D82
EcuParser --driver J293_822.drt --bin stock.bin --verify-checksum
```

---

## File structure

```
EcuParser/
├── src/
│   ├── core/         BinFile, parsers, checksum, stage application
│   ├── model/        DriverModel, MapDefinition, name overrides
│   └── gui/          MainWindow, table/graph/3D/diff/hex views, dialogs
├── data/
│   ├── drivers/      Driver files (J293_822.drt, 28F0_100.xdf)
│   ├── bin/          Sample stock bins
│   └── stages/       Stage package JSONs
└── tests/
```
