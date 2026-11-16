# Changelog

## 1.0.1 (2026-11-16)

- Adds the MIT `LICENSE` file to the repository and to the release zip. No changes to the audio, the controls or the skin.

## 1.0.0 (2026-11-15)

First release as **LUFS VU** (formerly "VU Meter"; plugin ID `SoVM` and all parameter IDs unchanged).

- **LUFS tab**: INTEGRATED, SHORT-TERM, MOMENTARY and TRUE PEAK MAX (ITU-R BS.1770 / EBU R 128), GAIN REDUCTION, CLEAR and RUN/HOLD. Verified against EBU Tech 3341 and FFmpeg `ebur128`.
- **AUTO loudness**: steers the integrated loudness towards a TARGET (−23 … −9 LUFS), at most 0.5 dB/s, ±12 dB.
- **True-peak LIMITER** with CEILING (−6.0 … −0.1 dBTP); about 3.2 ms latency, reported to the MPC, only while it is on.
- **METER**: the walnut cabinet fills the page with larger 560 × 400 dials, a brass inlay and corner screws. Controls moved off it: PEAK HOLD to SETUP, BYPASS and TRIM to the LUFS tab (TRIM stays on Q-Link 15).
- **CALIBRATION** adds 0 VU = −10 and −4 dBFS for dense, loud masters.
- **Data wheel**: touch any control and turn the main data wheel; press it for the large editor.
- The old one-shot METER AUTO trim is retired.

## 0.2

React and Cycle lighting, PHASE meter, MAX readouts, CLIP LEVEL, SETUP tab; mono view retired.

## 0.1

Stereo VU meter with clip lamps, calibration, trim and bypass.
