# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Read the memory bank first

Before starting any non-trivial task, read the files in `memory-bank/` — they are the authoritative source for project intent, current focus, and design rationale, and are kept up to date by contributors:

- `memory-bank/projectbrief.md` — scope, hardware, partition layout, pin map, task model
- `memory-bank/systemPatterns.md` — architecture, double-buffering, duty-cycle state machine, MicVolume2_pwr gain math
- `memory-bank/techContext.md` — build flags, dependencies, flash/SD layout, NVS namespaces
- `memory-bank/activeContext.md` — current work focus, recent changes, decisions in flight
- `memory-bank/progress.md` — what works, what's left, known issues
- `memory-bank/productContext.md` — product motivation
- `memory-bank/changelog.md` — dated history of *completed* work, moved out of `activeContext.md`; read only when you need the history

`.clinerules` documents the workflow expectation: read all six core files on every task. When you finish a significant change, update `activeContext.md` and `progress.md` so the next session has the latest state — and when a work stream is *finished*, move its dated entry from `activeContext.md` → `changelog.md` (verbatim, newest first) so activeContext stays focused on current work. The user can trigger a full review with the phrase **"update memory bank"**.

**Every version bump gets a line in [`VERSIONS.md`](VERSIONS.md)** (repo root): bump `VERSION` in
`include/project_config.h` by +0.01 per push, then add a one-line entry there, newest first. It is the
quick index over the firmware versions; `memory-bank/changelog.md` holds the long-form analysis.

Auxiliary deep-dives also live at the repo root: `README-DutyCycle-and-LoRa-Cooldown.md`, `README-DutyCycle-BugFixes.md`, `README-LoRa Session Persistence.md`, `README-DMM-4026-B-I2S-R.md`, `README-ai.md`, `README-nvs.md`, `README-Config-Restart-Semantics.md` (which `setConfig` fields apply live vs. require a reboot).

## What this firmware is (load-bearing context)

This codebase is **partition1 (the low-power recording partition)** of a dual-OTA ESP32 image. It cannot run standalone — the high-power partition at `0x10000` is required for boot/Bluetooth bootstrap. Flashing only this binary onto a blank device will not produce a working unit. See the partition layout in `elocPartitions.csv` and `memory-bank/techContext.md`.

It targets ELOC 3.0 hardware (ESP32-WROVER, 16 MB flash, PSRAM, ICS-43434 I2S mic, SX1262 LoRa, SDIO SD card, LIS3DH accel, PCA9557 expander). Pin assignments are centralized in `include/project_config.h`.

## Build / flash / test commands

The default environment is `esp32dev-ei` (defined in `platformio.ini`). Use `pio run -e <env>` to target a specific one.

```bash
# Build (no AI)
pio run -e esp32dev

# Build with Edge Impulse AI inference
pio run -e esp32dev-ei

# Upload firmware (auto-detects port via tools/setUploadMonitorPort.py)
pio run -e esp32dev -t upload

# Serial monitor (115200 baud, with esp32_exception_decoder + log2file)
pio device monitor

# Manual flash of just the firmware binary at the partition1 offset
# (the partition table places this firmware at 0x150000 historically;
# verify the current offset against elocPartitions.csv before using)
esptool.py --port COMx write_flash 0x150000 .pio/build/esp32dev/firmware.bin

# Flash NVS partition only (initial provisioning of LoRa keys, serial, etc.)
python -m esptool write_flash 0x9000 .pio/build/esp32dev/nvs.bin
# or use the helper:
python flashNVSonly.py
```

Edge Impulse builds **must do a Full Clean** before Build whenever any file under `lib/edge-impulse/src/` changes — otherwise stale objects remain in `.pio/`. See `README-ai.md` for the full model-update procedure and known EI SDK patches (e.g. `select.cpp` uninitialized `output_size`).

### Tests

```bash
# Run the single test named in platformio.ini's `selected_tests` (default: test_target_ElocConfig)
pio test -e target_unit_selected_tests

# Run every test in test/test_target_* on real hardware
pio test -e target_unit_all_tests

# Run desktop/native tests (test/test_generic_*)
pio test -e generic_unit_tests
```

To run a different single target test, edit `selected_tests = test_target_<name>` in `platformio.ini` under `[options]`. Tests live under `test/test_target_*` (on-device) and `test/test_generic_*` (native); `test/test_*` (without prefix) builds for both. See `test/README.md`.

