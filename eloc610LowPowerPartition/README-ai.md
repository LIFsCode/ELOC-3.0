# TFLM runtime (`esp32dev-tflm`)

The `esp32dev-tflm` build runs AI detection on **TensorFlow Lite Micro** directly, with no Edge
Impulse code linked. It runs the **device model package** that ELOC Model Training in the web app
builds after every training job. The format is `ELOC_management/DEVICE_MODEL_PACKAGE.md`, and the
plan with every decision is `README-TFLM-Runtime-Plan.md`.

For the field user nothing changes:
- same Bluetooth commands;
- same inference settings (`threshold`, `observationWindowS`, `requiredDetections`), still read on
  every window, so a change from the app applies without a reboot;
- same SD results CSV format, LoRa event message and duty cycle.

`esp32dev-ei` below stays the default and the fallback build until the TFLM build has proved itself
in the field (Phase 3 of the plan).

## Build

```bash
pio run -e esp32dev-tflm
```

Do a **Full Clean** (`pio run -e esp32dev-tflm -t clean`) after replacing the model header, and
when switching between `esp32dev-ei` and `esp32dev-tflm`.

## Updating the model

1. In the web app: Tools → ELOC Model Training → the experiment → **Download package**
   (`eloc_device_package.zip`).
2. Replace `lib/eloc_ml/model/eloc_model_data.h` with the one from the zip.
3. Replace `test/fixtures/eloc_model.tflite` and `test/fixtures/eloc_golden_vectors.bin` from the
   same zip. The native test checks that the header and the fixture are the same model.
4. Full Clean, then build `esp32dev-tflm`.

The model carries its whole configuration inside it: labels, sample rate, window, mel front-end and
quantization. Nothing else in the firmware changes with the model. A model the firmware cannot run
is rejected at boot with a readable reason. The reason is logged and shown in status
`detection.aiError`, AI refuses to start, and everything else runs normally. Rejected models
include:
- a newer package format;
- an unsupported operator;
- more than `AI_MAX_LABELS` (8) labels;
- overlapping or averaged windows.

## Tests

```bash
# On the PC: package parser + mel front-end against the package's golden vectors
pio test -e generic_unit_tests -f test_generic_mel_frontend

# On the device: whole classifier on the golden vectors, with timings and memory.
# First copy test/fixtures/eloc_golden_vectors.bin to the SD card root.
pio test -e target_tflm_tests
```

The on-target test prints three things:
- DSP and NN time per window at 240 MHz;
- `arena_used_bytes()`;
- internal and PSRAM heap before and after.

It asserts the quantized model input within ±1 and the raw output within ±2 of the reference.

## What the device reports

Boot log (tag `ElocDetector`):
- the model name, job id and creation date;
- the load time;
- the arena used and allocated;
- the heap held while AI is off;
- the recommended detection settings, marked "not applied".

When AI starts, it logs:
- the front-end buffers;
- the decimation (e.g. `I2S 16000 Hz -> model 16000 Hz`);
- the AI task's stack high-water mark after the first window.

Status (`getStatus`, object `session.detection`):

| Key | Meaning |
|---|---|
| `aiRuntime` | `"tflm"` (the Edge Impulse build reports `"ei"`) |
| `aiModel` | model name from ELOC Model Training |
| `aiModelId`, `aiModelCreated` | training job id and creation time |
| `aiLabels` | label list, background first |
| `aiModelDefaults` | the model's recommended `threshold` (0–100), `observationWindowS`, `requiredDetections` for its first target label. Shown only, never applied: the device's own inference config always wins |
| `aiArenaUsed` | bytes of the tensor arena the model really uses |
| `aiLastMs` | `{dsp, nn}` milliseconds of the last classified window |
| `silentWindows` | windows skipped by the silence guard since boot |
| `aiError` | why AI cannot run; empty when fine |

`device.buildVariant` stays `"ei"` in the TFLM build. The app's variant guard therefore lets a
unit switch between the two AI builds from the app's file picker.

Other outputs:
- **SD results CSV**: `EI-results-TFLM-<jobId>.csv` in the session folder, with the same header
  style and rows as the EI build and one column per label. The `EI-results` prefix keeps the web
  app's Model Results Comparison loading it.
- **LoRa**: unchanged. Labels are truncated to 5 characters (`chainsaw` → `chain`).

## Silence guard

A window whose peak |sample| is at most `AI_SILENCE_PEAK` (16 LSB, about -66 dBFS) is not
classified. It counts as no detection and adds to `silentWindows`, logged at most once a minute.
It is a **dead-mic safety net and indicator**:
- a muted or disconnected microphone is not scored, and shows up in status;
- a model trained without silent audio can score digital silence as a detection (the first
  chainsaw model scores 0.95 on it).

The real fix for the second point is silent and low-level windows in every model's background
training data, which is a web-app worker task.

## Limits (Phase 1)

- One model at a time, compiled in. Loading one from the SD card or pushing it from the app is
  Phase 2; the loader already takes any memory buffer.
- Back-to-back, non-overlapping windows with no score averaging, as the EI build runs today
  (`AI_CONTINUOUS_INFERENCE` off).
- The I2S sample rate must be a whole multiple of the model's (e.g. 16 or 32 kHz for a 16 kHz
  model). The sampler keeps every Nth sample with no filter, which is exactly what the model was
  trained on (`training_resampling: "firmware"`). Any other rate is refused at AI start, with the
  reason in `aiError`.

