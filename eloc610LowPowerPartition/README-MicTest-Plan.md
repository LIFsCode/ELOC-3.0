# Microphone self-test (`setMicTest` / `getMicTest`)

Status: **Phase 1 implemented in V1.81 on branch `EDsteve/mic-test`** — compiles, the maths is
unit-tested on the PC, nothing has run on hardware yet. Phase 1 deliberately returns **raw numbers
only, no pass/fail verdict**: the thresholds have to come from real broken microphones.

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
2. **Play tones and listen** (default 1000, 2700 and 4000 Hz, 0.6 s on / 0.4 s off, twice). For each
   tone the firmware compares the level at that pitch with the buzzer on against the same pitch with
   the buzzer off. The difference is the **rise**.
3. **Analyse**, then save the capture as a WAV plus the result as JSON on the SD card.

How each symptom shows up:

| Symptom | Number to look at | Expected on a broken mic |
|---|---|---|
| Only noise | `tones[].rise[dB]` | near 0 dB: the buzzer makes no difference |
| Only noise | `spectrum.slope[dB/oct]` | near 0 (pink); white noise would be about +3 |
| Faint | `tones[].rise[dB]` | clearly lower than on a healthy unit |
| Crackling | `crackle.impulses`, `crackle.crest[dB]`, `crackle.kurtosis` | more than 0, high, high |
| Nothing at all | `level.stuckRun`, `level.noise[dBFS]` | as long as the capture, -160 |
| Intermittent | `crackle.dropouts` | more than 0 |

All levels are measured on the **raw 24-bit samples before the gain shift**, so `MicVolume2_pwr`
does not change them.

## Commands

Both work from the app's command-line screen (Device Settings → Advanced → Command line).

### `setMicTest` — start a test

Returns at once; the test itself takes about 10 s. Optional arguments:

| Argument | Default | Range | Meaning |
|---|---|---|---|
| `freqs` | `1000,2700,4000` | 100 Hz .. 0.45 × sample rate, up to 6 | buzzer tones in Hz; `freqs=0` runs a silent test without the buzzer |
| `reps` | `2` | 1..5 | how often the tone list is repeated |
| `onMs` | `600` | 400..3000 | length of each tone |
| `gapMs` | `400` | 300..3000 | silence after each tone |
| `silenceMs` | `1500` | 500..10000 | silence before the first tone |
| `save` | `1` | 0/1 | write the WAV and the JSON to `/sdcard/mictest/` |

Example: `setMicTest#freqs=2700#reps=3`

Reply: `{"state":"starting","duration[ms]":7800}`. Refused (with a reason) while another test runs,
during a firmware transfer, during a coverage survey, and while an intruder alarm or candidate is
active.

### `getMicTest` — progress and result

While the test runs: `{"state":"capturing","progress[%]":40}`. States: `idle`, `starting`,
`capturing`, `analysing`, `saving`, `done`, `error`. Afterwards the last result stays readable until
the next test or a reboot:

```json
{
  "state": "done",
  "test": 3,
  "time": "2026-11-13 14:02:11",
  "fs[Hz]": 16000,
  "fsMeasured[Hz]": 15998,
  "duration[ms]": 7800,
  "i2sWasRunning": false,
  "micVolume2_pwr": -4,
  "level": {
    "noise[dBFS]": -71.2, "peak[dBFS]": -18.4, "dc[dBFS]": -64.0,
    "clipped": 0, "stuckRun": 2
  },
  "spectrum": {
    "edges[Hz]": [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000],
    "bands[dBFS]": [-80.1, -82.4, -84.0, -85.2, -86.9, -88.0, -89.3, -90.1],
    "slope[dB/oct]": -1.4
  },
  "crackle": {
    "impulses": 0, "crest[dB]": 12.9, "kurtosis": 0.1, "dropouts": 0, "analysed[ms]": 2500
  },
  "tones": [
    {"f[Hz]": 1000, "on[dBFS]": -52.1, "off[dBFS]": -88.0, "rise[dB]": 35.9, "riseMin[dB]": 35.1,
     "peak[Hz]": 1000, "onTotal[dBFS]": -40.2}
  ],
  "wav": "/sdcard/mictest/ELOC_00246_20261113_140211.wav"
}
```

The numbers above are invented to show the format. Nobody has measured a real unit yet.

### What the fields mean

