# Feature Plan: TensorFlow Lite Micro runtime (Edge Impulse replacement, Phase 1)

## Context

ELOC detects chainsaws, gunshots and elephant sounds on the device. Today that runs through an
**Edge Impulse** C++ library (`lib/edge-impulse/`) that has to be exported by hand from Edge
Impulse Studio (`README-ai.md`). Bring-Your-Own-Model export there now needs a paid licence.

The web app's **ELOC Model Training** tool already trains models. Since 2026-09-21 every training
job also produces a ready-to-run **ELOC device model package**:
- a fully INT8 TensorFlow Lite model of the classifier only;
- its complete configuration embedded as TFLite metadata;
- a C header of the model bytes;
- golden test vectors.

**Phase 1 (this plan)** builds the firmware side: a new build environment that runs that package
directly on **TensorFlow Lite Micro (TFLM)**, with no Edge Impulse code linked. Behaviour stays the
same for the field user: same Bluetooth commands, detection semantics, SD results file, LoRa event
message and duty cycle.

| Phase | Scope | Status |
|---|---|---|
| 0 | Web app: device package, INT8 export, calibration, golden vectors | **Done**, deployed 2026-09-21 and verified on real jobs (see "Phase 0 status") |
| **1** | **Firmware TFLM runtime, model compiled in** | **Implemented 2026-09-24 (V1.79)**, PC-verified; hardware tests pending (see "Implementation status") |
| 2 | Load the model from the SD card / push it from the app (no reflash) | Later; Phase 1 must not block it |
| 3 | Field A/B against the Edge Impulse build, then retire `esp32dev-ei` | Later |

### Decisions already made by the user

- **Delivery: both.** A compiled-in model (C header) is the default; a model file on the SD card or
  pushed from the app overrides it. Phase 1 builds compiled-in only, but the loader must take any
  memory buffer so Phase 2 only adds a file source.
- **Keep `esp32dev-ei` working as the fallback** until the new runtime has proved itself in the
  field. It must still build and behave exactly as today after Phase 1.
- **Why leave Edge Impulse:** the manual export steps, and a licence is now needed for BYOM export.
- **Licensing consequence:** the Edge Impulse SDK's `dsp/` folder and the generated model files carry
  a *paid-subscription* notice. The new runtime must **not** reuse any Edge Impulse code, including
  its MFE/spectrogram DSP. TFLM, ESP-NN (Apache-2.0) and KissFFT (BSD-3) are fine.
- **Multi-class:** one model can score several sounds (softmax). The package format supports it and
  the runtime must too. Rumbles probably need their own model (a different front-end), so Phase 1
  runs **one model at a time**.

## Implementation status (2026-09-24)

| Task | State |
|---|---|
| T0 toolchain | v1.3.4 + ESP-NN **v1.1.2** compile **unpatched** with GCC 8.4. The zeros invoke is the first case of `test_target_tflm`; **not yet run on hardware** |
| T1 `ModelPackage` | Done. Also verifies the flatbuffer, and rejects packages whose `input_scale`/`window`/`layout` differ from spec v1 |
| T2 `MelFrontend` | Done, native test green: quantized inputs **bit-identical** on all 9 golden records |
| T3 `TflmClassifier` | Done; `test_target_tflm` builds. **Needs a run on hardware** for DSP/NN ms and `arena_used_bytes()` |
| T4 `ElocDetector` + wiring | Done; both `esp32dev-ei` and `esp32dev-tflm` build |
| T5 silence guard | Done (`AI_SILENCE_PEAK` 16) |
| T6 status / CSV / identity | Done |
| T7 hygiene | TFLM map has no Edge Impulse objects and one TFLM copy; sizes EI 1,797,776 B, TFLM 1,977,376 B (slot 0x7E0000). IRAM: TFLM uses 90 B less than EI |
| T8 docs | `README-ai.md`, `CLAUDE.md`, memory-bank, `VERSIONS.md`, `lib/tflite-micro/ELOC_VENDOR.md`. Wiki edits prepared, pushed only with the user's OK |

Where the implementation differs from this plan, and why:
1. **`ElocDetector` lives in `lib/eloc_detector/`, not `lib/eloc_ml/`.** In `eloc_ml` it pulled
   the whole firmware (config, sampler, LoRa...) into the on-target test build. `eloc_ml` is now
   self-contained; `eloc_detector` is the glue. Both are in the EI env's `lib_ignore`.
2. **Quantization rounds half to even** (`rintf`), not `roundf`. That is what NumPy does, and the
   inputs match exactly instead of within ±1.
3. **The full-scale sine misses the 1e-3 feature tolerance (1.6e-3).**
   - Cause: the 1 kHz tone sits exactly on FFT bin 32, so with the Hann window the far bins are
     empty up to int16 rounding noise, ~100 dB below the peak, right at the log floor. There float32
     FFT round-off dominates, while the golden reference is float64-accurate (2.8e-7 from an exact
     DFT).
   - Measured: a full 512-point complex FFT gets 6.4e-4 at about twice the FFT cost, and real audio
     is within 3.7e-6 either way. The quantized input is exact in every case.
   - Kept the fast real FFT; that one record's feature tolerance is 2e-3. **Open for the user.**
