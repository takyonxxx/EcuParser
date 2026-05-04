# EcuParser - Qt 6 / C++ GUI

A Qt 6 application that parses `.drt` driver files and visualises
EDC15C bin files in side-by-side comparison mode. Load an original
(stock) bin and a modified (tune) bin together, browse every map
defined by the driver as either a table or a graph, edit cells, and
export the modified bin back to disk.

## Driver formats

EcuParser reads two driver formats:

- **`.drt`** - the format we reverse-engineered ourselves; what most of
  the bundled `data/J*.drt` files use.
- **`.xdf`** - TunerPro Definition File. XML-based, open, widely shared
  in the open-source tuning community. Drop XDFs in `data/` or
  `data/xdf/` and they'll appear in the Driver combo alongside DRTs.

The XDF parser implements the subset most XDFs in the wild use:

- Top-level `<XDFHEADER>` description
- `<CATEGORY>` index/name for grouping
- `<XDFTABLE>` with `<title>`, `<description>`, `<CATEGORYMEM>`
- One `<XDFAXIS id="z">` per table carrying `<EMBEDDEDDATA>` with
  `mmedaddress`, `mmedelementsizebits`, `mmedrowcount`, `mmedcolcount`
- X and Y axes for breakpoint addresses

Currently NOT interpreted (cells are read as raw u16 LE):

- `<MATH equation="...">` formulas (raw values shown instead of
  scaled physical units)
- `<XDFCONSTANT>`, `<XDFFLAG>` (single values and bit flags)
- Per-table endianness/sign overrides (we read header `<DEFAULTS>`
  and assume LE u16)

A sample XDF for J293_822 lives at `data/xdf/J293_822_example.xdf` -
hand-crafted from the addresses we reverse-engineered, byte-identical
in behaviour to the J293_822.drt driver. Use it as a starting point
when authoring XDFs for new schemas.

## Supported driver: J293_822 / J094_704 / J409_438 (Jeep WJ 2.7 CRD, EDC15C, schema 28F0_100)

The three drivers are **byte-identical** because the map layout is the
same across all SW revisions of the Jeep 2.7 CRD; only the underlying
firmware differs. Eleven maps reverse-engineered and verified against a
real Stage1 file. The addresses and dimensions are hard-coded in
`src/model/DriverNames.cpp` because the dim fields in the `.drt` are
unreliable for some maps and need to be overridden.

| #  | Map name                                            | Address   | Size   |
|----|-----------------------------------------------------|-----------|--------|
| 1  | injection at part throttle                          | 0x076F52  | 16x16  |
| 2  | rail pressure                                       | 0x07ADD2  | 16x16  |
| 3  | injection at part throttle (Map 1)                  | 0x072CF0  | 16x20  |
| 4  | injection at part throttle (Map 2)                  | 0x072FC0  | 16x20  |
| 5  | injection at part throttle (Map 1) (Boost x RPM)    | 0x078C5A  | 16x20  |
| 6  | injection at part throttle (Map 2) (Boost x RPM)    | 0x0791FA  | 16x20  |
| 7  | phase of injection                                  | 0x071F52  | 12x16  |
| 8  | fuel during acceleration                            | 0x07753E  | 12x10  |
| 9  | fuel during acceleration (Map 2)                    | 0x07765E  | 10x10  |
| 10 | turbo pressure                                      | 0x075EA0  | 12x12  |
| 11 | torque limiter                                      | 0x076D82  | 19x1   |

Maps 5 and 6 (Boost x RPM) are listed in the `.drt` with two
instances each (the second addresses are `0x078F2A` and `0x0794CA`),
but the canonical reference behaviour shows only the first. We
preserve that with `maxInstances=1`.

Map 11 (torque limiter) is recorded in the `.drt` with a "?" type
code, an empty `name`, and `0x0` dimensions; the override in
DriverNames sets it to `19x1` so it displays correctly.

The five 16-row maps (rail pressure plus Map 1/2 plus the two
Boost x RPM variants) **share the same RPM axis**, which is not
stored in the bin but embedded in the driver:

