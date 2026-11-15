# Controls

The meter is stereo: one needle for L and one for R. It only measures; at 0.0 dB TRIM the audio passes through untouched.

## METER tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **PEAK**: ON / OFF | 13 | ON holds a CLIP lamp for 2 s after a clipped sample. OFF lights it only while clipping happens. |
| **BYPASS** | 14 | Output = input, bit for bit (trim ignored). The meters then show the input. |
| **OUTPUT TRIM** ±12 dB | 15 | Output gain, smoothed over 20 ms. At 0.0 dB the audio passes through bit-exact. The meters show the trimmed output, so turn it until the music hovers around 0 VU. |
| **AUTO** (OUTPUT card) | – | Tap while the music plays. It listens for 3 s (the button glows), then sets TRIM so the music **averages 0 VU**. Quiet gaps are ignored, silence changes nothing, and a second tap cancels. |
| CLIP lamps (tap) | – | Light when a sample reaches the CLIP LEVEL (sample peak, not true peak). Tap a lamp to reset it **and the MAX readings**. In REACT or CYCLE a clip also **flashes the cabinet white for one beat**. |
| **MAX** readouts (in each dial) | – | The highest reading each needle has shown since the last lamp tap. It includes the needle's real overshoot. |
| **PHASE** meter (bottom rail) | – | Stereo correlation: **+1** L and R identical (mono-safe), **0** wide or unrelated, **−1** cancels in mono (red zone). The marker parks when there's no signal. |
| 0 VU plate | – | Shows the current CALIBRATION. |

## SETUP tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **CALIBRATION**: −20 / −18 / −14 dBFS | 13 | Sets which level reads 0 VU: a 1 kHz sine whose **peak** is at the chosen level. |
| **CLIP LEVEL**: 0 / −0.3 / −1 / −3 dBFS | 14 | The level that lights the CLIP lamps. Use −1 or −3 for headroom before resampling or bouncing. |
| **LIGHT**: STATIC (WARM / COOL / OFF) | 15 | A fixed dial backlight. |
| **LIGHT**: REACT, with **PALETTE** THERMAL / NEON / AMBER | 15, 16 | Colours the dials and the cabinet halo by how loud the music is. It warms up in about 120 ms and cools over about 0.9 s, like a glowing filament. **Thermal** runs deep blue when quiet → ice blue → amber → red at about 0 VU → magenta → purple when pinned at +3. **Neon** runs cyan → violet → hot magenta. **Amber** runs dim ember → gold → white-hot. It follows the louder needle. |
| **LIGHT**: CYCLE, with **RATE** 1 / 2 / 4 / 8 BARS | 15, 9 | Travels once round the colour wheel per RATE (default 2 bars), in 36 steps. While the song plays it locks to the song position, so the same colour lands on the same beat of every phrase, in any tempo and time signature. Pressing Play glides it onto the beat. Stopped, it keeps cycling at the project tempo. |

## What a VU meter shows

A VU meter shows average level with deliberately slow ballistics: about 300 ms to reach a steady level. It does **not** show instantaneous peaks. A drum hit can clip while the needle barely moves, so the CLIP lamps watch the sample peaks separately.
