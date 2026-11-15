# Controls

The meter is stereo: one needle for L and one for R. Processing is opt-in: with AUTO off, TRIM at 0.0 dB and LIMITER off the audio passes through bit-exact with zero latency.

Signal chain: **in → AUTO gain + TRIM (one gain for L and R) → LIMITER → out**. Every meter (needles, CLIP, PHASE, MAX and all LUFS readouts) reads the **output**, so the METER and LUFS tabs always agree.

## METER tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **PEAK**: ON / OFF | 13 | ON holds a CLIP lamp for 2 s after a clipped sample. OFF lights it only while clipping happens. |
| **BYPASS** | 14 | Output = input, bit for bit (AUTO, TRIM and LIMITER ignored). With LIMITER on the output is still delayed by its 3.2 ms so timing does not jump. The meters then show the input. |
| **OUTPUT TRIM** ±12 dB | 15 | Output gain, smoothed over 20 ms (the same TRIM as on the LUFS tab). At 0.0 dB the audio passes through bit-exact. Turn it until the music hovers around 0 VU. |
| CLIP lamps (tap) | – | Light when a sample reaches the CLIP LEVEL (sample peak, not true peak). Tap a lamp to reset it **and the MAX readings**. In REACT or CYCLE a clip also **flashes the dials white for one beat**. |
| **MAX** readouts (in each dial) | – | The highest reading each needle has shown since the last lamp tap. It includes the needle's real overshoot. |
| **PHASE** meter (bottom rail) | – | Stereo correlation: **+1** L and R identical (mono-safe), **0** wide or unrelated, **−1** cancels in mono (red zone). The marker parks when there's no signal. |
| 0 VU plate | – | Shows the current CALIBRATION. |

## LUFS tab

Loudness per ITU-R BS.1770 / EBU R 128, measured on the **output**. The scale under the numbers is decoration; the numbers are the readings.

Layout: the readout strip holds the **measurement** controls at its left end (CLEAR, RUN/HOLD: they only affect the readings). Below it, the **GAIN** card (AUTO, TARGET, TRIM: everything that sets the level) and the **OUTPUT** card (LIMITER, CEILING: protection after the gain).

| Readout | What it shows |
|---|---|
| **INTEGRATED** (LUFS) | Programme loudness since CLEAR, gated (−70 LUFS absolute, then 10 LU below the programme), so silence and quiet gaps do not pull it down. `--.-` until 0.4 s of programme has been measured. |
| **SHORT-TERM** (LUFS) | The last 3 s, ungated, live. |
| **M** (status row) | Momentary: the last 0.4 s. |
| **TRUE PEAK MAX** (dBTP) | The highest **inter-sample** peak of the output since CLEAR (4× oversampled), so it can read higher than the sample peak the CLIP lamps watch. |
| **GAIN REDUCTION** (dB) | What the LIMITER is taking off right now; 0.0 when it is idle or off. |
| AUTO status (status row) | AUTO OFF, LEARNING, ADJUSTING, IN RANGE, HOLD (SILENCE), GAIN LIMIT (needs more than ±12 dB), LIMITED (the limiter is working hard, so the target may not be reached), or **CLIP RISK** (LIMITER off and the output went over 0 dBTP in the last 3 s). The AUTO gain applied is shown beside it. |
| Elapsed (status row) | Measuring time since CLEAR. |

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **AUTO**: ON / OFF | 16 | Off by default. Steers the **integrated** loudness towards TARGET with one gain for L and R. It listens to the **input** for at least 3 s of programme first (LEARNING), then moves at most 0.5 dB/s and only when it is more than 0.3 LU off. It holds during silence, never moves more than ±12 dB, and glides back to 0 dB when switched off. It never chases every bar: it follows the programme since CLEAR. It is a live best estimate; it cannot know what the rest of the song will do. |
| **TARGET** ◀ ▶ | 13 | −23 … −9 LUFS in 0.5 steps (default −14). A starting point for music, not a platform rule. TARGET is unrelated to the VU CALIBRATION (0 VU = −18 dBFS is a different thing). |
| **TRIM** | 15 | The same output TRIM as on METER. With AUTO on it is a deliberate offset: TARGET −14 with TRIM +1 gives about −13 LUFS. With AUTO off, turn TRIM up to drive the LIMITER on purpose. |
| **LIMITER**: ON / OFF | 9 | Off by default. A stereo-linked, true-peak-aware limiter (3 ms look-ahead, program-dependent release). It never changes the balance between L and R. **While on it adds about 3.2 ms of latency**, which is reported to the MPC. Switching it crossfades over 20 ms, so it does not click. |
| **CEILING** ◀ ▶ | 14 | −6.0 … −0.1 dBTP in 0.1 steps (default −1.0). The highest true peak the LIMITER lets through. **With LIMITER off the ceiling does nothing**; watch for CLIP RISK. |
| **CLEAR** | – | Restarts INTEGRATED, TRUE PEAK MAX, the elapsed time and AUTO's estimate. It never changes the gain, so the audio does not jump. Settings are untouched. |
| **RUN / HOLD** | 10 | HOLD freezes INTEGRATED, TRUE PEAK MAX and the time; SHORT-TERM and M stay live, and AUTO keeps working. It is a measurement hold, not a mute. |
| **BYPASS** | 11 | The same Bypass as on METER: either button switches both. Bypass skips everything (AUTO, TRIM and LIMITER); the output is the input, bit for bit. With LIMITER on the output keeps its 3.2 ms delay, so timing does not jump. |
| CLIP lamps (either side) | – | Same as on METER; tap to reset. |

Measurements are not saved with the project: after loading, every reading starts empty. Settings are saved.

## SETUP tab

| Control | Q-Link (screen) | What it does |
|---|---|---|
| **CALIBRATION**: −20 / −18 / −14 dBFS | 13 | Sets which level reads 0 VU: a 1 kHz sine whose **peak** is at the chosen level. |
| **CLIP LEVEL**: 0 / −0.3 / −1 / −3 dBFS | 14 | The level that lights the CLIP lamps. Use −1 or −3 for headroom before resampling or bouncing. |
| **LIGHT**: STATIC (WARM / COOL / OFF) | 15 | A fixed dial backlight. |
| **LIGHT**: REACT, with **PALETTE** THERMAL / NEON / AMBER | 15, 16 | Colours the dials by how loud the music is. It warms up in about 120 ms and cools over about 0.9 s, like a glowing filament. **Thermal** runs deep blue when quiet → ice blue → amber → red at about 0 VU → magenta → purple when pinned at +3. **Neon** runs cyan → violet → hot magenta. **Amber** runs dim ember → gold → white-hot. It follows the louder needle. |
| **LIGHT**: CYCLE, with **RATE** 1 / 2 / 4 / 8 BARS | 15, 9 | Travels once round the colour wheel per RATE (default 2 bars), in 120 fine steps (3° each), so even an 8-bar cycle drifts smoothly. While the song plays it locks to the song position, so the same colour lands on the same beat of every phrase, in any tempo and time signature. Pressing Play glides it onto the beat. Stopped, it keeps cycling at the project tempo. |

## What a VU meter shows

A VU meter shows average level with deliberately slow ballistics: about 300 ms to reach a steady level. It does **not** show instantaneous peaks. A drum hit can clip while the needle barely moves, so the CLIP lamps watch the sample peaks separately.
