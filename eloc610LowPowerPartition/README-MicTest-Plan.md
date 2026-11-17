# Microphone self-test (`setMicTest` / `getMicTest`)

Status: **Phases 1 and 2 on branch `EDsteve/mic-test`.** The measurement came in V1.81 and was
corrected in V1.82 after the first bench test. **V1.83 adds the [verdict](#verdict-since-v183)**
(ok / broken / checkPort / suspect / repeat), with limits from the bench sessions on ELOC_00247
(seven healthy mics, two broken ones, tissue and a thumb on the port, mic unplugged; see
[Bench results](#bench-results)) and from an hour of real forest noise. A fault is confirmed by a
second run. The verdict is unit-tested and replayed on the bench data on a PC, **not yet on a
device**.

## Why

Microphones fail in the field from ants and humidity, and the failures do not look alike:

| What the recording sounds like | How often | What it means for the signal |
|---|---|---|
| Only pink noise, no real sound | most common | the mic no longer converts sound; it outputs its own noise |
| Real sound, but very faint | sometimes | sensitivity is down (blocked port, damaged membrane) |
| Crackling on top of the recording | sometimes | short spikes far above the background |
| Nothing at all | rare | data line stuck: all samples identical |

Today nothing mic-related leaves the device, so a broken mic is only found after the SD card comes
back. The goal is a check a field tech can run from the app while standing at the device.

## How the test works

The buzzer (MLT-8540H) sits on the same board as the microphone. That gives every ELOC a sound
source at a fixed distance from its own mic, so the test does not depend on the ambient sound or on
what the technician does.

1. **Listen in silence** (1.5 s). Gives the noise level, the shape of the noise spectrum, DC offset,
   stuck samples, clipping and the crackle count.
2. **Play tones and listen** (default 2700, 4000 and 5400 Hz, 0.6 s on / 0.6 s off, twice). For each
   tone the firmware compares the level at that pitch with the buzzer on against the same pitch with
   the buzzer off. The difference is the **rise**.
3. **Analyse**, then save the capture as a WAV plus the result as JSON on the SD card.

How each symptom shows up:

| Symptom | Number to look at | Expected on a broken mic |
|---|---|---|
| Only noise | `tones[].rise[dB]` | near 0 dB: the buzzer makes no difference. A healthy unit gives about 65 dB in a quiet room |
| Only noise | `tones[].off[dBFS]` | well above the healthy mic's own floor (about -104 dBFS at 2700 and 4000 Hz) |
| Faint | `tones[].rise[dB]` | clearly lower than on a healthy unit |
| Crackling | `crackle.impulses`, `crackle.crest[dB]`, `crackle.kurtosis` | more than 0, high, high |
| Nothing at all | `level.stuckRun`, `level.noise[dBFS]` | as long as the capture, -160 |
| Intermittent | `crackle.dropouts` | more than 0 |

All levels are measured on the **raw 24-bit samples before the gain shift**, so `MicVolume2_pwr`
does not change them.

The spectrum slope was meant to flag the "only pink noise" case, but it cannot: the healthy bench
mic gave -0.2 dB/oct in a quiet room, the same as pink noise (room rumble at the bottom, the mic's
own white noise at the top). The tone rise is the measure to rely on.

## Verdict (since V1.83)

**The test works best in a quiet place**, and the app should say so before it starts. In a loud
place a healthy mic gets "repeat: too noisy", never "broken".

The `getMicTest` result starts with a `verdict`:

| `result` | For the user |
|---|---|
| `ok` | the microphone is fine |
| `broken` | replace the microphone |
| `checkPort` | the mic hears, but its sound port seems blocked: check it for dirt, ants or water |
| `suspect` | clicks heard: listen to the recording |
| `repeat` | the conditions spoiled the test, not the mic: repeat as the reason says |
| `none` | silent test (`freqs=0`): no beeps, no verdict |

The checks run in this order, and the first that applies decides (`MicAnalyzer.hpp`, `judge()`).
The **rise** of a tone is its signal-to-noise ratio: how far the beep stands above the background in
its own band. The best of the three tones counts, because forest noise only ever covers some of them.

| # | `finding` | Rule | `result` | Measured on the bench and in the forest mix |
|---|---|---|---|---|
| 1 | `noSignal` | `stuckRun` above half the capture, or buzzer-off noise below -130 dBFS | broken | unplugged mic: all samples 0 |
| 2 | `buried` | best tone's rise below 30 dB: the beeps are buried in noise | broken | healthy 59–75 dB, at least 36.5 in the loudest forest minutes; #6 / #7 7–15 dB |
| 3 | `disturbed` | an octave band below 125 Hz above -45 dBFS: the device was touched or moved | repeat | healthy -80 to -92 dBFS, never in the forest; thumb -20 to -38 |
| 4 | `pressed` | loudest tone above -22 dBFS: the port is pressed or sealed (a sealed cavity amplifies) | repeat | healthy -30 to -36 dBFS; thumb -11 / -17 |
| 5 | `noisy` | quietest tone-band background above -90 dBFS: the place is loud or the mic hisses | repeat | healthy -101 to -106 dBFS; #6 / #7 -67 to -77; forest up to -64 |
| 6 | `weak` | best tone's rise below 45 dB, in a quiet background (check 5 passed): a faint mic | broken | healthy at least 53.8 dB; no faint mic tested yet |
| 7 | `portDamped` | lowest minus highest tone level above 18 dB (tones at least 1.8× apart in pitch) | checkPort | healthy 5–10 dB; tissue 30 |
| 8 | `crackle` | at least 10 impulses and kurtosis at least 20, in a quiet background | suspect | healthy 0–6 impulses; room clicks 13 (#3) |
| – | `clock` | measured sample rate more than 5 % off: every number is unreliable | repeat | |
| – | `ok` | none of the above | ok | |

A loud background (5) is ambiguous: a loud place and a hissing mic look the same. Hence "repeat in a
quiet place; if it stays noisy, the microphone is broken". A mic that hisses that badly but still
hears the beeps has not turned up yet: #6 and #7 also failed check 2.

**Confirming run.** When the first run finds anything (any result other than `ok` or `none`), the
test runs a second time straight away, so it takes about 24 s instead of 12. A fault counts only if
both runs show it, because a mic fault is there every time, while room noise comes and goes (#3):

- second run `ok`: `ok`, the first finding was not confirmed
- either run `repeat`: `repeat`, with the second run's reason if it has one
- two faults: the milder one, which both runs show at least (`suspect` < `checkPort` < `broken`)

`verdict.runs` lists each run's finding. Each run saves its own WAV and JSON; the second one's name
ends in `_run2`. The numbers in the reply are from the last run. `confirm=0` switches the second run
off, for bench measurements.

**Checked on the bench data** (`MicAnalyzer.hpp` on the PC, the nine bench mics rebuilt from their
measured numbers): the six healthy mics give `ok`, #6 and #7 `broken` / `buried`, the tissue (#9)
`checkPort`. With the forest hour mixed in, run in pairs as the confirming run would be:

| Mic | Busy first minutes (0.5–7 min) | Rest of the hour |
|---|---|---|
| healthy (6 mics) | `repeat` / `noisy` 100 % | `ok` 99 %, `repeat` / `noisy` 1 % |
| tissue on the port | `repeat` / `noisy` 100 % | `checkPort` 98 %, `repeat` / `noisy` 2 % |
| broken #6, #7 | `broken` / `buried` 100 % | `broken` / `buried` 100 % |

A healthy mic was never called broken, and a broken one never passed. The limits come from one
unit's buzzer (ELOC_00247): a unit with a quieter buzzer would lose margin on checks 2 and 6 first.

## Commands

Both work from the app's command-line screen (Device Settings → Advanced → Command line).

### `setMicTest` — start a test

Returns at once; the test itself takes about 12 s, or 14 s when it has to start the microphone,
and about twice as long when a second run confirms a finding. Optional arguments:

| Argument | Default | Range | Meaning |
|---|---|---|---|
| `freqs` | `2700,4000,5400` | 100 Hz .. 0.45 × sample rate, up to 6 | buzzer tones in Hz; `freqs=0` runs a silent test without the buzzer |
| `reps` | `2` | 1..5 | how often the tone list is repeated |
| `onMs` | `600` | 400..3000 | length of each tone |
| `gapMs` | `600` | 400..3000 | silence after each tone; the first 350 ms of it are not analysed (the tone is still fading) |
| `silenceMs` | `1500` | 500..10000 | silence before the first tone |
| `save` | `1` | 0/1 | write the WAV and the JSON to `/sdcard/mictest/` |
| `confirm` | `1` | 0/1 | run a second time when the first run finds anything (V1.83) |

Example: `setMicTest#freqs=2700#reps=3`

The defaults changed in V1.82: V1.81 used `1000,2700,4000` and a 400 ms gap (see
[Bench results](#bench-results)).

Reply: `{"state":"starting","duration[ms]":13500}`. Refused (with a reason) while another test runs,
during a firmware transfer, during a coverage survey, and while an intruder alarm or candidate is
active.

### `getMicTest` — progress and result

While the test runs: `{"state":"capturing","run":1,"progress[%]":40,"remaining[ms]":7200}`. States:
`idle`, `starting`, `capturing`, `analysing`, `saving`, `done`, `error`. When a confirming run starts,
`run` becomes 2 and the progress drops back to about half, because the planned time doubles.
Afterwards the last result stays readable until the next test or a reboot.

Since V1.83 the result starts with the verdict. For broken mic #6 it would read (replayed on the PC,
not yet from a device):

```json
"verdict": {
  "result": "broken",
  "finding": "buried",
  "reason": "beeps buried in noise: the best beep is only 15 dB above the background (limit 30) - the microphone is broken - found in both runs",
  "bestRise[dB]": 14.7,
  "background[dBFS]": -77.2,
  "runs": ["buried", "buried"]
}
```

The rest is unchanged. The real result of the first bench test (ELOC_00247, V1.81 defaults, so the
old 1000 Hz tone, before the verdict existed):

```json
{
  "state": "done",
  "test": 1,
  "time": "Tue, Nov 17 2026 16:57:30",
  "fs[Hz]": 16000,
  "fsMeasured[Hz]": 16024,
  "duration[ms]": 7936,
  "i2sWasRunning": false,
  "micVolume2_pwr": -3,
  "level": {"noise[dBFS]": -76.9, "peak[dBFS]": -32.2, "dc[dBFS]": -113.4, "clipped": 0, "stuckRun": 3},
  "spectrum": {
    "edges[Hz]": [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000],
    "bands[dBFS]": [-82.7, -90.4, -95.4, -96.4, -97.3, -96.6, -92.6, -94],
    "slope[dB/oct]": -0.2
  },
  "crackle": {"impulses": 3, "crest[dB]": 19.7, "kurtosis": 3.1, "dropouts": 0, "analysed[ms]": 3116},
  "tones": [
    {"f[Hz]": 1000, "on[dBFS]": -79.3, "off[dBFS]": -104.1, "rise[dB]": 24.8, "riseMin[dB]": 24.7,
     "peak[Hz]": 1000, "onTotal[dBFS]": -48.8},
    {"f[Hz]": 2700, "on[dBFS]": -36.2, "off[dBFS]": -103.8, "rise[dB]": 67.6, "riseMin[dB]": 67.4,
     "peak[Hz]": 2703, "onTotal[dBFS]": -35.3},
    {"f[Hz]": 4000, "on[dBFS]": -39.9, "off[dBFS]": -104.1, "rise[dB]": 64.1, "riseMin[dB]": 63.8,
     "peak[Hz]": 4000, "onTotal[dBFS]": -39.9}
  ],
  "wav": "/sdcard/mictest/ELOC_00247_20261117_165730.wav"
}
```

The 3 crackle impulses are false: they are the 2700 Hz tone fading out (see Bench results). V1.82
reports 0 for this same recording.

From V1.82 the JSON file on the SD card (not the Bluetooth reply) also carries an `analysis`
section: the guard times, the test parameters, and `marks`, the sample index of every buzzer switch
as `[on, off, tone]`. With it, the WAV can be run through `MicAnalyzer.hpp` again on a PC, to get
the same numbers or to try other guard times, without testing the mic again.

### What the fields mean

| Field | Meaning |
|---|---|
| `verdict.result` | `ok`, `broken`, `checkPort`, `suspect`, `repeat` or `none` (see [Verdict](#verdict-since-v183)) |
| `verdict.finding` | which check decided: `ok`, `noSignal`, `buried`, `disturbed`, `pressed`, `noisy`, `weak`, `portDamped`, `crackle`, `clock`, `noTone` |
| `verdict.reason` | one sentence for the user, with the number behind it |
| `verdict.bestRise[dB]` / `background[dBFS]` | the best tone's rise and the quietest tone-band background, the two numbers most checks use |
| `verdict.runs` | each run's own finding; two entries when a second run confirmed |
| `analysis.run` | SD file only: which run the file is from. The first run's file holds that run's verdict alone |
| `fsMeasured[Hz]` | sample rate measured against the RTC clock during the capture; far from `fs[Hz]` means the I2S clock is wrong and every other number is suspect |
| `i2sWasRunning` | false = the test started the microphone itself (device was idle) |
| `level.noise[dBFS]` | level with the buzzer off, DC removed. 0 dBFS = a full-scale sine, the same convention as the mic datasheet, so for the ICS-43434 dB SPL ≈ dBFS + 120. Dominated by sound below 63 Hz (room rumble, wind), so it says little about the mic; the bands and `tones[].off` say more |
| `level.peak[dBFS]` | largest sample of the whole capture, tones included |
| `level.dc[dBFS]` | DC offset with the buzzer off |
| `level.clipped` | samples at the 24-bit rails |
| `level.stuckRun` | longest run of identical consecutive samples; a healthy mic gives single digits |
| `spectrum.bands[dBFS]` | level per octave with the buzzer off; `edges[Hz]` are the band limits |
| `spectrum.slope[dB/oct]` | straight-line fit through the bands from 63 Hz up. Pink noise ≈ 0, white noise ≈ +3. Does not separate a healthy mic in a quiet room from pink noise |
| `crackle.impulses` | spikes more than 8× the typical sample-to-sample step, buzzer off; spikes within 2 ms count once |
| `crackle.crest[dB]` | largest sample-to-sample step relative to the typical one. Plain noise gives about 12–14 dB |
| `crackle.kurtosis` | excess kurtosis of the sample-to-sample steps. Plain noise ≈ 0, spiky signals are far above |
| `crackle.dropouts` | 16 ms pieces that are more than 20 dB quieter than the typical one |
| `tones[].on[dBFS]` / `off[dBFS]` | level in a ±5 % band around the tone, buzzer on / off. Both are the median over 64 ms blocks, so a few crackles or one bird call do not move them |
| `tones[].rise[dB]` | on minus off, averaged over the repetitions |
| `tones[].riseMin[dB]` | the lowest single repetition; far below `rise` means something disturbed one of them |
| `tones[].peak[Hz]` | strongest frequency inside the band while the buzzer was on |
| `tones[].onTotal[dBFS]` | level of everything the mic heard while that tone played |

## The bench session these numbers are for

1. Flash a healthy unit, connect with the app, open the command line, send `setMicTest`, wait
   15 s, send `getMicTest`. Repeat three times to see how much the numbers move.
2. Once on the healthy unit: `setMicTest#freqs=1500,2000,2700,3300,4000,5400#reps=1`. This maps
   how loud the buzzer is at each pitch, so the default tones can be chosen from data (5400 Hz is
   inferred, not measured).
3. Same unit with **tape over the mic port**. This shows how much of the buzzer reaches the mic
   through the board and the case instead of through the air.
4. Each **broken mic** available: noise-only, faint, crackling.
5. One run in a noisy place and one outdoors.
6. Copy `/sdcard/mictest/` off the card. Each test leaves a `.wav` (24-bit, the exact samples the
   numbers were computed from) and a `.json` with the result.

What the session has to answer:

- Does `rise[dB]` separate healthy from noise-only and from faint, and at which tone best?
- **Does a dead mic still show a rise?** The buzzer's drive current could reach a damaged mic's
  output through the supply instead of through the air. If it does, the rise alone is not a safe
  verdict. The spectrum slope cannot step in (see above); the tone-band background
  (`tones[].off`) and the WAVs would have to.
- Does tape over the port lower the rise enough to matter?
- Do `impulses` / `crest` / `kurtosis` pick out the crackling mics, and how often does normal sound
  (speech, rain on the case) set them off?

## Design

| Piece | File | Role |
|---|---|---|
| Analysis | `lib/audio_input/src/MicAnalyzer.hpp` | Header-only, no ESP code: takes the captured samples and the on/off schedule, returns the numbers; `judge()` / `combineRuns()` / `describe()` turn them into the verdict. Unit-tested on the PC. |
| Capture tap | `lib/audio_input/src/I2SMEMSSampler.{h,cpp}` | `set_raw_tap()`: hands every raw I2S buffer to one listener before the gain shift. One pointer check per buffer when unused. |
| Test run | `lib/Commands/src/MicTest.{hpp,cpp}` | Singleton with its own short-lived task: gets the mic running, drives the buzzer, captures to PSRAM, analyses, saves, builds the JSON. |
| Buzzer hand-over | `lib/ElocHardware/src/ElocSystem.{hpp,cpp}` | `acquireBuzzerForTest()` / `testTone()` / `releaseBuzzerForTest()`: keeps status beeps out of the test and keeps the knock guard engaged. |
| Commands | `lib/Commands/src/ElocCommands.cpp` | `setMicTest`, `getMicTest`; `setFwUpdateBegin` is refused while a test runs. |
| Main loop | `src/main.cpp` | Starts I2S when the test asks for it; no duty-cycle sleep while a test runs. |
| PC tests | `test/test_generic_mic_analyzer/` | 34 synthetic cases. Measurement: healthy, deaf (pink noise), faint, crackling, deaf and crackling, a fading buzzer tail, dead, stuck, DC, clipping, off-pitch, dropouts, 8 kHz, segment building, truncated capture. Verdict (V1.83): every finding from bench-like scenarios, a faint mic in noise ("repeat", not "broken"), one tone drowned by insects ("ok"), combining two runs, every reason text. Run with `pio test -e generic_unit_tests -f test_generic_mic_analyzer`. |

Decisions and the reasons for them:

- **Asynchronous, like `getLinkCheck`.** The test takes about 12 s. Blocking the Bluetooth task for
  that long stalls the status LEDs, the LoRa loop and the app link.
- **Capture first, analyse afterwards.** The I2S read task only copies samples into a PSRAM buffer
  (about 590 kB for the default test). All maths runs later in the test task at low priority, so a
  running recording is not disturbed. The same buffer becomes the WAV.
- **The main loop stays the only owner of the I2S driver.** The test sets a flag; the main loop
  starts I2S exactly as it does for a recording. No second code path installs the driver.
- **Analysis windows skip 200 ms after the buzzer switches on and 350 ms after it switches off**
  (`GUARD_ON_MS` / `GUARD_OFF_MS` in `MicAnalyzer.hpp`). A switch is heard 64–90 ms after its mark,
  and after switching off the tone takes until about 240 ms after the mark to fade out. V1.81 used
  200 ms for both, and the fading tone counted as crackle.
- **Tone level is measured in a ±5 % band**, not at one frequency, so a slightly off I2S clock or
  buzzer pitch does not lower the result.
- **Both sides of the rise use the median block, not the mean.** With the mean, a microphone that
  hears nothing but crackles showed a rise it does not have (crackles during the tone), and a
  healthy one lost rise to crackles in the gaps. Both are covered by PC tests.
- **Crackle detection only uses buzzer-off time** and works on sample-to-sample steps, which
  suppresses low-frequency ambient sound and pink noise.

## Known limits of Phase 1

- **I2S keeps running after a test on an idle device**, as it already does after a recording is
  stopped. It costs some power until the next reboot or deep sleep. A clean stop needs a way to end
  the I2S read task, which does not exist yet.
- **The test is audible in a running recording**: about 4 s of beeps in the WAV, and the detector
  hears them too.
- **The buzzer's tones are not clean.** It is driven with a square wave and has a strong resonance,
  so each tone also carries harmonics (2700 Hz comes with 5400 Hz at -43 dBFS). This does not
  affect the rise, which compares each tone's own band with the buzzer on and off.
- **A LoRa join beep or a coverage-survey readout during the test** spoils the tone numbers. Surveys
  are refused; a join beep is not detected. Run the test again.
- **The rumble band (10–200 Hz) is not tested with a tone.** The buzzer cannot produce it. The
  octave bands show the level down there, nothing more.
- **Needs PSRAM** for the capture buffer; the test reports an error if the block is not available.
- **With the microphone set to 8 kHz** the 4000 and 5400 Hz tones are skipped (above 0.45 × sample
  rate), which leaves only 2700 Hz.

## Bench results

### ELOC_00247, 2026-11-17, V1.81 defaults, indoors

One test on a unit believed healthy. To check the device maths, the WAV was replayed through
`MicAnalyzer.hpp` on the PC, with the switch marks rebuilt at the 1024-sample read granularity. It
reproduced the device JSON to 0.1 dB.

| Measure | Value | Reading |
|---|---|---|
| 2700 Hz | on -36.2 dBFS (≈ 84 dB SPL), rise 67.6 dB | hears the buzzer very well |
| 4000 Hz | on -39.9 dBFS, rise 64.1 dB | same |
| 1000 Hz | on -79.3 dBFS, rise 24.8 dB | the buzzer hardly produces 1 kHz |
| Background in the tone bands | -104 dBFS | close to the ICS-43434's own noise in a 270 Hz band, i.e. a quiet room |
| Octave bands, buzzer off | -90 to -97 dBFS from 63 Hz up, -83 dBFS at 31–63 Hz | room rumble below, mic self-noise above 2 kHz |
| `stuckRun` / DC / clipped | 3 / -113 dBFS / 0 | normal |
| `fsMeasured` | 16 024 Hz | I2S clock fine |

What it changed:

1. **The fading tone counted as crackle.** A switch is heard 64–90 ms after its mark. After
   switching off, the 1000 and 2700 Hz tones took until 205–240 ms after the mark to fade within
   3 dB of the background, but the analysis started at 200 ms. The 3 impulses (crest 19.7 dB,
   kurtosis 3.1) all sit in the fading 2700 Hz tone, 200–225 ms after its switch-off mark. Replaying
   with every possible I2S read phase gave 3 to 24 impulses at a 200 ms guard, and 0 impulses (crest
   13.3–14.4 dB, kurtosis 0.0–0.2) at 300 ms or more. V1.82: 350 ms after switch-off, gap 600 ms.
2. **1000 Hz is useless as a test tone.** About 60 % of the sound during that tone is its 3 kHz
   harmonic, and its fundamental is 43 dB weaker than the 2700 Hz tone. In forest noise its 25 dB
   rise would vanish. V1.82 default: 2700, 4000, 5400 Hz.
3. **The spectrum slope cannot flag "only pink noise"** (-0.2 dB/oct here, see above).

### ELOC_00247, 2026-11-17, V1.82 defaults: normal, thumb on the port, no mic

Four tests in a row, run with the working-tree build (V1.82 plus the uncommitted CPU-scaling work and
its 100 Hz tick, which does not affect the test). All four replayed on the PC from the switch marks in
their JSON give the device numbers exactly.

| | normal | thumb on port #1 | mic unplugged | thumb on port #2 |
|---|---|---|---|---|
| rise 2700 / 4000 / 5400 Hz (dB) | 70.6 / 66.4 / 59.5 | 84.4 / 68.1 / 62.0 | 0 / 0 / 0 | 76.2 / 70.0 / 69.3 |
| tone level 2700 / 4000 / 5400 (dBFS) | -34.1 / -37.5 / -43.2 | -10.6 / -23.3 / -34.9 | -160 | -16.9 / -18.1 / -22.9 |
| background in tone bands (dBFS) | -103 to -105 | -91 to -97 | -160 | -88 to -93 |
| level below 200 Hz (dBFS, median of 250 ms) | -77 | -21 | -160 | -18 |
| `level.noise` / `peak` (dBFS) | -76.8 / -31.4 | -20.1 / -4.1 | -160 / -160 | -16.3 / -2.2 |
| `stuckRun` | 2 | 2 | 144 384 (all samples 0) | 2 |
| crackle impulses / crest / kurtosis | 0 / 12.7 dB / 0.0 | 19 / 24.9 dB / 13.7 | 0 / 0 / 0 | 29 / 35.7 dB / 62 |
| slope (dB/oct) | -0.6 | -7.0 | 0 | -7.1 |

What it shows:

1. **The V1.82 fix works on the device:** the healthy mic now shows 0 impulses, crest 12.7 dB and
   kurtosis 0.0.
2. **5400 Hz works as a test tone:** -43 dBFS at the mic, rise 59.5 dB.
3. **Healthy reference is stable.** Repetitions within one test agree to 0.6 dB. Between this test
   and the first one (V1.81) the tone levels differ by about 2 dB.
4. **An unplugged mic is unmistakable:** every sample is 0, so `stuckRun` equals the capture length
   and every level is -160. (Cosmetic: `peak[Hz]` then reports the band's lowest bin; it should be
   left out when there is no signal.)
5. **Covering the port makes the buzzer LOUDER, by 8 to 24 dB, not quieter.** It is consistent
   between repetitions (within 1.5 dB), so it is not random handling noise. The likely reason is that
   the sealed port forms a small closed cavity which the buzzer's vibration, carried through the case,
   pressurises. A low rise therefore cannot detect a blocked port; a tone level far ABOVE the healthy
   reference may. A thumb also adds movement, so this needs confirming with something static over the
   port (tape, wax).
6. **Touching the device during the test wrecks the crackle numbers.** The thumb's movement gives
   19–29 "impulses", kurtosis up to 62, and sound below 200 Hz about 55 dB above normal, with peaks
   near full scale. A verdict needs a "disturbed, repeat without touching" outcome when the
   low-frequency level is that high, instead of calling it a crackling mic.

### ELOC_00247, 2026-11-17 17:59–18:06: several mics swapped in, judged blind

Nine tests, mics swapped on the same unit. The user did not say which mics were broken. The last
test was a working mic with 6 layers of tissue pressed on the port (about 300 g).

| Test | rise 2700/4000/5400 (dB) | tone level 2700/4000/5400 (dBFS) | background in tone bands (dBFS) | impulses / kurtosis | Verdict from the data |
|---|---|---|---|---|---|
| #1 17:59 | 75 / 62 / 66 | -30 / -42 / -36 | -103 to -105 | 0 / 0.0 | healthy |
| #2 18:00 | 72 / 61 / 64 | -34 / -44 / -39 | -103 to -106 | 0 / 0.0 | healthy |
| #3 18:01 | 73 / 66 / 61 | -31 / -37 / -41 | -102 to -104 | 13 / 198 | hears normally, crackle bursts. Actually **healthy**: the user confirmed the clicks were room noise |
| #4 18:01 | 70 / 62 / 61 | -35 / -42 / -42 | -103 to -105 | 2 / 2.0 | healthy (2 tiny clicks) |
| #5 18:02 | 72 / 64 / 62 | -34 / -40 / -40 | -102 to -106 | 0 / 0.1 | healthy |
| #6 18:03 | 15 / 13 / 11 | -63 / -64 / -66 | -77 | 1 / 1.5 | **broken**: about 28 dB deaf, hiss about 28 dB above normal |
| #7 18:05 | 7 / 7 / 7 | -62 / -64 / -65 | -69 to -72 | 70 / 17.7 | **broken**: deaf, loud noise peaking at 125–500 Hz, crackling |
| #8 18:05 | 71 / 63 / 63 | -34 / -41 / -39 | -102 to -104 | 6 / 6.5 | healthy (a few small clicks) |
| #9 18:06 | 68 / 51 / 37 | -34 / -50 / -63 | -101 | 12 / 152 | working mic, **port damped**: the higher the pitch, the more is lost |

What it shows:

1. **Deaf and "only noise" mics are far apart from healthy ones.** Healthy rises are 59–75 dB; the
   two broken mics give 7–15 dB. The tone levels are about 28 dB down, and the background in the tone
   bands is 28–35 dB up (-69 to -77 dBFS vs -101 to -106). Any threshold between 25 and 45 dB of rise
   separates them.
2. **A soft blockage (tissue) shows as a high-frequency loss,** unlike the thumb, which sealed the
   port and made it louder. 2700 Hz is unchanged; 4000 Hz is 9 dB and 5400 Hz 23 dB below the healthy
   mics. The level difference 2700 − 5400 Hz is 5–10 dB on every healthy mic and 30 dB with the
   tissue.
3. **Crackle is the thin part, and #3 was a false alarm.** #3's events come in bursts of 4–6 clicks
   within 50 ms, each about 10–20 ms long and broadband (mostly above 2 kHz, not at the buzzer's
   pitches), up to 21 dB over the background. The user confirmed afterwards that they were random
   noise in the room. #8 had one smaller burst of the same shape and #4 two single clicks. Room
   sounds look exactly like a crackling mic in a single test.

The user confirmed the blind verdicts: only #6 and #7 are broken. The deaf/noisy calls were right,
and the crackle call was wrong.
4. **Healthy mics differ by about ±3 dB** at each tone (2700 Hz: -30 to -35 dBFS), which includes
   reseating the mic each time.

### In jungle noise (simulation with a real Sumatran recording)

The question was whether the test still works in the forest. The input was one hour of a real field
recording: ELOC8, grid 17, 2022-08-10 from 08:48, 16-bit, the first 30 s left out.
- **Calibration:** the 2022 firmware stored `raw32 >> 11` (the "far-field" default), so the file sits
  30.1 dB above the test's raw 24-bit scale. That checks out: at the quietest moments, the file's
  2700 Hz band sits exactly at the ICS-43434's own noise floor on that scale (about -104 dBFS).
- **The forest:** minutes 0.5–7 are busy and loud (broadband bursts and bird calls, perhaps people
  still at the device). From minute 7 there is a steady insect chorus above about 4.3 kHz, right on
  the 5400 Hz tone, while 2700 and 4000 Hz stay quiet.
- **Method:** the SD card with the bench WAVs had been removed. Each tested mic was therefore rebuilt
  synthetically from its measured tone levels, octave noise spectrum and the real V1.82 switch marks.
  The rebuilt mics reproduce the real results to 1–3 dB. The forest was mixed in at full level for
  the healthy mics and 28 dB down for the two broken ones, which hear that much less. The firmware's
  own `MicAnalyzer.hpp` then ran on 236 stretches of the hour.

Share of jungle stretches where each rule fires (6 healthy mics / tissue / the 2 broken mics):

| Rule | Loud first minutes: healthy | Rest of the hour: healthy | Tissue: loud minutes / rest | Broken #6, #7 |
|---|---|---|---|---|
| deaf: **median** rise < 30 dB | **8–38 %** | 0 % | 92 % / 0 % | 100 % |
| deaf: **best tone's** rise < 30 dB | 0 % | 0 % | 0 % / 0 % | 100 % |
| deaf: median tone **level** < -55 dBFS | 0 % | 0 % | 0 % / 0 % | 100 % |
| noisy: tone-band background > -90 dBFS | **100 %** | 4–6 % | 100 % / 6 % | 100 % |
| crackle: impulses ≥ 10 and kurtosis ≥ 20 | **85 %** | 0 % | 85 % / 0 % | 0 % |
| disturbed: 31–125 Hz band > -45 dBFS | 0 % | 0 % | 0 % / 0 % | 0 % |
| port damped: 2700 − 5400 Hz level > 18 dB | 0 % | 0 % | 100 % / 100 % | 0 % |

Lowest rise of a healthy mic: in the loud minutes 36.5 dB at 2700 Hz, 20 dB at 4000 and 5400 Hz; in
the rest of the hour 47 / 42 / 24 dB. The broken mics never exceed 15 dB at any tone.

What it means:

1. **The test works in the jungle if the verdict uses the best tone, or the absolute tone level, not
   the median.** Forest noise only ever covers some of the tones (insects at 5400 Hz, busy minutes at
   4000 Hz too). A healthy mic always clears 30 dB on at least one tone, while a deaf one clears it on
   none. The absolute tone level ignores the forest altogether (healthy -36 to -42, broken -62 to
   -64 dBFS), but it depends on every unit's buzzer being as loud as ELOC_00247's, which is unchecked.
2. **"Noisy background" cannot be a verdict in the field.** It fires on healthy mics whenever the
   forest is loud. Keep it as information only.
3. **Crackle cannot be a verdict either:** forest clicks trigger it 85 % of the time in busy minutes,
   and room noise did on the bench (#3). Report it as "clicks heard, listen to the recording".
   Telling a crackling mic from a noisy place needs repeated tests or the Phase 4 long-term counter.
4. **Port damping and handling detection hold up:** the tissue rule fired on every stretch and never
   on a healthy mic, and the forest never triggered "disturbed".

Limits: one hour of one site in the morning. Night choruses, cicadas at dusk and rain on the case may
be louder. A unit with a quieter buzzer loses margin on every rule. The 2022 recorder's enclosure may
let in more or less forest sound than today's.

Candidate rules for Phase 2, revised after the jungle check. V1.83 implements them, in the order
and with the limits of [Verdict](#verdict-since-v183), and makes crackle a `suspect` result once a
second run confirms it:

| Result | Rule (tentative) | Healthy | Broken / blocked |
|---|---|---|---|
| no signal | `stuckRun` > half the capture | 2–3 | 144 384 (unplugged) |
| beeps buried in noise (bad SNR) | **best tone's** rise < 30 dB | 59–75 bench, ≥ 36.5 jungle | 7–15 |
| port damped | tone level 2700 − 5400 Hz > 18 dB | 5–10 | 30 (tissue) |
| port sealed or pressed | 2700 Hz tone level > -22 dBFS | -30 to -36 | -11, -17 (thumb) |
| disturbed, repeat | 31–125 Hz bands above -45 dBFS | -80 to -92 bench, never in jungle | -20 to -38 (thumb) |
| noisy, repeat in a quiet place | the quieter of the 2700 / 4000 Hz backgrounds above -90 dBFS, checked only after "deaf" passed | -101 to -106 bench; forest up to -82 (calm) and -64 (busy) | -67 to -77 (#6, #7) |
| weak (faint mic) | background quiet (the rule above did not fire) and best tone's rise < 45 dB | 59–75 bench, ≥ 53.8 in the quiet 87 % of the jungle hour | none tested yet |
| information only | crackle statistics | | |

The rise is the beeps' signal-to-noise ratio. The 30 dB limit catches only very bad SNR, because a
loud forest can push a healthy mic down to 36.5 dB. A quiet background proves both that the place is
quiet and that the mic does not hiss, so only then the stricter 45 dB limit applies. It is meant for
the "very faint" mics: same self-noise, beeps 15 dB or more quieter. No such mic has been tested yet,
and a unit with a quieter buzzer loses margin on this rule first.

**Decision (2026-11-17): the app tells the user "The test works best in a quiet environment"**
instead of asking where the test runs. So the hiss check runs everywhere, but it can never say
"broken": a healthy mic in a loud forest gets "noisy, repeat in a quiet place", which is the same
advice. If the mic is still noisy in a quiet place, the mic is suspect. A mic that hears the beeps but
hisses has not turned up yet: #6 and #7 were also deaf, and the deaf check catches them first.

Still open: a second healthy unit (is the buzzer level the same on every ELOC?), a test in the forest
itself, a static cover over the port, the buzzer frequency map, and more broken mics. The field
recordings in "Broken Microphones ICS-43434" could show what broken mics sound like in the forest.

## Phases

| Phase | Content | State |
|---|---|---|
| 1 | Firmware measurement, raw numbers, WAV + JSON on SD | V1.81; V1.82 corrects the off guard and the default tones after one healthy-unit test; broken mics pending |
| 2 | Thresholds and a verdict in the result, from the bench sessions | V1.83: verdict with a confirming second run; PC-verified on the bench and forest data, not yet on a device |
| 3 | App: "Mic check" screen with the verdict and the numbers | open |
| 4 | Optional: crackle counter during normal recording, mic health in `getStatus` and in the LoRa heartbeat | open |
| 5 | Optional: send a short clip to the phone to listen | open |

## Wiki

The firmware wiki page `ELOC-3.0-App-Interface` lists every BT command and does **not** have these
two yet. They are held back on purpose: the wiki describes released firmware, this is a test branch,
and the result format will change once the thresholds exist. Add both commands to the wiki when the
branch is merged; the "Commands" section above is written to be pasted.
