# Controls

## METER tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **MODE**: STEREO / MONO | 13 | STEREO shows separate L and R needles. MONO shows one large needle reading `0.5·(L+R)`. This only changes the display; the audio stays stereo. |
| **CALIBRATION**: −20 / −18 / −14 dBFS | 14 | Sets which sine level reads 0 VU. A 1 kHz sine whose **peak** is at the chosen level reads 0 VU. |
| **PEAK**: ON / OFF | 15 | ON holds a CLIP lamp for 2 s after a clipped sample. OFF lights it only while clipping happens. |
| **LIGHT**: WARM / COOL / OFF / REACT / CYCLE | 16 | Dial illumination only. **REACT** colours the dials and the cabinet halo by how hot the program is. In the default Thermal palette it runs deep blue when quiet, ice blue, then amber, red at about 0 VU, magenta, and purple when pinned at +3. It warms up in about 120 ms and cools over about 0.9 s, like a glowing filament, so it breathes with the music. It follows the loudest visible needle (L/R in STEREO, the sum in MONO). **CYCLE** travels once round the colour wheel every **two bars** by default (set 1/2/4/8 on SETUP), in 36 steps. While the song plays it locks to the song position, so the same colour lands on the same beat of every two-bar phrase, in any tempo and time signature. Pressing Play glides it onto the beat instead of jumping. When the transport is stopped it keeps cycling at the project tempo. |
| **BYPASS** | 9 | Output = input, bit for bit (trim ignored). The meters then show the input. |
| **OUTPUT TRIM** ±12 dB | 10 | Output gain, smoothed over 20 ms. At 0.0 dB the audio passes through bit-exact. The meters show the trimmed output, so turn it until program peaks hover around 0 VU. |
| **AUTO** (OUTPUT card) | – | Tap while the music plays. It listens for 3 s (the button glows), then sets TRIM so the music **averages 0 VU**. Quiet gaps are ignored, silence changes nothing, and a second tap cancels. |
| CLIP lamps (tap) | – | Light when a sample reaches the CLIP LEVEL (sample peak, not true peak). Tap a lamp to reset it **and the MAX readings**. In MONO, INPUT CLIP lights from L or R even if the sum cancels. In REACT/CYCLE a clip also **flashes the cabinet white for one beat**. |
| **MAX** readouts (in each dial) | – | The highest reading each needle has shown since the last lamp tap. It includes the needle's real overshoot. |
| **PHASE** meter (bottom rail) | – | Stereo correlation: **+1** L and R identical (mono-safe), **0** wide/unrelated, **−1** cancels in mono (red zone). The marker parks with no signal. Shown in both views. |

## SETUP tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **CLIP LEVEL**: 0 / −0.3 / −1 / −3 dBFS | 13 | The level that lights the CLIP lamps. Use −1 or −3 for headroom before resampling or bouncing. |
| **CYCLE RATE**: 1 / 2 / 4 / 8 BARS | 14 | How long one trip round the colour wheel takes in CYCLE. |
| **REACT PALETTE**: THERMAL / NEON / AMBER | 15 | REACT colours. Thermal runs blue → red → purple; Neon runs cyan → violet → hot magenta; Amber runs dim ember → gold → white-hot. |

## What a VU meter shows

A VU meter shows average program level with deliberately slow ballistics: about 300 ms to reach a steady level. It does **not** show instantaneous peaks. A drum hit can clip while the needle barely moves, so the CLIP lamps watch the sample peaks separately.
