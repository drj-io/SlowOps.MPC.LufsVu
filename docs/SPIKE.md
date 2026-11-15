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
   - Stereo has 4 sectors per needle; mono has 8.
   - Exactly one sector per needle shows a needle.
   - At a sector change the plugin shows the new sector before blanking the old one.
2. **Face swap.** Eight full-cabinet images (1220 × 370): Warm, Cool and Off for each mode, plus one glow overlay per mode shared by React and Cycle. The `View` parameter (10 steps) switches them through `IndexedEnabling/v/10`, and through `RangedEnabling` for the glow overlay.
3. **Large stereo/mono swap.** 8 stereo needle widgets and 8 mono needle widgets, gated by `IndexedEnabling/<0|1>/2` on `Mode`.
4. **Transparent tap areas.** Transparent Buttons over the CLIP lamps fire the `Reset Clip` trigger.

5. **React and Cycle lighting.**
   - An 85-step `Glow` parameter drives three filmstrips: a cabinet halo strip and two face-glow strips, all drawn at 1/4 size. The steps are 3 palettes × 16 heat steps, then 36 hue steps, then 1 white clip-flash frame. The glow bleeds 10 px under the bezel so its soft edge is hidden.
   - The MPC must scale them up to their bounds. It is not yet known whether it scales filmstrip frames, or how smoothly.
   - The glow sits under a full-resolution transparent overlay (bezel, scale, legends).

## Checklist

| # | Check | Pass criterion |
|---|---|---|
| 1 | Install with `scripts/deploy.sh`, restart, insert on a stereo track | Plugin listed under Slow Ops; the METER tab shows the background, faces, plate and controls |
| 2 | Play a −18 dBFS 1 kHz sine (default ref) | Both needles sit on the printed 0 tick |
| 3 | Play program material | Needles move smoothly at about 30 FPS. No tearing, no blank frames, and no doubled needle at sector boundaries (watch around −10, −3, 0 and +1) |
| 4 | Tap MONO, then STEREO, repeatedly during playback | Instant swap to one large dial and back. Both views never show at once, and audio is unaffected |
| 5 | Tap WARM / COOL / OFF in both modes | The correct face appears with no flash of another face |
| 6 | Turn the TRIM knob and its Q-Link; tap BYPASS | Value label updates; the needle follows the trim; bypass returns the needle to the input level |
| 7 | Clip the input (for example +6 dB trim on a hot loop) | The CLIP lamp lights and holds for about 2 s. Tapping the lamp clears it. In MONO, an anti-phase clip still lights INPUT CLIP |
| 8 | Check memory and responsiveness | Note memory % before and after opening the plugin and after the view swaps, the page open time, and whether touch and data-wheel response stays crisp while needles move |
| 9 | Save the project, restart the MPC, reload | Mode, 0 VU, Peak, Light, Trim and Bypass are restored; needles start at rest |
| 10 | Run two instances on different tracks in different modes | Independent |
| 11 | Set LIGHT to REACT and play quiet, then loud, then stop | The dials and halo glow blue when quiet, red near 0 VU and purple when pinned, then fade back to blue over about a second. The glow fills the dial windows exactly; there are no blocky edges or misaligned frames, and the printed scale stays crisp |
| 12 | Set LIGHT to CYCLE; play a sequence at 120 BPM, then stop; change the tempo and time signature | One trip round the colour wheel per 2 bars, the same colour on the same beat every phrase. Pressing Play glides onto the beat. Cycling continues at the tempo when stopped |
| 13 | Play a loop at about −6 VU and tap AUTO | The button glows for about 3 s, TRIM jumps to about +6.0 dB, the needles then sit around 0 VU. A second tap during listening cancels |
| 14 | Play mono material, then wide stereo, then flip one channel's polarity | PHASE marker near +1, then near 0, then in the red near −1. With no signal the marker parks |
| 15 | Push the meter, then tap a CLIP lamp | MAX shows the highest reading (in each dial's lower-right corner) and clears on the tap. Set CLIP LEVEL to −1 on SETUP: a −0.5 dBFS peak now lights CLIP |
| 16 | Open the SETUP tab: try every CLIP LEVEL, CYCLE RATE and PALETTE button, and Q-Links 13–15 | Settings apply on METER. In CYCLE, a clip flashes the cabinet white for one beat |

## If something fails

- **Sector boundary flicker or a doubled needle.** Try 2 stereo / 4 mono sectors (bigger sprites), or blank first and show second. Both are one-line changes in `SlowOpsVUMeter.cpp` and `skin_layout.py`.
- **Memory too high.** Use 96 or 64 frames per needle, or narrower sectors. `check_skins.py` prints the decoded budget.
- **IndexedEnabling with 6 steps is unreliable.** Split `View` into Mode-gated face images plus a Light-gated overlay.
- **Glow strips are not scaled, or look blocky.** Set `REACT_GLOW_SCALE = 2` in `skin_layout.py` (about +40 MiB). If memory then gets tight, drop to two palettes.
- **Conditional swap unusable.** Last resort is two tabs (STEREO, MONO) with both views metering continuously. Document this as a compromise.
