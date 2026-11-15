# Controls

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **MODE**: STEREO / MONO | 13 | STEREO shows separate L and R needles. MONO shows one large needle reading `0.5·(L+R)`. This only changes the display; the audio stays stereo. |
| **CALIBRATION**: −20 / −18 / −14 dBFS | 14 | Sets which sine level reads 0 VU. A 1 kHz sine whose **peak** is at the chosen level reads 0 VU. |
| **PEAK**: ON / OFF | 15 | ON holds a CLIP lamp for 2 s after a clipped sample. OFF lights it only while clipping happens. |
| **LIGHT**: WARM / COOL / OFF / REACT / CYCLE | 16 | Dial illumination only. **REACT** colours the dials and the cabinet halo by how hot the program is: deep blue when quiet, ice blue, then amber, red at about 0 VU, magenta, and purple when pinned at +3. It warms up in about 120 ms and cools over about 0.9 s, like a glowing filament, so it breathes with the music. It follows the loudest visible needle (L/R in STEREO, the sum in MONO). **CYCLE** travels once round the colour wheel every **two bars**, in 36 steps. While the song plays it locks to the song position, so the same colour lands on the same beat of every two-bar phrase, in any tempo and time signature. Pressing Play glides it onto the beat instead of jumping. When the transport is stopped it keeps cycling at the project tempo. |
| **BYPASS** | 9 | Output = input, bit for bit (trim ignored). The meters then show the input. |
| **OUTPUT TRIM** ±12 dB | 10 | Output gain, smoothed over 20 ms. At 0.0 dB the audio passes through bit-exact. The meters show the trimmed output, so turn it until program peaks hover around 0 VU. |
| CLIP lamps (tap) | – | Lights when a sample reaches full scale (\|x\| ≥ 1.0, sample peak, not true peak). Tap the lamp to reset it. In MONO, INPUT CLIP lights from L or R even if the sum cancels. |

## What a VU meter shows

A VU meter shows average program level with deliberately slow ballistics: about 300 ms to reach a steady level. It does **not** show instantaneous peaks. A drum hit can clip while the needle barely moves, so the CLIP lamps watch the sample peaks separately.
