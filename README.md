# Slow Ops VU Meter (MPC)

An analog-style VU meter insert effect for Akai MPC standalone (MPC OS 3.10.1+).

- **Stereo VU meter**: two independent L/R needles. With AUTO off, TRIM at 0.0 dB and LIMITER off it only measures, and the audio passes through bit-exact with zero latency.
- **LUFS tab** (ITU-R BS.1770 / EBU R 128): INTEGRATED (gated), SHORT-TERM, MOMENTARY, TRUE PEAK MAX (4× oversampled, post-processing) and limiter GAIN REDUCTION, on a smoked-glass "radio dial" strip under the same VU meters. CLEAR and RUN/HOLD control the measurement.
- **AUTO loudness** (off by default): gently steers the programme's integrated loudness towards a TARGET (−23 … −9 LUFS, default −14). It moves at most 0.5 dB/s, holds during silence and is limited to ±12 dB. It is a best-effort live estimate, not a guarantee.
- **True-peak LIMITER** (off by default) with a CEILING of −6.0 … −0.1 dBTP (default −1.0). It is stereo-linked with a 3 ms look-ahead, which adds about 3.2 ms of latency **only while it is on**; the latency is reported to the MPC.
- **Ballistics**: full-wave average-responding with second-order needle dynamics aimed at IEC 60268-17. The design target is 99 % in 300 ms with 1.3 % overshoot, verified in `core_tests`.
- **Calibration**: 0 VU = −20 / −18 / −14 dBFS. The reference is a 1 kHz sine whose *peak* is at that level.
- **CLIP lamps**: sample-peak at a selectable CLIP LEVEL, with an optional 2 s hold. Tap a lamp to reset it. This is not true-peak detection.
- **LIGHT**: Warm, Cool, Off, **React** or **Cycle**.
  - **React** makes the dials glow blue → red → purple as the program heats up, with filament-like warm-up and cool-down. ![react](docs/images/react.gif)
  - **Cycle** sweeps round the hue wheel once every two bars, locked to the MPC's tempo and song position. ![cycle](docs/images/cycle.gif)
- **PHASE** correlation meter, **MAX** VU readouts, and a selectable **CLIP LEVEL** (0 / −0.3 / −1 / −3 dBFS).
- **Three tabs.** METER has the dials, PHASE, MAX, CLIP lamps, PEAK and BYPASS. LUFS has the loudness readouts and the AUTO, TARGET, TRIM, LIMITER, CEILING, CLEAR, RUN/HOLD and BYPASS controls. SETUP has CALIBRATION, CLIP LEVEL and a LIGHT box grouped as STATIC (warm/cool/off), REACT with its palette, and CYCLE with its rate.
- **Output TRIM** (±12 dB, smoothed; on the LUFS tab) and **BYPASS** (on both tabs, one setting). With TRIM at 0.0 dB (and AUTO off), or with Bypass on, the output is a **bit-exact** copy of the input. Every meter reads the output signal.

| METER | LUFS | SETUP |
|---|---|---|
| ![meter](docs/images/preview-stereo.png) | ![lufs](docs/images/preview-lufs.png) | ![setup](docs/images/preview-setup.png) |

*Previews are rendered on the desktop from the real skin assets. Hardware captures go in `docs/HARDWARE_RESULTS.md`.*

## Status

| | |
|---|---|
| Core engine (ballistics, calibration, clip, trim, React heat, Cycle hue, correlation, loudness, true peak, limiter, AUTO) | Tested on desktop (`core_tests`, 126 checks, including EBU Tech 3341 cases 1–5 and 9) |
| Loudness and true peak against an independent meter | `lufs_crosscheck` vs FFmpeg `ebur128`: I within 0.1 LU, TP within 0.3 dB |
| Real plugin binary through the VST2 ABI | Tested on desktop (`host_sim` 20 and `scenario_host` 101 checks) |
| Skin JSON / assets consistency | Tested on desktop (`check_skins.py`) |
| **Needle animation, view switching, memory, latency compensation on an MPC** | **Not yet tested.** See `docs/SPIKE.md` |

## Build and test

```sh
git submodule update --init           # SDK pinned at febd2ca
./scripts/test.sh                     # native + arm32 + arm64, validation, all tests (+ FFmpeg cross-check if installed)
./scripts/package.sh                  # dist/vu-meter-<version>-mpc.zip
MPC_ADDRESS=<ip> ./scripts/deploy.sh  # install over the network (developer mode) and restart
./scripts/make_art.sh                 # redraw skin PNGs + previews (creates build/venv with Pillow)
```

The output folder is `plugin/build/Slow Ops - VST - VU Meter/`.

## Install on the MPC

1. Copy `Slow Ops - VST - VU Meter` into a `VST2 Plugins` folder at the top level of a drive.
2. On MPC OS 3.10.1 or later, go to **Preferences → Enhancements (Beta)** and turn on **Enable Enhancements** and **External Plugin Support**. Restart.
3. Add **Slow Ops › VU Meter** as an insert effect. The MPC only rereads skins on restart.

## Layout

```
core/include/slowops/vumeter/   NeedleDynamics, VuEngine, PeakDetector, Trim, KWeighting, LoudnessMeter,
                                TruePeak, TruePeakLimiter, AutoLoudness (standard library only)
plugin/src/SlowOpsVUMeter.cpp   thin mpc::Plugin adapter (parameters, display publishing)
plugin/skins/                   generated and committed: TUI.json, Q-Links.json, PNGs
scripts/skin_layout.py          shared geometry: dials, tick formula, needle sectors, widget rects
scripts/make_skin_art.py        Pillow artwork (original, generated from code)
scripts/gen_skins.py            TUI.json / Q-Links.json from plugin/build/params.json
scripts/check_skins.py          skin ↔ parameter validation
tests/core_tests.cpp            engine tests (spec section 8.1)
tests/scenario_host.cpp         real-binary integration tests (spec section 8.2, LUFS scenarios)
tests/lufs_crosscheck.cpp       renders programmes for scripts/lufs_crosscheck.py (FFmpeg comparison)
```

**Needle rendering.** Each needle is split into 4 angular *sector* filmstrips. Each sector is a small transparent sprite, and frame 0 is blank. The plugin shows exactly one sector per needle. This keeps all needle art at about 22 MiB decoded. The LUFS tab has its own 0.8-size faces and needle strips (the MPC never shrinks an image) and reuses the glow strip; all images together decode to at most about 119 MiB (counted per widget, so an upper bound). Faces are swapped by a plugin-computed `View` parameter (the Light setting). The v0.1 MONO view was retired in v0.2; its parameter slots remain so indices stay stable, but they are unused.

Font: Titillium Web (SIL OFL 1.1, `assets/fonts/OFL.txt`). Licence: MIT.