4. **The front-end's internal-RAM buffers (~12.6 KB) exist only while the AI task runs.** They are
   allocated at task start, internal RAM first, PSRAM if short, and freed when it stops. The model,
   arena, features and audio buffers are PSRAM from boot as planned. The reason is the Bluetooth
   internal-heap sensitivity noted in `memory-bank/activeContext.md`.
5. **base64 uses a small built-in decoder**, not `mbedtls_base64_decode`, so the native test runs
   the same code as the device.
6. **Aligned allocation is done by hand** (`MlAlloc.cpp`). `heap_caps_aligned_alloc()` pulls
   ~1.5 KB of TLSF code into IRAM, and the EI build has only ~330 B of IRAM left, so the TFLM build
   overflowed it.
7. **`generic_unit_tests` now has `lib_ldf_mode = off`** and an explicit `lib_deps`. The dependency
   finder pulled ESP-only libraries into the host build, so every native test failed before this.
8. **The timer-wake CSV name is built after the model loads**, because the TFLM file name contains
   the job id. One function, `buildInferenceResultFilename()`, builds it for both runtimes.
9. **Label 0 may be `background`, `other` or `others`**, the names the EI path already treats as
   non-target.
10. **The TFLM task waits for audio with a 1 s timeout.** It notices a stop without audio, and a
    restart waits for the old task to exit instead of running two.

## Phase 0 status (verified 2026-09-24)

Phase 0 is finished. Evidence from production (Firestore `model_training_jobs`):

| Job | Model | FP32 → INT8 val PR AUC | Feature parity | Ops | Size / arena est. / compute |
|---|---|---|---|---|---|
| `SkRQW4hUFaXslHibnmv9` | chainsaw | 0.9988 → 0.9987 | 5.5e-5 | CONV_2D, FULLY_CONNECTED, LOGISTIC, MAX_POOL_2D, RESHAPE | 14 KB / 56 KB / 1.43 M MAC |
| `VnXuKnHqxVMHUnjDbOMR` | chainsaw | 0.8658 → 0.8588 | 4.1e-5 | same | same |
| `43cDE2aL0lxfL6tqJHVu` | gibbon | 0.9934 → 0.9932 | 3.6e-5 | same | same |

**Independent check of the contract.** The front-end was reimplemented from
`ELOC_management/DEVICE_MODEL_PACKAGE.md` alone: frame loop, Hann formula, rFFT, sparse mel from
base64, log, normalization. Run on job `SkRQ…`'s golden vectors it gave:
- features within 2.4e-7;
- bit-identical INT8 inputs and outputs (TFLite reference kernels).

The same check confirmed that the embedded config equals `eloc_model.json`, all buffers are 16-byte
aligned, and the header bytes equal the `.tflite`. **The spec document is sufficient to implement
the C front-end.**

### Findings that shape Phase 1

1. **Digital silence scores 0.953 "chainsaw"** in the chainsaw golden vectors, and a full-scale
   1 kHz sine scores 0.996. The training data has no near-silent audio, so all-zero input is out of
   distribution.
   - **The fix is training data** (Decision 3): silence and very-low-level background windows,
     added automatically by the web-app worker.
   - The runtime keeps a small **silence guard** (T5) as a dead-mic safety net.
2. **The calibration is based on very little negative audio** (0.15–0.58 h per job), so the
   recommended settings are marked "not verifiable". Phase 1 **shows** the model's recommended
   settings but does **not** apply them (see Decisions).
3. **Arena estimate:** the host-side estimate is 56 KB for the current architecture. The real TFLM
   figure is unknown until measured on the device (T3).

### Other Phase 0 follow-ups (web app, not blocking Phase 1)

- The Create-model page only pre-checks firmware-resampling rate compatibility for datasets that
  record `sampleRates`. Two jobs failed late on 44.1/24 kHz sources.
- The worker and UI train one target against background only; the multi-class UI is not built.
- The Audio Studio local pipeline exports INT8 in its own format, not this package.
- The training-worker repo `ie/` (branch `feature/cloud-training-worker`) is **local only** by the
  user's choice. **Do not push it.**

## Repos and key paths

