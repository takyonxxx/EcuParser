# EcuParser

A Qt/C++ desktop tool for inspecting and editing Bosch EDC15C diesel ECU calibration files. Targeted at the Jeep WJ 2.7 CRD (Mercedes OM612 engine, schema `28F0_100`), but extensible to other EDC15C variants by supplying a matching driver file.

EcuParser parses calibration drivers (`.drt` and `.xdf` formats), reads and edits the maps they describe inside a binary firmware dump, and writes a modified bin while preserving the bytes that matter for ECU validation.

---

## Supported ECUs

EcuParser autodetects the ECU family from the bin signature and decides which driver(s) it can use. Four families are recognised today:

| Family | ECU | Bin size | Schema id | Status |
|---|---|---|---|---|
| **Jeep WJ 2.7 CRD** (1999-2004 Grand Cherokee, Mercedes OM612 5-cyl diesel, Avrupa) | Bosch EDC15C | 512 KiB | `28F0_100` | ✓ Full driver shipped (`data/xdf/28F0_100.xdf`); maps named, units calibrated, stage packages applicable |
| **GM PCM 0411** (2002+ GM trucks/SUVs, LM4/LM7/LQ4 V8) | Motorola 68k | 512 KiB | `GM_0411_OS<n>` | ⚠ Detected and named; bring your own driver (XML XDF or DRT). No shipped stages. |
| **GM E40 PCM** (2003-2005 GTO, CTS-V, etc.) | PowerPC | 1 / 1.25 MiB | `GM_E40_OS<n>` | ⚠ Detected and named; bring your own driver. Phoenix XDFs (community-distributed) work. No shipped stages. |
| **Chrysler JTEC / JTEC+** (1996-2004 WJ 4.0L/4.7L benzin, TJ Wrangler, Dakota, Durango, Viper) | MC68HC16Z2 | 256 / 512 KiB | `Chrysler_JTEC_<partno>` | ⚠ Detected via 10-char part-number scan (`5604xxxxxx`); bring your own driver. Public JTEC XDFs are scarce - try jeepforum.com, jeepstrokers.com, or commercial tuners (FRP, Syked, B&G). No shipped stages. |

Where `<n>` is the 8-digit GM OS number and `<partno>` is the Chrysler service part number, both read directly from the bin.

### What about other Jeep WJ variants?

The shipped driver covers **only the WJ 2.7 CRD diesel** (Mercedes OM612 engine, sold mostly in European markets). Other WJ trims use entirely different ECUs:

| Trim | ECU | Status |
|---|---|---|
| WJ 2.7 CRD (OM612, Avrupa) | Bosch EDC15C | ✓ Full support, schema `28F0_100` |
| WJ 4.0L I6 benzin | Chrysler JTEC / JTEC+ | ⚠ Bin auto-detected (`Chrysler_JTEC_56044xxxxx`), but you must supply a driver yourself - public XDFs are scarce |
| WJ 4.7L V8 benzin | Chrysler JTEC | ⚠ Same as 4.0L: detected, no shipped driver |
| WJ 4.7L V8 HO (2002+) | Chrysler NGC | ✗ Not detected - bin format differs from JTEC |
| WJ 3.1L TD (early VM Motori R425/R428) | Bosch EDC (different schema) | ✗ Not supported - farklı motor (295 Nm/175 PS), farklı kalibrasyon |

The three sub-revisions of the WJ 2.7 CRD calibration (`J293_822`, `J094_704`, `J409_438`) all share schema `28F0_100` and the same map addresses, so a single driver covers all of them. The differences between sub-revisions live in the **code region** of the ECU image (bytes outside the calibration table), which EcuParser doesn't try to interpret.

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

**Scope of the shipped stages.** All four stage packages target schema `28F0_100` only — the Bosch EDC15C calibration used in the 1999-2004 Jeep Grand Cherokee WJ 2.7 CRD with the Mercedes-Benz OM612 five-cylinder diesel (163 PS / 400 Nm). Their map names, addresses, value ranges, and the tuning logic itself (torque limiter shape, rail pressure atomisation, cruise-band injection, EGR strategy) are specific to this engine and ECU. Stages cannot be applied to other schemas — when a different driver is loaded the **Apply Stage** button is greyed out and its tooltip explains why. Other schemas (GM PCM 0411, GM E40, etc.) can still browse and edit maps freely, and users can build their own tune packages with the **Custom tune editor** that match the loaded driver's map names.

### Custom tune editor
Build a tune inline by adding edits one at a time: pick a map, pick a region (rows/cols), enter a percentage or absolute target, optional cap. Save as JSON and re-apply later or share with another bin. Custom tunes are not constrained to a particular schema — the saved JSON declares which schema(s) it applies to, so it shows up in the Apply Stage picker only when a compatible driver is loaded.

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

## Recent changes

### Schema family and multi-ECU support
- `BinFile::detectSchema()` now recognises three ECU families:
  - **Bosch EDC15C 28F0_100** (Jeep WJ 2.7 CRD OM612, 512 KiB)
  - **GM PCM 0411** (Motorola 68k-based GM trucks/SUVs, 512 KiB) — schema id `GM_0411_OS<n>` where `<n>` is the 8-digit OS number read from offset `0x504`
  - **GM E40** (PowerPC-based 2003-2005 GTO etc., 1 MiB or 1.25 MiB with slave chip) — schema id `GM_E40_OS<n>` where `<n>` is the 8-digit OS-id ASCII string read from offset `0x1FE9E`
- Driver auto-pick is now family-aware: a GM E40 bin will accept any driver whose schema id contains a matching 8-digit OS number, even from a different revision (with a non-blocking warning that some addresses can shift between OS revisions).

### Apply Stage scope clarified
- The shipped stage packages target schema `28F0_100` only. Other schemas grey out the **Apply Stage** button with a tooltip explaining why (custom tunes built via the Custom tune editor still work for any schema).

### Improved error messages
- Encrypted XDF files (TunerPro RT password-protected) and legacy binary XDFs are now detected up front and produce a specific error pointing the user to the right action ("open in TunerPro RT, save unencrypted, then load that copy") instead of the generic "incorrectly encoded content" message.

### Diff summary
- Coloured HTML chips for the headline metrics (amber peak torque, pink peak power, red/green fuel use direction).
- Estimated fuel-consumption projection added using a 5-category weighted model with cruise-zone-aware main injection blending.
- Torque scale fixed to OM612 reference (400 Nm / 7500 raw, 163 PS @ 4000 rpm) — earlier versions used a placeholder VM Motori reference (295 Nm / 175 PS).

### 3D surface view
- Original mesh redrawn as a vivid cyan-blue wireframe (1.6 px stroke, both row and column lines) for clear contrast against the modified surface.
- 1D maps (torque limiter 19×1) now render as a four-cell-wide ribbon instead of degenerating to a single line.

### Stage catalogue
- Two new stage packages: **Economy (Soft)** targeting daily-driver fuel saving with peak torque preserved, and **Economy (Hard)** targeting aggressive fuel saving with reduced peak torque and dramatically reduced upper-RPM power. Both keep EGR active.
- Stage picker order: performance stages (Stage 1, Stage 2) appear first, economy variants follow, anything else last — alphabetical within each group.

### Localisation
- All UI strings are now English; project-specific references removed from comments.

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
