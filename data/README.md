# Test data

- `J293_822.drt` - Driver for the Jeep WJ 2.7 CRD (EDC15C, schema 28F0_100)
- `293-822.bin` - Original ECU dump (524288 bytes)
- `293-822_stage1_egr_off.bin` - Stage1 remap (EGR off); 720 bytes differ
  from the original, including 44 cells in "injection at part throttle"
  (high RPM x high load fueling boost) and edits in rail pressure and
  torque limiter.