| What | Path |
|---|---|
| Firmware project (this repo) | `c:\Development\ELOC\Firmware\ELOC-3.0\eloc610LowPowerPartition\` — branch **`EDsteve/Patrol`**, remote `LIFsCode/ELOC-3.0` |
| **Package format contract** | `c:\Development\ELOC\ELOC_management\DEVICE_MODEL_PACKAGE.md` — read this first |
| Executable front-end spec (Python) | `c:\Development\ELOC\ie\cloud_training\eloc_features.py` (`FeatureSpec.compute`, `FeatureSpec.from_config`) |
| Package builder / golden reader | `c:\Development\ELOC\ie\cloud_training\device_package.py` (`SUPPORTED_OPS`, `read_golden_vectors`, `read_embedded_config`) |
| Detection-rule simulation (Python) | `c:\Development\ELOC\ie\cloud_training\calibration.py` (`simulate_events` mirrors the firmware) |
| Current EI wrapper | `lib/edge-impulse/src/EdgeImpulse.{hpp,cpp}` — the API the rest of the firmware calls |
| Inference buffer struct | `include/ei_inference.h` (`inference_t`: two `int16` buffers, `buf_select/buf_ready/buf_count/n_samples/status_running`) |
| Per-window logic to preserve | `src/main.cpp` `ei_callback_func()` (~l.642): threshold → start recording → `addDetectionToWindow` → `checkDetectionCriteria` → `increment_detectedEvents` → `updateEventInfo`; SD CSV via `save_inference_result_SD()` |
| Results CSV naming | `src/main.cpp` `create_inference_result_file_SD()` (~l.540) **and** the timer-wake rebuild (~l.1516). Keep both in sync (refactor into one function) |
| AI start/stop | `src/main.cpp` main loop (~l.1941): `rec_ai_evt_queue`, deferred start `g_ai_start_pending`; BT side `lib/Commands/src/ElocCommands.cpp` ~l.600–622 |
| Sampler → inference buffers | `lib/audio_input/src/I2SMEMSSampler.cpp` ~l.349–382 (decimation by `ei_skip_rate`, notify `ei_TaskHandler`); `register_ei_inference()` ~l.140 |
| Status JSON | `ElocCommands.cpp` ~l.138–160: `session.detection.{state,detectingTime[h],detectedEvents,aiModel}`, `device.buildVariant` |
| LoRa event message | `lib/ElocHardware/src/ElocLora.cpp` ~l.521 (trigger on `get_detectedEvents()` change), `sendEventMessage()` ~l.603 (labels truncated to `LORA_LABEL_LEN = 5`, value 0–100) |
| TTN formatter | `payload-formatters/radiolib-uplink-formatters.js` (generic 5-char labels; no change needed) |
| Detection config | `lib/ElocHardware/src/ElocConfig.hpp:96` `inferenceConfig_t {threshold 0–100, observationWindowS, requiredDetections}` |
| Duty cycle + AI | `src/main.cpp` `prepareCyclicDeepSleep()` (~l.880), `rtc_duty_cycle.aiEnabled` (~l.1484, ~l.1834) |
| CPU boost during AI | `lib/ElocHardware/src/ElocSystem.cpp:1034` (`AI_INCREASE_CPU_FREQ`) |
| AI constants | `include/project_config.h` ~l.285–360 (`AI_CONTINUOUS_INFERENCE` **off**, `EI_BUFFER_IN_PSRAM`, `TASK_PRIO_AI 7`, `TASK_AI_CORE 1`, `ENABLE_HEAP_MONITOR`) |
| EI heap overrides | `src/ei_porting_overrides.cpp` (EI only; PSRAM-first ≥ 8 KB). This is the reason big AI buffers go to PSRAM |
| Web app consumer of the CSV | `ELOC_management/src/pages/ModelComparisonPage.tsx:399` (`/EI-results.*\.csv$/i`), `src/utils/modelComparison.ts` (model name from `VER-(\d+)`, else the filename) |
| App variant guard | `App/ELOC-Control-Panel/.../FirmwareUpdateActivity.kt` ~l.333 (refuses flashing when the filename variant ≠ `device.buildVariant`) |

### Facts verified while planning (do not re-derive)

- **Toolchain:** ESP-IDF **4.4.7** with arduino-esp32 **2.0.7** as a component, `xtensa-esp32-elf-g++`
  **GCC 8.4.0** (esp-2021r2-patch5). The project name must stay `idf-wav-sdcard`.
- **esp-tflite-micro:** the current release (v1.4.1) needs IDF ≥ 5.1, and v1.3.5–v1.3.8 need
  IDF ≥ 5.0. **v1.3.4 is the last tag declaring IDF ≥ 4.4**; it needs `esp-nn >= 1.1.1`. Both are
  Apache-2.0.
- **FFT: KissFFT first, ESP-DSP optional.**
  - ESP-DSP is *not bundled* with IDF 4.4, but it *can* be vendored like esp-nn: every release up to
    current master declares IDF ≥ 4.2, and it is Apache-2.0. arduino-esp32 picks it up through
    `maybe_add_component(esp-dsp)`.
  - Start with **KissFFT** (BSD-3-Clause, `kiss_fft.c` + `kiss_fftr.c`, vendored from upstream
    `mborgerding/kissfft`, **not** from the EI SDK). Today's EI build already runs KissFFT, and its
    whole spectrogram step takes 52–54 ms per 1 s window (`README-ai.md`). The new front-end does
    similar work (61 × 512-point vs 32 × 1024-point FFTs), and the identical code runs in the native
    test.
  - Put the FFT behind a one-function `RealFft` wrapper. Switch to ESP-DSP's assembly FFT only if
    T3's measured DSP time justifies it: expect it to save a few tens of ms per window, a small
    energy gain rather than new capability.
- **Staying on ESP-IDF 4.4 (decided 2026-09-24).** IDF 4.4 has been out of support since about
  mid-2024, but moving to IDF 5 means Arduino core 3.x, which breaks APIs across I2S, Bluetooth
  Serial, LEDC, timers and ADC. PlatformIO's official platform doesn't support that combination
  (the community `pioarduino` platform does), and every hardware-tuned area would need re-validation.
  The user decided it will be done **together with the ESP32 chip upgrade** (new hardware
  revision), not before. Raise it earlier only if T0 fails with every fallback.
- **The current firmware is non-continuous:** `AI_CONTINUOUS_INFERENCE` is commented out. It scores
  back-to-back 1 s windows, uses no score averaging, and makes one decision per window. The
  package's `inference.hop_samples == window_samples` and `average_count == 1` describe exactly this.
- **Known AI fragility, which T4 should design out:**
  - `ei_thread` runs on a 4 KB stack (1448 B free under load).
  - The EI DSP mallocs per inference and failed under BT+LoRa+GPS load (`MFE -1002`).
  - A 1-in-20 first-inference panic is still open (memory note `todo-eloc-panic-investigation`).

### Repo conventions that apply (from `CLAUDE.md`)

- **Flags:**
  - Build-variant switches go in `platformio.ini`, as `EDGE_IMPULSE_ENABLED` already does. That is
    where `ELOC_AI_ENABLED` and `ELOC_TFLM_ENABLED` belong.
  - Tunables go in `include/project_config.h`, e.g. `AI_SILENCE_PEAK` and `AI_MAX_LABELS`. That
    header must not include other headers.
- **Libraries:** each subsystem is a PlatformIO library under `lib/`. Never reference `/lib` libraries
  from `lib_deps`; per-env isolation uses `lib_ignore`.
- **Timer-wake fast-boot path:** duty-cycle wakes skip heavy init, but AI auto-resumes on timer wake
  when `rtc_duty_cycle.aiEnabled`. So the model load also runs on timer wake:
  - keep it light (parse about 6 KB of JSON, one arena allocation);
  - log its duration.
- **Runtime sample rate** comes from the SD/SPIFFS config, not the compiled default. Check the
  actual I2S rate against the model's rate at AI start, not at compile time.
- Logging: `ESP_LOGx(TAG, …)` with `ESP_LOGV(TAG, "Func: %s", __func__)` at entry points.

## Target design

```
I2SMEMSSampler ──(decimate by i2s_rate/model_rate)──► inference_t double buffer (PSRAM, int16)
                                                              │ xTaskNotify
                                                              ▼
                                ElocDetector AI task (core 1, 8 KB stack, prio TASK_PRIO_AI)
                                  1. silence guard
                                  2. MelFrontend  (C++, spec v1, KissFFT, preallocated)
                                  3. quantize → TFLM input tensor (int8)
                                  4. TflmClassifier.Invoke()  (ESP-NN kernels, arena in PSRAM)
                                  5. dequantize → per-label probabilities
                                  6. shared handleClassification(): threshold / observation window /
                                     start recording / events / SD CSV / LoRa trigger  (unchanged)
