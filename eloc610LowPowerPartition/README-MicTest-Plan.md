# Microphone self-test (`setMicTest` / `getMicTest`)

Status: **Phase 1 on branch `EDsteve/mic-test`**: implemented in V1.81, corrected in V1.82 after
the first bench test. The maths is unit-tested on the PC, and one test has run on a healthy unit
(see [Bench results](#bench-results)). Broken mics have not been tested yet. Phase 1 deliberately
returns **raw numbers only, no pass/fail verdict**: the thresholds have to come from real broken
microphones.

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

## Commands

Both work from the app's command-line screen (Device Settings → Advanced → Command line).

### `setMicTest` — start a test

Returns at once; the test itself takes about 12 s, or 14 s when it has to start the microphone.
Optional arguments:

| Argument | Default | Range | Meaning |
|---|---|---|---|
| `freqs` | `2700,4000,5400` | 100 Hz .. 0.45 × sample rate, up to 6 | buzzer tones in Hz; `freqs=0` runs a silent test without the buzzer |
| `reps` | `2` | 1..5 | how often the tone list is repeated |
| `onMs` | `600` | 400..3000 | length of each tone |
| `gapMs` | `600` | 400..3000 | silence after each tone; the first 350 ms of it are not analysed (the tone is still fading) |
| `silenceMs` | `1500` | 500..10000 | silence before the first tone |
| `save` | `1` | 0/1 | write the WAV and the JSON to `/sdcard/mictest/` |

Example: `setMicTest#freqs=2700#reps=3`

The defaults changed in V1.82: V1.81 used `1000,2700,4000` and a 400 ms gap (see
[Bench results](#bench-results)).

Reply: `{"state":"starting","duration[ms]":13500}`. Refused (with a reason) while another test runs,
during a firmware transfer, during a coverage survey, and while an intruder alarm or candidate is
active.

### `getMicTest` — progress and result

While the test runs: `{"state":"capturing","progress[%]":40,"remaining[ms]":7200}`. States: `idle`,
`starting`, `capturing`, `analysing`, `saving`, `done`, `error`. Afterwards the last result stays
readable until the next test or a reboot. The real result of the first bench test (ELOC_00247,
V1.81 defaults, so the old 1000 Hz tone):

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
| Analysis | `lib/audio_input/src/MicAnalyzer.hpp` | Header-only, no ESP code: takes the captured samples and the on/off schedule, returns the numbers. Unit-tested on the PC. |
| Capture tap | `lib/audio_input/src/I2SMEMSSampler.{h,cpp}` | `set_raw_tap()`: hands every raw I2S buffer to one listener before the gain shift. One pointer check per buffer when unused. |
| Test run | `lib/Commands/src/MicTest.{hpp,cpp}` | Singleton with its own short-lived task: gets the mic running, drives the buzzer, captures to PSRAM, analyses, saves, builds the JSON. |
| Buzzer hand-over | `lib/ElocHardware/src/ElocSystem.{hpp,cpp}` | `acquireBuzzerForTest()` / `testTone()` / `releaseBuzzerForTest()`: keeps status beeps out of the test and keeps the knock guard engaged. |
| Commands | `lib/Commands/src/ElocCommands.cpp` | `setMicTest`, `getMicTest`; `setFwUpdateBegin` is refused while a test runs. |
| Main loop | `src/main.cpp` | Starts I2S when the test asks for it; no duty-cycle sleep while a test runs. |
| PC tests | `test/test_generic_mic_analyzer/` | 18 synthetic cases: healthy, deaf (pink noise), faint, crackling, deaf and crackling, a fading buzzer tail, dead, stuck, DC, clipping, off-pitch, dropouts, 8 kHz, segment building, truncated capture. Run with `pio test -e generic_unit_tests -f test_generic_mic_analyzer`. |

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

Still open from this test: nothing yet on a broken mic, taped port, or outdoors.

## Phases

| Phase | Content | State |
|---|---|---|
| 1 | Firmware measurement, raw numbers, WAV + JSON on SD | V1.81; V1.82 corrects the off guard and the default tones after one healthy-unit test; broken mics pending |
| 2 | Thresholds and a pass / warn / fail verdict in the result, from the bench session | open |
| 3 | App: "Mic check" screen with the verdict and the numbers | open |
| 4 | Optional: crackle counter during normal recording, mic health in `getStatus` and in the LoRa heartbeat | open |
| 5 | Optional: send a short clip to the phone to listen | open |

## Wiki

The firmware wiki page `ELOC-3.0-App-Interface` lists every BT command and does **not** have these
two yet. They are held back on purpose: the wiki describes released firmware, this is a test branch,
and the result format will change once the thresholds exist. Add both commands to the wiki when the
branch is merged; the "Commands" section above is written to be pasted.