| Field | Meaning |
|---|---|
| `fsMeasured[Hz]` | sample rate measured against the RTC clock during the capture; far from `fs[Hz]` means the I2S clock is wrong and every other number is suspect |
| `i2sWasRunning` | false = the test started the microphone itself (device was idle) |
| `level.noise[dBFS]` | level with the buzzer off, DC removed. 0 dBFS = a full-scale sine, the same convention as the mic datasheet, so for the ICS-43434 dB SPL ≈ dBFS + 120 |
| `level.peak[dBFS]` | largest sample of the whole capture, tones included |
| `level.dc[dBFS]` | DC offset with the buzzer off |
| `level.clipped` | samples at the 24-bit rails |
| `level.stuckRun` | longest run of identical consecutive samples; a healthy mic gives single digits |
| `spectrum.bands[dBFS]` | level per octave with the buzzer off; `edges[Hz]` are the band limits |
| `spectrum.slope[dB/oct]` | straight-line fit through the bands from 63 Hz up. Pink noise ≈ 0, white noise ≈ +3 |
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
   10 s, send `getMicTest`. Repeat three times to see how much the numbers move.
2. Same unit with **tape over the mic port**. This shows how much of the buzzer reaches the mic
   through the board and the case instead of through the air.
3. Each **broken mic** available: noise-only, faint, crackling.
4. One run in a noisy place and one outdoors.
5. Copy `/sdcard/mictest/` off the card. Each test leaves a `.wav` (24-bit, the exact samples the
   numbers were computed from) and a `.json` with the result.

What the session has to answer:

- Does `rise[dB]` separate healthy from noise-only and from faint, and at which tone best?
- **Does a dead mic still show a rise?** The buzzer's drive current could reach a damaged mic's
  output through the supply instead of through the air. If it does, the rise alone is not a safe
  verdict and the spectrum slope has to carry the "only noise" case.
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
| PC tests | `test/test_generic_mic_analyzer/` | 16 synthetic cases: healthy, deaf (pink noise), faint, crackling, deaf and crackling, dead, stuck, DC, clipping, off-pitch, dropouts, 8 kHz, truncated capture. Run with `pio test -e generic_unit_tests -f test_generic_mic_analyzer`. |

Decisions and the reasons for them:

- **Asynchronous, like `getLinkCheck`.** The test takes about 10 s. Blocking the Bluetooth task for
  that long stalls the status LEDs, the LoRa loop and the app link.
- **Capture first, analyse afterwards.** The I2S read task only copies samples into a PSRAM buffer
  (about 500 kB for the default test). All maths runs later in the test task at low priority, so a
  running recording is not disturbed. The same buffer becomes the WAV.
- **The main loop stays the only owner of the I2S driver.** The test sets a flag; the main loop
  starts I2S exactly as it does for a recording. No second code path installs the driver.
- **Analysis windows skip 200 ms after every buzzer switch.** Samples reach the test up to about
  130 ms after the microphone picked them up, so the switch moment is not exact.
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
- **A LoRa join beep or a coverage-survey readout during the test** spoils the tone numbers. Surveys
  are refused; a join beep is not detected. Run the test again.
- **The rumble band (10–200 Hz) is not tested with a tone.** The buzzer cannot produce it. The
  octave bands show the level down there, nothing more.
- **Needs PSRAM** for the capture buffer; the test reports an error if the block is not available.
- **With the microphone set to 8 kHz** the 4000 Hz tone is skipped (above 0.45 × sample rate).

## Phases

| Phase | Content | State |
|---|---|---|
| 1 | Firmware measurement, raw numbers, WAV + JSON on SD | implemented V1.81, hardware test pending |
| 2 | Thresholds and a pass / warn / fail verdict in the result, from the bench session | open |
| 3 | App: "Mic check" screen with the verdict and the numbers | open |
| 4 | Optional: crackle counter during normal recording, mic health in `getStatus` and in the LoRa heartbeat | open |
| 5 | Optional: send a short clip to the phone to listen | open |

## Wiki

The firmware wiki page `ELOC-3.0-App-Interface` lists every BT command and does **not** have these
two yet. They are held back on purpose: the wiki describes released firmware, this is a test branch,
and the result format will change once the thresholds exist. Add both commands to the wiki when the
branch is merged; the "Commands" section above is written to be pasted.
