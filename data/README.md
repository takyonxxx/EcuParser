# Test data

Three Jeep WJ 2.7 CRD calibrations and their tuning samples. All three
ECU dumps are EDC15C, schema 28F0_100, 524288 bytes (512 KB). The DRT
files are byte-identical across the three calibrations because the map
layout is the same; only the SW revision differs.

## Drivers

- `J293_822.drt` - Jeep WJ 2.7 CRD, SW revision F_02 (HW BB119988)
- `J094_704.drt` - Jeep WJ 2.7 CRD, SW revision F_03 (HW CC119988)
- `J409_438.drt` - Jeep WJ 2.7 CRD, SW revision F_03 (HW CC119988)

## Original bins

- `293-822.bin` - SW F_02 stock dump
- `094-704.bin` - SW F_03 stock dump
- `409-438.bin` - SW F_03 stock dump (different model year than 094-704)

## Tuned bins

- `293-822_stage1_egr_off.bin` - Stage1 + EGR off, derived from
  `293-822.bin`. 720 bytes differ from the original (44 cells in
  injection at part throttle, plus changes in rail pressure and
  torque limiter).
- `409-438_stage1_tuned.bin` - Stage1 tune derived from `409-438.bin`.
  497 bytes differ from the original.

## Pairing reference

| Driver         | Original bin      | Tuned bin                       |
|----------------|-------------------|---------------------------------|
| `J293_822.drt` | `293-822.bin`     | `293-822_stage1_egr_off.bin`    |
| `J094_704.drt` | `094-704.bin`     | -                               |
| `J409_438.drt` | `409-438.bin`     | `409-438_stage1_tuned.bin`      |
