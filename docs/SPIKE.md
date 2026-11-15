# Hardware spike: needle animation and view switching

These questions can only be answered on a real MPC, ideally a Live III, and an XL if one is available. Record every result, with the build hash and screenshots, in `HARDWARE_RESULTS.md`.

## What the design assumes (from EQ Artist, Looper and Scratchivision on hardware)

- FilmStrip `Image` widgets bound to read-only parameters animate.
- `IndexedEnabling/<i>/<n>/Parameter N` shows and hides widgets.
- About 230 parameter updates/s are smooth.
- Very tall strips load, around 250 MB decoded in EQ Artist.
- Fixed-text Labels, `Path` and `Plotter` do not render, so all captions are drawn into the PNGs.

## What is new here and untested

1. **Sector needles.**
   - Each sector is a filmstrip of 33 frames, with frame 0 blank.
   - 4 sectors per needle.
   - Exactly one sector per needle shows a needle.
   - At a sector change the plugin shows the new sector before blanking the old one.
2. **Face swap.** Four full-cabinet images (1220 × 368): Warm, Cool and Off, plus one glow overlay shared by React and Cycle. The `View` parameter (10 steps) switches them through `IndexedEnabling/v/10`, and through `RangedEnabling` for the glow overlay.
3. **Three tabs.** METER, LUFS and SETUP, each with its own background.
   - **Found on the Live III (v0.3.0):** the MPC does **not** shrink an image that is larger than its bounds; it draws it at native size and crops it. The LUFS faces and needle strips are therefore rendered at their final 0.8 size. Only the soft glow strip is scaled, and only up.
   - **Found on the Live III:** Label text is drawn at about 0.65 of the requested font height. LUFS-tab Labels request `size / 0.65` (`skin_layout.label_px`).
4. **Transparent tap areas.** Transparent Buttons over the CLIP lamps fire the `Reset Clip` trigger.

5. **React and Cycle lighting.**
   - A 169-step `Glow` parameter drives the face-glow filmstrip, drawn at 1/4 size. The steps are 3 palettes × 16 heat steps, then 120 hue steps, then 1 white clip-flash frame. The glow bleeds 10 px under the bezel so its soft edge is hidden.
   - The MPC must scale them up to their bounds. It is not yet known whether it scales filmstrip frames, or how smoothly.
   - The glow sits under a full-resolution transparent overlay (bezel, scale, legends).

6. **Latency changes.** LIMITER on reports about 3.2 ms (`initialDelay` + `audioMasterIOChanged`); off reports 0. Whether the MPC re-reads latency at run time, and compensates it, is unknown.
7. **Value Labels as readouts.** The LUFS numbers are `Label`s bound to read-only parameters, updated at 10 Hz.
8. **Arrow buttons.** TARGET and CEILING arrows are toggle Buttons bound to self-resetting trigger parameters.

## Checklist