## Code map

| Where | What |
|---|---|
| `lib/eloc_ml/` | Portable core, also built on the PC for the native test: `ModelPackage` (parse and validate the embedded config), `MelFrontend` + `RealFft` (spec v1 front-end, KissFFT), `TflmClassifier` (arena, op resolver, Invoke), `GoldenVectors`, `CompiledModel` (the only file that includes the model header) |
| `lib/eloc_detector/` | `ElocDetector`: the AI task, audio buffers, silence guard, event counters. Same public names as `EdgeImpulse` |
| `include/ai_runtime.h` | `AiRuntime` = `EdgeImpulse` or `ElocDetector`; the shared code uses `aiRuntime` |
| `src/main.cpp` `handleClassification()` | Per-window detection rules shared by both runtimes: threshold, recording start, observation window, event count, CSV row |
| `lib/tflite-micro/`, `lib/esp-nn/` | esp-tflite-micro v1.3.4 and ESP-NN v1.1.2, unchanged; see `lib/tflite-micro/ELOC_VENDOR.md` |

The ops the firmware registers (`registerOps()` in `TflmClassifier.cpp`) must equal
`SUPPORTED_OPS` in the training worker's `device_package.py`. The worker marks any model using
another op as not firmware-ready, so change both or neither.

Memory:
- **PSRAM, from boot:** the model's arena, features and audio double buffer. Nothing is allocated
  per window.
- **Internal RAM (~12.6 KB), only while the AI task runs:** the FFT plan and per-frame buffers. It
  falls back to PSRAM if internal RAM is short.
- **AI task stack:** `AI_TASK_STACK_SIZE` (8 KB).

---

# Edge Impulse build (`esp32dev-ei`, current default and fallback)

## To build the ai 'branch'/ version of this project:
Enter the folder containing the project (e.g. cd ~/Documents/PlatformIO/Projects/eloc610LowPowerPartition)

1. git checkout ai
2. git pull

Build using 'esp32dev-ei' in the 'Project Tasks':
1.  'Full Clean'   **(Very important, otherwise changes to any files in lib/edge-impulse/src/ will not be pulled into .pio build folder)**
2.  'Build'
3.  'Upload'

# To update the Edge Impulse model follow these steps:
1. Open the Edge Impulse Studio and navigate to the project containing the model you want to use.
2. Ensure the correct target device is selected (i.e. ESP32).
3. Click on the "Deployment" tab.
4. Select the deployment you want to update (i.e. "Arduino Library").
5. Click on the "Builde" button & download the zip file.
6. Delete the following folders from this project. This may not be strictly necessary as some fo these folders may be unchanged but it's a good idea to ensure that the latest versions are used:  
    `lib/edge-impulse/src/edge-impulse-sdk`  
    `lib/edge-impulse/src/model-parameters`  
    `lib/edge-impulse/src/tflite-model`  
    `lib/edge-impulse/src/<model-header-file.h>`  
7. Copy the three new folders (edge-impulse-sdk, model-parameters & tflite-model) & model header file from the downloaded model into 'lib/src/'. 
8. Amend the following line (currently #10) in /lib/edge-impulse/src/EdgeImpulse.cppn.cpp with the correct model header file name (if necessary)
    `#include "trumpet_inferencing.h"`
9. The project-local pre-build script `tools/patch_ei_fft_cache.py` automatically reapplies the ELOC
   reusable-KissFFT-plan patch when a new SDK export overwrites
   `lib/edge-impulse/src/edge-impulse-sdk/dsp/numpy.hpp`. The script is idempotent and deliberately
   stops the build if a future SDK changes `numpy::software_rfft()` instead of patching unknown code.
   In the build output, verify it reports either `patched numpy.hpp` or `already patched
   (// ELOC-FFT-CACHE)`. Without this patch the current 32-frame MFE model rebuilds its ~10.5 KB FFT
   plan for every frame and DSP time can regress from the hardware-validated 52–54 ms to 600–900 ms.
   The cache adapts automatically to a different `n_fft`; it assumes serialized inference.
10. Under 'esp32dev-ei' in the 'Project Tasks' menu run:
    'Full Clean' **(Very important, otherwise the new model will not be pulled into .pio build folder)**
    'Build'
    'Upload'

## Troubleshooting
1. I've noticed that at startup there are errors about failing to run the inference model (or similar). When the Bluetooth task is suspended (after 30sec?) the problem seems to resolve itself & predictions will be visible.
2. esp32dev-ei might not appear under 'Project Tasks'. The refresh button above will do the trick.
3. Compile error:
    ```
    lib/edge-impulse/src/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp:157:21: 
    error: 'output_size' may be used uninitialized in this function [-Werror=maybe-uninitialized]
    TfLiteIntArrayFree(output_size);
    ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~
    cc1plus: some warnings being treated as errors
    [.pio/build/esp32dev-ei/lib7f0/edge-impulse/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp.o] Error 1   
    ```
    Modify line 120 of lib/edge-impulse/src/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp to read:
    ```
    TfLiteIntArray* output_size = nullptr;
    ```
5. Compile error:
    ```
    collect2.exe: error: ld returned 1 exit status
    *** [.pio\build\esp32dev-ei-windows\firmware.elf] Error 1
    ```
    Solution: Comment out this line in platform.io: -DEI_CLASSIFIER_ALLOCATION_STATIC=1
    It's a RAM issue. More info here: https://github.com/LIFsCode/ELOC-3.0/issues/79


