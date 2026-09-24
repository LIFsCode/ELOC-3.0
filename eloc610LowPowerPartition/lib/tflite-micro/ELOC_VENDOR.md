# Vendored third-party code for the TFLite Micro runtime (`esp32dev-tflm`)

Three libraries are copied into this repo unchanged. None of them come from the Edge Impulse SDK,
whose `dsp/` folder and generated files carry a paid-subscription licence notice. Updating one
means re-copying the upstream tree at a new tag, redoing the file lists below, and running both
tests again (`test_generic_mel_frontend`, `test_target_tflm`).

| Library | Upstream | Pinned | Licence | Where |
|---|---|---|---|---|
| esp-tflite-micro | github.com/espressif/esp-tflite-micro | tag **v1.3.4** (tag object `fb9164125fd6`, commit `1171b51af69d`) | Apache-2.0 | `lib/tflite-micro/` |
| ESP-NN | github.com/espressif/esp-nn | tag **v1.1.2** (commit `596b08401a63`) | Apache-2.0 | `lib/esp-nn/` |
| KissFFT | github.com/mborgerding/kissfft | tag **131.2.0** (commit `7bce4153c6bc`) | BSD-3-Clause | `lib/eloc_ml/third_party/kissfft/` |

## Why these versions

- **esp-tflite-micro v1.3.4 is the last tag that declares ESP-IDF ≥ 4.4.** v1.3.5–v1.3.8 need
  IDF ≥ 5.0 and v1.4.x needs IDF ≥ 5.1. This firmware stays on IDF 4.4.7 (arduino-esp32 2.0.x)
  until the ESP32 hardware upgrade.
- **ESP-NN v1.1.2** meets v1.3.4's `espressif/esp-nn >= 1.1.1` and was released the same day as it
  (2025-09-01). v1.2.0 also allows IDF ≥ 4.2, but differs only in an ESP32-S3 file and tests;
  v1.3.0 and later need IDF ≥ 5.1.
- **KissFFT** is used only by the mel front-end (`lib/eloc_ml/src/RealFft.cpp`). TFLM's own
  `third_party/kissfft` copy is not compiled (see below).

## What was copied

- esp-tflite-micro: `tensorflow/`, `third_party/`, `signal/`, `LICENSE`, `README.md`. Left out:
  `examples/`, `scripts/`, the IDF `CMakeLists.txt` and `idf_component.yml`.
- ESP-NN: `include/`, `src/`, `LICENSE`, `README.md`. Left out: `tests/`, `test_app/`, the IDF
  build files.
- KissFFT: `kiss_fft.c/.h`, `kiss_fftr.c/.h`, `_kiss_fft_guts.h`, `kiss_fft_log.h`, `COPYING`,
  `LICENSES/`.

## Build (PlatformIO instead of the IDF component manager)

- `lib/tflite-micro/library.json` compiles exactly the source list of upstream's `CMakeLists.txt`:
  the seven kernels ESP-NN replaces (add, conv, depthwise_conv, fully_connected, mul, pooling,
  softmax) come from `kernels/esp_nn/`, and `micro/esp/micro_time.cc` replaces `micro_time.cc`.
  - Two upstream parts are **not** compiled because the ELOC front-end does not use them:
    `signal/` (TFLM's signal-processing ops) and `experimental/microfrontend/`. Their headers stay,
    since `micro_ops.h` includes them. This also keeps TFLM's KissFFT copy out of the link.
  - Flags from upstream: `-DESP_NN -DTF_LITE_DISABLE_X86_NEON -O3` plus its warning relaxations.
    `TF_LITE_STATIC_MEMORY` and the `third_party` include paths are in `platformio.ini` `[tflm]`
    instead, because every file that includes a TFLM header needs them (the define changes the
    layout of `TfLiteTensor`).
- `lib/esp-nn/library.json` compiles upstream's ESP32 source list (the ANSI and generic "opt"
  versions; the `.S` files are ESP32-S3 only).
- `-DCONFIG_NN_OPTIMIZED` is set in both libraries. Under IDF it comes from ESP-NN's Kconfig,
  which PlatformIO libraries do not get. It selects the generic optimised conv, depthwise conv and
  softmax kernels for the ESP32 (check the linker map for `esp_nn_conv_s8_opt`).
- Isolation: `esp32dev-tflm` sets `lib_ignore = edge-impulse`, and `esp32dev-ei` sets
  `lib_ignore = eloc_ml, eloc_detector, tflite-micro, esp-nn`. Both trees define `tflite::`
  symbols, so a build must never link both.

## Patches

**None.** v1.3.4 and ESP-NN v1.1.2 built with GCC 8.4.0 (esp-2021r2-patch5) with no source change
and no warning from these libraries (2026-09-24).

One compiler bug did show up in ELOC's own code, not the vendored trees. GCC 8.4 for Xtensa fails
with "insn does not satisfy its constraints" (an ICE in postreload) when the mel band sum is inlined
into `MelFrontend::compute()`. It is avoided with a `noinline` helper; see `bandSum()` in
`lib/eloc_ml/src/MelFrontend.cpp`.