```

- **Compile-time switches** (in `platformio.ini`):
  - `ELOC_AI_ENABLED`: any AI runtime. Set by **both** AI envs. Use it for all AI code that isn't
    Edge Impulse-specific: sampler hookup, status fields, LoRa event, duty-cycle AI state, the main
    loop's AI queue, CPU boost.
  - `EDGE_IMPULSE_ENABLED`: EI build only (unchanged meaning).
  - `ELOC_TFLM_ENABLED`: TFLM build only.
- **One runtime alias** (new header `include/ai_runtime.h`) replaces direct references to
  `EdgeImpulse edgeImpulse`:
  ```cpp
  #if defined(EDGE_IMPULSE_ENABLED)
    #include "EdgeImpulse.hpp"
    using AiRuntime = EdgeImpulse;
  #elif defined(ELOC_TFLM_ENABLED)
    #include "ElocDetector.hpp"
    using AiRuntime = ElocDetector;
  #endif
  extern AiRuntime aiRuntime;   // was: extern EdgeImpulse edgeImpulse;
  ```
  `ElocDetector` implements the same public method names and types the shared code uses:
  - `Status`, `DetectedEventInfo`, `get_status/set_status`, `getInference()`, `buffers_setup`,
    `free_buffers`, `microphone_inference_record`
  - thread start (`start_ei_thread(callback)`; keep the name or rename in both)
  - `get_detectedEvents`, `increment_detectedEvents`, `get_totalDetectingTime_secs`,
    `get_lastEventInfo`, `updateEventInfo`
  - `addDetectionToWindow`, `checkDetectionCriteria`, `clearDetectionWindow`, `get_aiModel`

  `DetectedEventInfo` must not be sized by `EI_CLASSIFIER_LABEL_COUNT` in shared code. Introduce
  `AI_MAX_LABELS` (e.g. 8) for the TFLM build.
- **New libraries** (vendored under `lib/`, each with a `library.json`):
  - `lib/tflite-micro/`: esp-tflite-micro **v1.3.4** sources.
  - `lib/esp-nn/`: esp-nn ≥ 1.1.1, the version v1.3.4 expects.
  - `lib/eloc_ml/`: new ELOC code, plus `third_party/kissfft/`.
  - Isolation: `esp32dev-tflm` sets `lib_ignore = edge-impulse`; `esp32dev-ei` sets
    `lib_ignore = eloc_ml, tflite-micro, esp-nn`.
  - **Both define `tflite::` symbols; linking both would silently mix two TFLM versions.** Verify
    with the linker map (T7).
- **Memory:**
  - Load the model once, and allocate every buffer when the model loads or the task starts. There
    are **no per-window mallocs**.
  - Put these in PSRAM:
    - audio double buffer: 2 × 16000 × int16 = 64 KB;
    - tensor arena: start at `max(config.arena_bytes_estimate × 1.25, 64 KB)`, 16-byte aligned via
      `heap_caps_aligned_alloc(16, …, MALLOC_CAP_SPIRAM)`;
    - feature matrix: 61 × 64 float = 15.6 KB.
  - Keep the small hot buffers (FFT in/out, power spectrum: 257 floats, window table) internal.
  - After `AllocateTensors()`, log `interpreter.arena_used_bytes()`.

## Tasks

### T0 — Toolchain spike (do first; stop and report if it fails)

1. Vendor esp-tflite-micro **v1.3.4** and esp-nn (the version its manifest pins, ≥ 1.1.1) as
   PlatformIO libraries.
2. Create env `esp32dev-tflm` as a copy of `esp32dev-ei`:
   - flags: `-DELOC_AI_ENABLED -DELOC_TFLM_ENABLED`, and **no** `EDGE_IMPULSE_ENABLED`;
   - `lib_ignore = edge-impulse`;
   - sdkconfig: create `sdkconfig.esp32dev-tflm` by copying `sdkconfig.esp32dev-ei`, then check it
     against `sdkconfig.defaults`.
3. Build a minimal program that:
   - maps the real model (see "Fixtures");
   - registers the ops in `SUPPORTED_OPS`;
   - calls `AllocateTensors()` and `Invoke()` on zeros;
   - logs `arena_used_bytes()`.
4. **Pass:**
   - it compiles with GCC 8.4;
   - it runs on hardware;
   - the output tensor is int8 [1,1].
5. **If v1.3.4 does not build with GCC 8.4** after trivial fixes, stop and report the errors, with
   this fallback order: v1.3.3 → v1.3.2 → the Apache-2.0 TFLM tree from a matching upstream
   `tensorflow/tflite-micro` commit. **Do not** take TFLM from `lib/edge-impulse`: that keeps the EI
   tree as a dependency and blocks Phase 3.
6. Record the pinned versions and any patches in `lib/tflite-micro/ELOC_VENDOR.md`, as
   `tools/patch_ei_fft_cache.py` does for EI.

### T1 — `ModelPackage`: parse and validate the embedded config

**`lib/eloc_ml/src/ModelPackage.{hpp,cpp}`:** `ModelPackage::load(const uint8_t* data, size_t len)`.
The model bytes must stay alive and 16-byte aligned for the lifetime of the interpreter.

1. `tflite::GetModel(data)`; read `metadata` entry `eloc_model_config`, then parse the JSON with
   ArduinoJson 6 (already a dependency). Use a `BasicJsonDocument` with a PSRAM allocator: the JSON
   is about 6 KB, and the base64 filterbank sits inside it.
2. Reject a package (with a readable error string) when any of these fails:
   - `format == "eloc-model"`, `format_version == 1`, `features.spec_version == 1`;
   - `fft_length` is a power of two; `frame_length <= fft_length`;
   - `n_frames == 1 + (window_samples - frame_length) / frame_step`;
   - `band_start`/`band_length` sizes equal `n_mels`;
   - the decoded weight count equals the sum of `band_length`;
   - `tensors.input.shape == [1, n_frames, n_mels]`; input and output are int8;
   - output count is 1 (sigmoid, 2 labels) or equals the label count (softmax);
   - `labels[0]` is background;
   - `inference.hop_samples == window_samples` and `average_count == 1` (Phase 1 supports only this);
   - `transform ∈ {log, pcen, pcen+log, linear}`.
3. Decode `weights_f32le_b64` with `mbedtls_base64_decode` (IDF mbedtls) into a PSRAM float array.
4. Expose plain structs: `FeatureConfig`, `TensorQuant{scale, zero_point}`, `labels[]`,
   `DetectionDefaults{threshold, observation_window_s, required_detections}` per label, `jobId`,
   `name`, `createdUtc`, `arenaEstimate`, `sampleRate`, `windowSamples`.

### T2 — `MelFrontend`: spec v1 in C++

**`lib/eloc_ml/src/MelFrontend.{hpp,cpp}`** implements exactly the steps in
`DEVICE_MODEL_PACKAGE.md` → "Feature front-end, spec version 1", with float32 throughout:

1. `x = sample / 32768.0f`
2. Frames at `t · frame_step`, of `frame_length`, with no end padding.
3. Periodic Hann `0.5 − 0.5·cos(2πn/frame_length)`, precomputed once.
4. Zero-pad to `fft_length` → real FFT through a `RealFft` wrapper around `kiss_fftr` (plan
   allocated once per model; the wrapper is the only place an ESP-DSP swap would touch) →
   `power = re² + im²`.
5. Sparse mel: `mel[m] = Σ_{k=start}^{start+len−1} power[k] · w`; a band with len 0 gives 0.
6. PCEN when configured: state reset to 0 at the start of **every window**.
7. log: `logf(fmaxf(v, 1e-6f))`.
8. `(v − mean) / divisor`, stored time-major `[t·n_mels + m]`.

Then quantize straight into the TFLM input tensor:
`clamp(roundf(v / scale) + zero_point, −128, 127)`. `roundf` rounds halves away from zero while NumPy
rounds half to even; the ±1 tolerance covers this.

**Native test** (`test/test_generic_mel_frontend`, runs in the existing `generic_unit_tests` env):
- read `test/fixtures/eloc_model.tflite` and `test/fixtures/eloc_golden_vectors.bin`
  (layout: `DEVICE_MODEL_PACKAGE.md` → "Golden vectors");
- assert features within **1e-3** absolute and the quantized input within **±1**, for every record,
  including the synthetic ones (silence, full-scale sine, noise).

KissFFT and the parser must compile on the host for this, so keep ESP/Arduino headers out of
`MelFrontend` and `ModelPackage`.

### T3 — `TflmClassifier`

**`lib/eloc_ml/src/TflmClassifier.{hpp,cpp}`:**
- `MicroMutableOpResolver<N>` registering **exactly** `SUPPORTED_OPS` from `device_package.py`: ADD,
  AVERAGE_POOL_2D, BATCH_TO_SPACE_ND, CONCATENATION, CONV_2D, DEPTHWISE_CONV_2D, DEQUANTIZE,
  EXPAND_DIMS, FULLY_CONNECTED, LOGISTIC, MAX_POOL_2D, MEAN, MUL, PAD, QUANTIZE, RELU, RELU6,
  RESHAPE, SOFTMAX, SPACE_TO_BATCH_ND, SQUEEZE, STRIDED_SLICE.
- **This list is a two-sided contract:** the worker marks any model using another op as not
  firmware-ready. Put a comment in both files pointing at the other.
- Arena in PSRAM (see Memory). On `AllocateTensors()` failure, retry once at twice the size, then
  fail with an error.
- `classify(const int16_t* window, float* probs)`:
  1. front-end;
  2. quantize;
  3. `Invoke()`;
  4. dequantize `(q − zp) · scale`;
  5. map to per-label probabilities: sigmoid gives `[1−p, p]`, softmax gives `out[i]`.

  Return the DSP and NN times in ms.

**On-target test** (`test/test_target_tflm`, modelled on `test/test_target_ai_model`, which already
reads from the SD card):
- read the golden `.bin` from the SD card;
- assert the quantized input within **±1** and the raw int8 output within **±2** (ESP-NN may differ
  from TFLite reference kernels by one step);
- log DSP ms, NN ms, `arena_used_bytes()`, and the internal/PSRAM free heap before and after.

### T4 — `ElocDetector` + shared wiring

1. **`lib/eloc_ml/src/ElocDetector.{hpp,cpp}`** mirrors `EdgeImpulse`'s public surface (see "Target
   design"). Its thread:
   - is pinned to `TASK_AI_CORE` at `TASK_PRIO_AI` with an **8 KB** stack;
   - waits on `xTaskNotifyWait` exactly like `EdgeImpulse::ei_thread()`;
   - on stop, exits cooperatively and deletes only itself.
   - Log `uxTaskGetStackHighWaterMark` after the first inference.
2. **Refactor the per-window block** of `ei_callback_func()` (from the `print_results` check through
   the CSV write) into `handleClassification(labels, values, n, dspMs, nnMs)`:
   - Both runtimes call it. The EI path passes `result.classification[]` and `result.timing`.
   - **The EI behaviour must stay byte-identical:** same log lines, same CSV format, same
     threshold (`value > threshold`, strictly greater).
   - In the TFLM path, index 0 is background by definition. Also keep the existing name checks for
     "background/other/others".
3. **Switch shared sites** from `EDGE_IMPULSE_ENABLED` to `ELOC_AI_ENABLED` and from `edgeImpulse`
   to `aiRuntime`:
   - `I2SMEMSSampler.cpp`; `ElocStatus.hpp`; `ElocLora.cpp`;
   - `ElocCommands.cpp` (status block);
   - `ElocSystem.cpp:1034` (CPU boost);
   - `main.cpp`: duty-cycle sync, main-loop queue, SD-remount re-arm, timer-wake CSV rebuild.

   EI-only code stays under `EDGE_IMPULSE_ENABLED`:
   - the `EdgeImpulse.hpp` include and `test_samples.h`;
   - `microphone_audio_signal_get_data`;
   - the disabled `if (0)` self-test;
   - `ei_porting_overrides.cpp`;
   - the `EI_CLASSIFIER_SENSOR` check.
4. **Sample rate:**
   - Register with `input.register_ei_inference(&aiRuntime.getInference(), model.sampleRate)`.
   - Refuse to start AI when the I2S rate is not an integer multiple (≥ 1) of `model.sampleRate`.
     The sampler would silently mis-decimate.
   - Buffers come from `buffers_setup(model.windowSamples)`.
5. **Model source (Phase 1):**
   - `lib/eloc_ml/model/eloc_model_data.h`, copied verbatim from the package. Its array is already
     `__attribute__((aligned(16)))`. Include it in exactly one `.cpp`.
   - If the model fails validation, log the reason, keep `ai_run_enable = false`, and report it in
     status (T6). **Never crash.**
   - Load at boot, before BT starts, so the allocations happen while the heap is quiet.

### T5 — Silence guard

- If a window's peak `|sample| ≤ AI_SILENCE_PEAK` (new `project_config.h` constant, suggested **16**
  LSB), skip the front-end and inference for that window. Treat it as no detection, count it
  (`silentWindows`), and log at most once per minute.
- Role: a **dead-mic safety net and indicator**, not the fix for finding 1.
  - The fix is in training data: every model's background set gets digital silence and very
    low-level noise (Decision 3).
  - The guard still keeps a muted or disconnected mic from being scored, makes a dead mic visible
    through `silentWindows`, and skips pointless work.
  - At a peak of 16 LSB or less, nothing detectable is lost.

### T6 — Status, CSV, identity

- **Status** (`ElocCommands.cpp` `detection` object). Unknown keys are ignored by old apps (see the
  existing comment there).
  - `aiModel` = `config.model.name` (it was `EI_CLASSIFIER_PROJECT_NAME`).
  - New keys in the TFLM build only:
    - `aiRuntime: "tflm"`;
    - `aiModelId` = job id;
    - `aiModelCreated`;
    - `aiLabels` (array);
    - `aiModelDefaults: {threshold, observationWindowS, requiredDetections}` for the first
      non-background label;
    - `aiArenaUsed`;
    - `aiLastMs: {dsp, nn}`;
    - `silentWindows`;
    - `aiError` (empty when fine).
  - The EI build adds `aiRuntime: "ei"` only.
- **`device.buildVariant`:** keep reporting `"ei"` from the TFLM build (Decision 1, confirmed), so
  the app's variant guard lets the user switch between the EI and TFLM builds with the app's file
  picker.
- **SD results CSV:**
  - Same header style and row format (`HH:MM:SS Ddd, Mon D YYYY, v0, v1…`), columns from
    `config.model.labels`.
  - Filename `EI-results-TFLM-<jobId>.csv`, built by one function used by both the create path and
    the timer-wake path. The `EI-results` prefix keeps the web app's Model Results Comparison
    loading it: it matches `/EI-results.*\.csv$/i`; the model name falls back to the filename.
- **LoRa:** no change. Labels are truncated to 5 characters as before ("chainsaw" → "chain").

### T7 — Build hygiene and verification

1. Both `pio run -e esp32dev-ei` and `pio run -e esp32dev-tflm` build from a Full Clean.
2. The `esp32dev-tflm` linker map (`firmware.map`) contains **no** `edge-impulse`, `ei_` or
   `EdgeImpulse` objects, and exactly one copy of the TFLM sources.
3. The TFLM `firmware.bin` fits the OTA slot (`elocPartitions.csv`). Report both binary sizes.
4. The `esp32dev-ei` behaviour is unchanged: same boot log AI lines, same status JSON (plus
   `aiRuntime:"ei"`), same CSV.
5. Native test (T2) and on-target test (T3) pass.

### T8 — Documentation (same pass, per the repo rules)

- **`README-ai.md`:** a new top section, "TFLM runtime (`esp32dev-tflm`)". Updating the model
  becomes:
  1. download `eloc_device_package.zip` from ELOC Model Training;
  2. replace `lib/eloc_ml/model/eloc_model_data.h`;
  3. Full Clean + build `esp32dev-tflm`.

  Keep the EI section for the fallback build.
- **`CLAUDE.md`:** the build command list; note that the default env stays `esp32dev-ei` until
  Phase 3.
- **`memory-bank/`:** `techContext.md` (new libs, flags, env), `systemPatterns.md` (the AI pipeline
  above), `activeContext.md`/`progress.md`.
- **`VERSIONS.md` + `VERSION` +0.01** on push, one line per version.
- **GitHub wiki** `LIFsCode/ELOC-3.0.wiki.git`:
  - `Settings ‐ Config & Status`: the new status keys, and `aiModel` now showing the web-app model
    name in the TFLM build.
  - `ELOC-3.0-App-Interface`: no new commands in Phase 1, but check it.
  - If the user-visible AI behaviour gets a page, describe the silence guard there.
- **`ELOC_management/DEVICE_MODEL_PACKAGE.md`:** only if the firmware reveals a spec ambiguity. Any
  change there is a format change (bump `format_version`).

## Fixtures (for T0–T3)

Use the real chainsaw package from job `SkRQW4hUFaXslHibnmv9`: 14,208-byte model, 9 golden records,
config verified above.

```bash
gcloud storage cp gs://eloc-b1e63.appspot.com/model-training/artifacts/SkRQW4hUFaXslHibnmv9/eloc_device_package.zip .
```

Unzip it. Copy the files as follows:
- `eloc_model_data.h` → `lib/eloc_ml/model/`;
- `eloc_model.tflite` and `eloc_golden_vectors.bin` → `test/fixtures/`;
- `eloc_golden_vectors.bin` → the SD card too, for T3.

The user can instead download it from the web app: Tools → ELOC Model Training → that experiment →
**Download package**.

The Python reader `device_package.read_golden_vectors()` in `ie/cloud_training/` is the reference
for the golden-vector layout. `FeatureSpec.from_config()` there rebuilds the front-end from the
embedded JSON alone and matches the golden features exactly.

## Decisions (answered by the user 2026-09-24)

1. **`buildVariant` in the TFLM build: confirmed, `"ei"` plus `aiRuntime: "tflm"`.**
   - The app treats both builds as "the AI build", so the user can flash either one from the app
     picker.
   - A `"tflm"` variant would make the app's variant guard refuse picker updates between the two.
   - Revisit in Phase 3, when TFLM becomes the published AI build.
2. **The model's recommended detection settings: show only, never apply.**
   - They go into the device's status JSON as `aiModelDefaults`. That is what the app reads for its
     status page.
   - The current app ignores unknown keys, so it won't *display* them. Displaying them is a small,
     optional app change that fits Phase 2, when models become swappable from the app.
   - Never overwrite the user's `threshold`/`observationWindowS`/`requiredDetections`.
   - **User requirement:** the Android app must keep showing the device's *real* threshold (and the
     observation window and required detections) and keep changing them. It already does, in
     Device Settings through `setConfig` → `inference`. The TFLM path must read `getInferenceConfig()`
     on every window, as `ei_callback_func()` does today, so an edit from the app takes effect on the
     next window without a reboot.
3. **Silence: fix it in the training data.**
   - Every model's background set must contain digital silence and very low-level noise.
   - This is a web-app worker follow-up: add such windows automatically, so no dataset can forget
     them.
   - The firmware guard (T5) stays only as the dead-mic safety net and indicator.
4. **Bench model: confirmed.** Job `SkRQ…` is for bring-up only. The user will retrain on a bigger,
   better dataset (plus the silence windows) before field testing.

## Out of scope for Phase 1

- Loading the model from the SD card or over Bluetooth (Phase 2). The loader already takes a buffer:
  add a file source, validation and fallback to the compiled-in model.
- Several models at once; continuous or overlapping inference; score averaging.
- Switching the FFT to ESP-DSP. Measure KissFFT in T3 first; the `RealFft` wrapper keeps a later
  swap local.
- Migrating to ESP-IDF 5 / Arduino core 3 (see "Staying on ESP-IDF 4.4").
- Retiring `esp32dev-ei`, changing the default env, publishing TFLM releases (Phase 3).
- Web-app changes. Any spec ambiguity found here goes back to `DEVICE_MODEL_PACKAGE.md` with a
  format bump.

## Sequencing for implementing agents

- **T0** gates everything. Report to the user if it fails.
- **T1 → T2** (native test green) → **T3** (on-target test green) → **T4 → T5 → T6** → **T7** → **T8**.
- Commit per task on `EDsteve/Patrol` with selective staging only:
  - **never** `keyfile.csv` or `nvs.csv` (per-device secrets);
  - the working tree usually carries unrelated WIP (e.g. the V1.78 work in `main.cpp`,
    `project_config.h`, `ElocLora_survey.cpp`), so stage explicit paths;
  - check `git diff --cached --name-only` against `git diff --name-only` before committing.
- The user builds and flashes firmware themselves (`pio run -e <env>`). Hand over at T7 with exact
  build commands and a bench checklist. Do not claim hardware results you have not seen.

## End-to-end acceptance (real hardware, by the user)

1. Boot the TFLM build:
   - the log shows the model name, job id, arena used, stack high-water mark and no errors;
   - status shows `aiRuntime:"tflm"`, `aiModel`, and `aiModelDefaults`.
2. The golden-vector on-target test passes within tolerances.
3. Start detection from the app. Play chainsaw recordings at a realistic level:
   - detections appear in the log as `(DSP: … ms., Classification: … ms.)`;
   - `EI-results-TFLM-<jobId>.csv` is written and opens in the web app's Model Results Comparison;
   - a LoRa event reaches TTN / the web map with label `chain`.
4. In the app's Device Settings, the TFLM build shows the real inference threshold, observation
   window and required detections. Change the threshold (e.g. 90 → 99) while detection runs: a
   clip that triggered before stops triggering on the next window, with no reboot. Set it back.
5. Mute or disconnect the mic: no detections, and `silentWindows` increases.
6. Duty-cycle mode: AI resumes after a timer wake, and the CSV appends to the same file.
7. **24 h soak** with BT + LoRa + GPS + AI:
   - no panics or classifier errors;
   - `ENABLE_HEAP_MONITOR` shows internal heap headroom at least as good as the EI build.

   This is the bar the current EI build fails intermittently.
8. The EI build flashed back onto the same unit behaves as before.