### Pre-build scripts (run automatically by PlatformIO)

- `tools/genVersion.py` — regenerates `src/version.h` with git hash + build timestamp
- `tools/genNVS.py` — compiles `nvs.csv` → `nvs.bin` for the NVS partition
- `tools/setUploadMonitorPort.py` — auto-selects the upload/monitor serial port
- `AutoFlasher.py` — batch device provisioning helper (separate from PlatformIO flow)

## Architecture in one screen

FreeRTOS multi-task layout split across the two ESP32 cores; details in `memory-bank/systemPatterns.md`.

- **Core 0**: I2S read (prio 10), WAV writer (prio 8), main loop (control + state machine).
- **Core 1**: Edge Impulse inference (prio 7), LoRa work.
- **Producer/consumer**: `I2SMEMSSampler` (audio_input) fills double buffers; `WAVFileWriter` (wav_file) drains to SD; `EdgeImpulse` consumes a parallel buffer.
- **Singletons**: `ElocSystem`, `ElocLora`, `Battery` — accessed via `GetInstance()`. New hardware subsystems should follow the same pattern.
- **Control flow**: Bluetooth commands and the boot button feed FreeRTOS queues (`rec_req_evt_queue`, `rec_ai_evt_queue`); the main loop in `src/main.cpp` polls them and orchestrates state transitions.
- **Duty-cycle deep sleep**: state machine in `ElocStatus.hpp` (`SleepCycleState_t`); persistent state in `RTC_DATA_ATTR rtc_duty_cycle_t` (magic `0xE10CDC1E`); LoRaWAN session in RTC, DevNonces in NVS. **Timer-wake takes a fast-boot path** that skips Battery/PerfMonitor/Bluetooth/LED animation/LoRa serial-monitor delay — preserve this when adding boot-time work; gate any new heavy init on `esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER`.
- **Configuration cascade**: SD card (`/sdcard/eloc/*.config`) overrides SPIFFS (`/spiffs/*.config`) overrides compile-time defaults. Runtime BT config writes propagate back to SD/SPIFFS.

## Conventions specific to this repo

- **Library isolation**: each subsystem is its own PlatformIO library under `lib/`. **Do not declare external PlatformIO `lib_deps` on libraries inside `/lib`** — it triggers strange build errors. Shared third-party libs go in `[env].lib_deps` in `platformio.ini`. Do not include files from `/lib` in `lib_deps` either (per the comment in `platformio.ini`).
- **Feature flags live in `include/project_config.h`** as `#define`s (AI, perf monitor, test UART, mic model, PSRAM placement of buffers, etc.). Don't add new feature toggles elsewhere. Note the warning in that file: it must not include any other headers — the include path is relative and depends on every includer's location.
- **Mic gain** is `MicVolume2_pwr` in the JSON config. The math is bit-shift based: `shift = (32 - I2S_BITS_PER_SAMPLE) - volume2_pwr`, so `+1` doubles amplitude (+6 dB), `-1` halves it. Defaults: ICS-43434 = −4, DMM-4026 = −3, SPH0645 = −3. The disabled `ENABLE_AUTOMATIC_GAIN_ADJUSTMENT` path uses `>>=`/`<<=` on the exponent (non-linear jumps) — known bug, leave disabled until reworked. Full derivation in `systemPatterns.md`.
- **LoRaWAN nonces must be loaded from NVS only**, never from RTC, on session restore. Loading from RTC causes DevNonce regression and join failures. See `ElocLora_persistence`.
- **Edge Impulse buffers go in PSRAM** (`EI_BUFFER_IN_PSRAM`); WAV and I2S buffers stay in internal SRAM for speed. Don't move them without measuring — PSRAM is noticeably slower and more power-hungry on this part.
- **Logging**: use `ESP_LOGx(TAG, ...)`; entry-point `ESP_LOGV(TAG, "Func: %s", __func__)` is the house style.
- **Bluetooth is Classic SPP, not BLE**, for compatibility with the existing ELOC Control Panel Android app. Don't migrate to BLE without coordinating an app change.
- **APLL is unreliable below 16 kHz** on this ESP32; sample rates under 16 kHz fall back to PLL_D2 and are not recommended.
- **Sample rate, mic volume, and other operational parameters are overridden by the SD/SPIFFS config at runtime** — defaults in `project_config.h` are only the fallback. Don't assume the compiled value is what's running on a given device.