```
700, 800, 900, 1000, 1100, 1300, 1500, 1700,
1900, 2100, 2400, 2700, 3100, 3500, 4000, 4500
```

DriverNames keeps these RPM axis values hard-coded.

## Decode rules (verified)

- **Cell endianness: little-endian** - at 0x076F52 the bytes
  `0x10 0x0E` decode to 3600 LE
- **Cell layout: idx = axisX_row * dimY + axisY_col** (row-major,
  RPM = outer index, Load = inner index)
- **Axis breakpoints are LE** as well
- **Cell size: 2 bytes (u16)**, range 0..65535
- **DRT dim fields are not always reliable** - DriverNames overrides
  pin the dim and axis values per map

## Look and feel

- **Tree sidebar (dark)**: Maps grouped by category (INJECTION / TURBO
  / LIMITERS / OTHER), sorted by canonical per-driver order.
- **Table (light theme)**: White/light background, blue header text;
  only **changed** cells are tinted (green = increase, pink = decrease)
  with an orig->mod delta in the tooltip.
- **Graph (cyan background)**: 1 px blue line (Original) and 1 px red
  line (Modified), Y axis 0..65535 (full u16) on both sides, hex
  address labels on the X axis.
- **Cursor crosshair**: Hovering over the graph snaps the crosshair to
  the nearest cell and shows a cream tooltip with **RPM / Load /
  address / Ori / Mod (delta)**.

## Features

**File handling**

- Driver / Original / Modified bin combos populated from `data/`,
  nothing loaded at startup.
- "..." browse buttons let you pick files outside `data/`.
- Selecting an Original auto-mirrors the Modified slot to the same
  file (a copy of the bin in memory; edits to Modified never alter
  Original). You can swap Modified to a different bin afterwards.
- **Export modified...** suggests `<orig>_modified.bin` as the default
  filename so you don't overwrite the original on disk.

**Editing**

- Double-click a cell to edit its value (written back as LE u16).
- Select a region in the table, **right-click -> Set value...** to
  write a single value into every selected cell at once (bulk edit).
- **Copy ORI -> MOD** restores the selected map's original values
  into the modified bin.

## Project layout

```
src/core/   DrtParser, BinFile (LE/BE read+write), MapData (LE row-major)
src/model/  DriverModel, MapDefinition, AxisDefinition, MapCategory,
            DriverNames (canonical name + override table)
src/gui/    MainWindow, DriverTreeWidget, MapTableWidget,
            MapGraphWidget, AppPaths
data/       Three Jeep 2.7 CRD calibrations + sample bins (see data/README.md)
```

## Building

`build.sh` keeps the project directory clean by writing build output
into a sibling `EcuParser-build/` directory. The .pro file routes
release and debug artefacts into separate sub-directories so both
configurations can co-exist:

```bash
./build.sh                              # release build
```

Run it from the project directory (so it can find `data/`):

```bash
cd EcuParser
../EcuParser-build/release/EcuParser
```

**Qt Creator** users: the .pro file routes per-configuration output
into the right sub-directories automatically. With a build directory
of e.g. `build-EcuParser/`, the binaries land here:

```
build-EcuParser/
|-- Makefile, Makefile.Debug, Makefile.Release
|-- debug/EcuParser(.exe)        <- Debug build output
|   `-- .obj, .moc, ...
`-- release/EcuParser(.exe)      <- Release build output
    `-- .obj, .moc, ...
```

**Manual out-of-source** build:

```bash
mkdir -p ../EcuParser-build && cd ../EcuParser-build
qmake6 ../EcuParser/EcuParser.pro CONFIG+=release
make -j4
# Binary: ./release/EcuParser
```

In-source builds (`qmake6 EcuParser.pro && make` from the project
root) still work but pollute the source tree; out-of-source is
recommended.

## Known limits

- Some maps have unreliable DRT dim or axis info because the real
  values are embedded in the driver. DriverNames lets you override
  them per map.
- Some maps have axis breakpoint counts that differ from their
  declared dim, so the trailing rows or columns may be meaningless.
- Cell values are raw u16 LE - there is no conversion into physical
  units (e.g. torque -> Nm, pressure -> mbar).
- Only u16 cell writes are supported.
