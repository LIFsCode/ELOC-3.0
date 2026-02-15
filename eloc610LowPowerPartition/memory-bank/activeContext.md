# Active Context

## Current Work Focus

The project is in an active development phase with the core recording, AI inference, LoRaWAN, and Bluetooth subsystems all operational. Recent work has focused on:

1. **LoRaWAN session persistence** — RadioLib 7.2.1 upgrade with full session (RTC) and nonces (NVS) persistence to eliminate DevNonce exhaustion issues
2. **AI inference configuration** — Configurable detection threshold, observation window, and required detections count for more nuanced event triggering
3. **Microphone support expansion** — Added PUI DMM-4026-B-I2S-R microphone support with DC offset filtering
4. **Deferred AI startup** — AI thread start deferred by 3 seconds after BT command to allow status queries to complete

## Recent Changes

- **RadioLib 7.2.1 upgrade** with dual-storage persistence (RTC + NVS) for LoRaWAN
- **GPIO ISR service** installed earlier in initialization to avoid conflicts with LoRa and other interrupt sources
- **Inference configuration** added: threshold, observation window, required detections — allowing "N detections in M seconds" logic
- **DMM-4026-B-I2S-R microphone** driver support with DC offset removal filter
- **Conservative LoRa rejoin** strategy with 10-minute minimum interval
- **Audio feedback** for LoRa join events (buzzer)

## Active Decisions and Considerations

- **Bluetooth Classic vs BLE:** Currently using BT Classic SPP for compatibility with the existing ELOC Control Panel Android app. Migration to BLE is desired for power savings but requires app changes.
- **PSRAM for AI buffers:** AI inference buffers stored in PSRAM (`EI_BUFFER_IN_PSRAM`) to save internal RAM. WAV and I2S buffers kept in internal RAM for speed.
- **Continuous vs non-continuous inference:** Both modes supported but continuous inference is currently disabled by default (`AI_CONTINUOUS_INFERENCE` not defined).
- **Automatic gain adjustment:** Feature exists but is disabled due to causing distortion — needs fixing.
- **Light sleep effectiveness:** Light sleep is configured but the I2S driver's APB_FREQ_MAX lock may prevent actual sleep during recording.

## Important Patterns and Preferences

- **Library isolation:** Each hardware subsystem is in its own PlatformIO library under `/lib` — do not add external PlatformIO dependencies from `/lib` (causes build errors)
- **Config file priority:** SD card config overrides SPIFFS config overrides code defaults
- **Thread safety:** Shared variables between tasks (wav_writer, ai_run_enable, etc.) should be guarded with mutexes (noted as TODO in codebase)
- **Logging conventions:** Use `ESP_LOGx(TAG, ...)` macros with function-level `ESP_LOGV(TAG, "Func: %s", __func__)` at entry points
- **Build flag toggling:** Feature flags (AI, perf monitor, UART test, etc.) controlled via `#define` in `project_config.h`

## Next Steps

- Fix automatic gain adjustment distortion issue
- Investigate and verify light sleep effectiveness during idle periods
- Consider BLE migration path for power savings
- Add mutex guards to shared task variables
- Improve error recovery for SD card hot-swap scenarios
- Expand unit test coverage for LoRa persistence and AI detection logic

## Learnings and Project Insights

- **MicVolume2_pwr gain mechanism fully documented** in systemPatterns.md — formula, defaults per mic, and automatic gain bug identified (uses `>>` / `<<` instead of `±1`). Key reference for future microphone feature work.
- ESP32 APLL is unreliable below 16 kHz sample rate — falls back to PLL_D2
- RadioLib session restoration must load nonces ONLY from NVS (not RTC) to prevent DevNonce regression
- Edge Impulse SDK requires manual patches for some ESP32 compilation warnings-as-errors
- Double buffering buffer sizes should be multiples of 512 bytes (SD card block size) for optimal write performance
- The high-power partition must be present for this firmware to function — it handles initial BT bootstrap