| # | Check | Pass criterion |
|---|---|---|
| 1 | Install with `scripts/deploy.sh`, restart, insert on a stereo track | Plugin listed under Slow Ops; the METER tab shows the background, faces, plate and controls |
| 2 | Play a −18 dBFS 1 kHz sine (default ref) | Both needles sit on the printed 0 tick |
| 3 | Play program material | Needles move smoothly at about 30 FPS. No tearing, no blank frames, and no doubled needle at sector boundaries (watch around −10, −3, 0 and +1) |
| 4 | Switch between METER and SETUP during playback | Both tabs draw fully; the meters keep moving when you return; audio is unaffected |
| 5 | On SETUP, tap WARM / COOL / OFF, then return to METER | The correct face appears with no flash of another face |
| 6 | On LUFS, turn the TRIM knob and its Q-Link; tap BYPASS | Value label updates; the METER needles follow the trim; bypass returns them to the input level |
| 7 | Clip the input (for example +6 dB trim on a hot loop) | The CLIP lamp lights and holds for about 2 s. Tapping the lamp clears it. Anti-phase clip still lights INPUT CLIP |
| 8 | Check memory and responsiveness | Note memory % before and after opening the plugin and after the view swaps, the page open time, and whether touch and data-wheel response stays crisp while needles move |
| 9 | Save the project, restart the MPC, reload | 0 VU, Peak, Light, Palette, Rate, Clip Level, Trim and Bypass are restored; needles start at rest |
| 10 | Run two instances on different tracks with different lights | Independent |
| 11 | Set LIGHT to REACT and play quiet, then loud, then stop | The dials glow blue when quiet, red near 0 VU and purple when pinned, then fade back to blue over about a second. The glow fills the dial windows exactly; there are no blocky edges or misaligned frames, and the printed scale stays crisp |
| 12 | Set LIGHT to CYCLE; play a sequence at 120 BPM, then stop; change the tempo and time signature | One trip round the colour wheel per 2 bars, the same colour on the same beat every phrase. Pressing Play glides onto the beat. Cycling continues at the tempo when stopped |
| 13 | (Retired in v0.3: the METER tab's one-shot AUTO trim was removed. AUTO now lives on the LUFS tab, see 17–22) | – |
| 14 | Play mono material, then wide stereo, then flip one channel's polarity | PHASE marker near +1, then near 0, then in the red near −1. With no signal the marker parks |
| 15 | Push the meter, then tap a CLIP lamp | MAX shows the highest reading (in each dial's lower-right corner) and clears on the tap. Set CLIP LEVEL to −1 on SETUP: a −0.5 dBFS peak now lights CLIP |
| 16 | Open the SETUP tab: try every CALIBRATION, CLIP LEVEL, PEAK HOLD, LIGHT, PALETTE and RATE button, and Q-Links 13–16, 9 and 10 | Settings apply on METER. In CYCLE, a clip flashes the dials white for one beat; the cabinet border never changes colour |
| 17 | Open the LUFS tab while music plays | Needles move smoothly on the smaller dials (no jaggies or misalignment), the four numbers update and are readable at arm's length, and the status row shows AUTO OFF and the elapsed time |
| 18 | Play a −23 dBFS 1 kHz sine (or a known −23 LUFS file) and tap CLEAR | INTEGRATED, SHORT-TERM and M read −23.0 (±0.1); TRUE PEAK MAX −23.0. RUN → HOLD freezes INTEGRATED, TRUE PEAK MAX and the time; CLEAR restarts them |
| 19 | Tap TARGET and CEILING arrows; use Q-Links 13/14 on the LUFS tab | TARGET steps 0.5 LUFS (−23 … −9), CEILING 0.1 dB (−6.0 … −0.1); the arrow lights while pressed |
| 20 | AUTO on with a quiet song (about −24 LUFS), TARGET −14 | LEARNING for about 3 s, then ADJUSTING and a slow rise (≤ 0.5 dB/s), then IN RANGE with about +10 dB. Stopping playback shows HOLD (SILENCE) and the gain stays. AUTO off glides back to 0 dB |
| 21 | **Latency.** Play a sequence with a click or drum on another track. Toggle LIMITER on and off during playback | No clicks at the switch. Note whether this track shifts in time against the others when LIMITER is on (≈3.2 ms; the MPC should compensate). Repeat with two instances |
| 22 | LIMITER on, TRIM +6 dB into a hot loop | GAIN REDUCTION shows a few dB, TRUE PEAK MAX stays at or below the CEILING, and AUTO shows LIMITED if it is also on. LIMITER off with the same trim shows CLIP RISK |
| 23 | Save with AUTO/LIMITER/TARGET/CEILING/HOLD set, restart, reload | Settings restored (LIMITER latency reported again); readings start empty (--.-) |
| 24 | **Data wheel.** On each tab touch a control (a button, a choice row, the TARGET/CEILING arrows, the TRIM knob), then turn the data wheel; press it | An amber ring marks the focused control; the wheel steps choices and moves TRIM/TARGET/CEILING; pressing opens the large editor; touching still toggles buttons normally. Note if the ring or the wheel does nothing (the focus wrapper is untested on hardware) |
| 25 | METER tab: turn Q-Link 15. SETUP: PEAK HOLD button and Q-Link 10; CALIBRATION −10 and −4 | TRIM moves (watch the needles). PEAK HOLD works as before. A −10 dBFS sine reads 0 VU at −10; the plate shows the choice. A project saved before this update still loads its old CALIBRATION |

## If something fails

- **Sector boundary flicker or a doubled needle.** Try 2 sectors (bigger sprites), or blank first and show second. Both are one-line changes in `SlowOpsVUMeter.cpp` and `skin_layout.py`.
- **Memory too high.** Use 96 or 64 frames per needle, or narrower sectors. `check_skins.py` prints the decoded budget.
- **IndexedEnabling with many steps is unreliable.** Gate the face images on `Light` directly instead of `View`.
- **Latency changes are not compensated.** Keep the limiter but report a constant latency whenever LIMITER *could* be used (a SETUP option), or shorten the look-ahead.
- **Glow strips are not scaled, or look blocky.** Set `REACT_GLOW_SCALE = 2` in `skin_layout.py` (about +40 MiB). If memory then gets tight, drop to two palettes.
