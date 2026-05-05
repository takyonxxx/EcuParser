# EcuParser

A Qt/C++ desktop tool for inspecting and editing Bosch EDC15C diesel ECU calibration files. Targeted at the Jeep WJ 2.7 CRD (Mercedes OM612 engine, schema `28F0_100`), but extensible to other EDC15C variants by supplying a matching driver file.

EcuParser parses calibration drivers (`.drt` and `.xdf` formats), reads and edits the maps they describe inside a binary firmware dump, and writes a modified bin while preserving the bytes that matter for ECU validation.

---

## Capabilities

### Driver parsing
- Native parser for `.drt` files (custom format with map name, address, dimensions, and axes).
- TunerPro `.xdf` parser, including the `MATH` equation field for physical-unit conversion.
- Auto-detects the schema from the loaded bin and suggests a matching driver from the data directory.

### Map editing
- Browse all maps the driver describes in a tree grouped by category (Injection, Turbo, Limiters, etc.).
- Edit cells in a 2D table view with min/max/mean readouts and an optional physical-unit overlay (e.g. `7500 raw → 400 Nm`).
- Smoothing tools: linear gradient between selected cells, scale by percentage, fill region with a constant.
- Full undo/redo stack.
- Original vs Modified mirror: any edit shows a coloured delta against the original bin.

### Visualisation
- 2D graph view per map (line for 1D, contour for 2D).
- 3D surface view: filled colour-graded mesh for the modified map plus a vivid blue wireframe overlay for the original. 1D maps are drawn as a four-cell-wide ribbon so torque-limiter shape changes are visible. Mouse: left-drag to rotate, right-drag to pan, wheel to zoom.

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
Pre-built tune profiles that apply a coordinated set of map edits in one click. Four packages ship by default for the OM612 / 28F0_100 schema:

| Stage | Peak torque | Est. peak power | Est. fuel | Notes |
|---|---|---|---|---|
| Stage 1 (Black Pearl HO) | 400 → 453 Nm | 163 → 178 PS | ↑ +7% | Hardware-safe, factory-validated target |
| Stage 2 (market level) | 400 → 520 Nm | 163 → 197 PS | ↑ +22% | Intercooler upgrade recommended |
| Economy Soft | 400 → 400 Nm | 163 → 163 PS | ↓ −1% | Hardware-safe, EGR stays on |
| Economy Hard | 400 → 381 Nm | 163 → 158 PS | ↓ −3% | Aggressive eco character, fleet/long-haul |

Each stage exposes optional sub-edits (EGR off, soft rev-limit, highway cruise, etc.) the user can toggle before applying.

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
- Caveat: this approach works if the ECU validates the checksum word "soft" (read once, trusted) rather than "hard" (re-computed at every boot). EDC15C is generally soft-validated; if a specific sub-revision is hard-validated, a commercial tool is required.

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

## Recent changes (v6 → v7)

- All UI strings translated to English; comments cleaned up.
- Diff summary line now uses coloured HTML chips (amber for torque, pink for power, red/green for fuel) so the headline metrics stand out against the table below.
- Estimated fuel-consumption projection added to the diff summary using a 5-category weighted model with cruise-zone-aware main injection blending.
- Torque scale fixed to OM612 reference (400 Nm / 7500 raw, 163 PS @ 4000 rpm) — earlier versions used a placeholder VM Motori reference (295 Nm / 175 PS).
- 3D surface view: original mesh redrawn as a vivid cyan-blue wireframe (1.6 px stroke, both row and column lines) for clear contrast against the modified surface. 1D maps (torque limiter 19×1) now render as a four-cell-wide ribbon instead of degenerating to a single line.
- Two new stage packages: Economy (Soft) targeting daily-driver fuel saving with peak torque preserved, and Economy (Hard) targeting aggressive fuel saving with reduced peak torque and dramatically reduced upper-RPM power. Both keep EGR active.

---

## File structure

```
EcuParser/
├── src/
│   ├── core/         BinFile, parsers, checksum, stage application
│   ├── model/        DriverModel, MapDefinition, name overrides
│   └── gui/          MainWindow, table/graph/3D/diff views, dialogs
├── data/
│   ├── *.drt         Driver files (J293_822.drt covers 28F0_100)
│   ├── stages/       Stage package JSONs
│   └── *.bin         Sample stock bins
└── tests/
```
