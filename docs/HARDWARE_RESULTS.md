# Hardware results

Run the checklist in `SPIKE.md` and record each result here.

| Date | Device | MPC OS | Build (git hash) | Check # | Result | Notes / screenshot |
|---|---|---|---|---|---|---|
| 2026-11-15 | MPC Live III | | f2f172b | 16, 17, 24, 25, 26 (visual) | Pass | METER, LUFS and SETUP render correctly; needles aligned on the large and LUFS dials; data-wheel focus ring shows; −10 dBFS calibration and plate; LUFS readouts, AUTO IN RANGE and LIMITER live. Screenshots: `images/mpc-meter.png`, `images/mpc-lufs.png`, `images/mpc-setup.png` |
| | MPC Live III | | | | | |
| | MPC XL | | | | | |

## Desktop verification (not hardware)

- `core_tests`: 44/44. This covers calibration ±0.2 VU at every reference, the scale points, step response (99 % at 300.0 ms, 1.30 % overshoot at 44.1/48/96 kHz), mono sum, anti-phase cancellation, square vs sine (average-responding, not RMS), NaN safety, the clip latch and trim/bypass bit-exactness.
- `host_sim`: 20/20, with no warnings.
- `scenario_host`: 37/37. This runs the real binary and covers bit-exact pass-through in both views, trim and bypass, display traffic (at most 64 writes/s), sector invariants, clip lamps, save/reload and multiple instances.
