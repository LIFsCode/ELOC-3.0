# Active Context

## Current Work Focus

**Firmware update over Bluetooth (app-driven OTA) — Phases 0–2 implemented, hardware verification
in progress.** Current firmware V1.51 (2026-07-10 review fixes) + app MVP shipped in the same cycle.
Plan doc: `README-FirmwareUpdate-From-App-Plan.md`. What is still owed before field rollout:

1. **Hardware verification matrix** — SD-swap regression, power-pull during flash, crashing-image
   rollback, BT transfer + mid-transfer drop + resume (incl. fully-staged resume), preflight refusals.
2. **Partition0 ground truth on deployed devices** — gates the first field OTA.
3. **Phase 3 (distribution)** — Firestore `firmware_releases` manifest, `tools/make_release.py`,
   "update available" UX, result upload; designed in the plan, not started.

**Standing open bug:** the 1-in-20 `ei_thread` first-inference panic (see progress.md → Known Issues).
The V1.44 heap-headroom fix likely addressed its root cause — re-test on the suspect unit; planned
diagnostics (coredump-to-flash partition, `ei_thread` stack high-water logging) are still unimplemented.

Completed earlier in this cycle (details in `changelog.md`): exFAT SD support (V1.46), mode-based CPU
frequency profiles (V1.45), internal-heap headroom fix (V1.44), intruder alarm over LoRa (V1.43),
GPS integration + burst power-saving (June 2026), duty-cycle deep sleep + 24h LoRa heartbeat (May 2026).

## Recent Changes

- **BT firmware update review fixes** (2026-07-10, V1.51; app changes in the same cycle):
  - **Fully-staged resume deadlock fixed.** Previously, when a resume found the staged file
    already complete (`resumeOffset == size`, e.g. the final "staged" ack was lost in a
    disconnect right at the end), `FwUpdateTransfer::begin()` still entered binary mode waiting
    for frames the app would never send; the app's retry loop then fed `setFwUpdateBegin` into
    the frame parser as garbage bytes — permanent, unrecoverable failure (only manual SD access
    cleared it). Now `begin()` skips binary mode in that case (state → STAGED),
    `cmd_SetFwUpdateBegin` only hooks the raw data sink when actually receiving, and the Begin
    response carries `state: "receiving"|"staged"`. App side: on `state=="staged"` it proceeds
    straight to apply; against older V1.47–V1.50 firmware (`resumeOffset >= totalBytes`, no
    `state` field) it sends the len==0 sentinel to exit binary mode cleanly
    (`FirmwareUpdater.finishFullyStagedResume()`).
  - **Project-name guard in `validateImageFile()`**: refuses images whose
    `esp_app_desc_t.project_name` differs from the running image's (compared dynamically via
    `esp_ota_get_app_description()`, nothing hardcoded). Rationale: rollback only reverts
    *crashing* images — a stably running foreign image would leave a field device unreachable
    over BT. Applies to the manual SD-swap path too; a project rename would block updates across
    the rename.
  - App also now resolves the picked file's name via `OpenableColumns.DISPLAY_NAME`
    (`Uri.lastPathSegment` is an opaque `msf:<id>` for the Downloads provider, which silently
    disabled the ei/no-ai variant guard).
  - Known-but-deferred from the 2026-07-10 review (low practical risk): app sets its
    `firmwareTransferActive` gate only after the Begin response (small window where another
    component's queued command could corrupt the frame stream); `DeviceDriver`'s internal 15 s
    command timeout undercuts the updater's 20/30 s waits; flash failure and genuine rollback
    are both reported as "rolled back"; `result.json` is written but never read by the app.

- **Firmware update over Bluetooth — Phases 0+1** (2026-07-06, V1.47). Full plan in
  `README-FirmwareUpdate-From-App-Plan.md`; app counterpart shipped in the same cycle
  (ELOC-Control-Panel `FirmwareUpdater`/`FirmwareUpdateActivity`).
  - **Phase 0 — SD updater rewritten** (`lib/FirmwareUpdate/FirmwareUpdate.cpp`): fixed the
    NULL-partition/`fclose(NULL)`/ignored-`esp_ota_write` bugs; removed the fragile compile-date
    comparison (downgrades allowed — device only verifies integrity: image magic + app descriptor
    + size ≤ inactive slot); optional SHA-256 check from `/sdcard/eloc/update/elocupdate.json`
    (written by the BT path, optional for manual SD-swap); writes
    `/sdcard/eloc/update/result.json` (fromVersion/toVersion/outcome/timestamp); deletes staged
    files on success. **OTA rollback enabled** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in
    `sdkconfig.defaults` — generated sdkconfigs were regenerated); `markRunningFirmwareValid()`
    in `main.cpp` (after BT server start, non-timer boot) confirms the image, else the bootloader
    reverts. Rollback only protects updates *from V1.47 onward* and needs the new bootloader
    (existing devices keep their old bootloader after OTA → degrade gracefully, no rollback).
  - **Phase 1 — BT transfer protocol**: `setFwUpdateBegin#meta={size,sha256,version,variant,
    chunkSize}` (preflight: recording+AI off, SD mounted, free space ≥ remaining+1 MB, battery
    ≥ 3.1 V LiFePo / 3.5 V LiPo / skip if BAT_NONE, size ≤ slot; replies
    `{resumeOffset,chunkSize}`), then raw binary frames `[seq:u16 LE][len:u16 LE][payload]
    [crc32:u32 LE]` (CRC over seq+len+payload, IEEE = java CRC32; len==0 is the end-of-stream
    sentinel), stop-and-wait with per-frame EOT-JSON acks (`cmd:"fwFrame"`), then
    `getFwUpdateStatus` / `setFwUpdateAbort#discard=` / `setFwUpdateApply` (re-hashes staged
    file, writes metadata + `doUpdate.txt`, restarts).
  - **Binary RX path**: the stock BluetoothSerial RX queue is 512 bytes and silently drops on
    overflow, so binary mode re-routes via `SerialBT.onData()` into a FreeRTOS stream buffer
    (`FwUpdateTransfer`, buffers allocated per-transfer, payload in PSRAM);
    `wait_for_bt_command()` services it and bypasses `CmdParser`. Any error/timeout (30 s)/
    disconnect exits binary mode and keeps the partial + `transfer.json` for resume.
  - **Capability advertisement** in `getStatus` `device`: `fwUpdateProto:1`,
    `buildVariant:"ei"|"no-ai"`, `otaSlotSize` (status JSON doc grown to 1536).
  - New files: `lib/FirmwareUpdate/src/FwFrameParser.hpp` (pure header, native-tested in
    `test/test_generic_fw_frame_parser/` — needs a host gcc, not present on the dev machine),
    `FwUpdateTransfer.{hpp,cpp}`. Both `esp32dev` and `esp32dev-ei` build-verified.
  - **HW test 2026-07-06: SD-swap flash of V1.47 succeeded and booted; first `getStatus` from the
    app then double-exception panic'd (stack overflow) on the "BT Server" task.** Root cause: the
    task had a 4 KB stack and `printStatus()` serializes a big `StaticJsonDocument` on it; growing
    that doc 1024→1536 for the new `fwUpdateProto`/`buildVariant`/`otaSlotSize` fields, combined
    with the deep FatFS `vfs_fat_stat` path taken when the SD log rotates mid-getStatus, overran
    it. Fix: raised the BT Server task stack to 8 KB in `BluetoothServer.cpp`
    (`xTaskCreate(wakeup_task,...)`) — the 4 KB stack was already marginal (log-rotation getStatus
    exceeded it), so this is a root-cause fix, not just masking my +512 B. **Re-test on hardware
    still owed**, plus: power-pull during flash, rollback of a crashing image, BT transfer +
    resume, battery/recording refusals.

- **Older completed entries have moved to `changelog.md`** (same folder): exFAT SD support
  (2026-07-06, V1.46), automatic mode-based CPU frequency profiles (2026-07-05, V1.45),
  internal-heap headroom fix (2026-07-05, V1.44), intruder alarm over LoRa (2026-07-04, V1.43),
  GPS burst power-saving + timezone fixes (June 2026), GPS bring-up + power-gate polarity,
  duty-cycle record-ON support, LoRa RSSI in getStatus, duty-cycle Phase 1.

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

### High Priority
- **Hardware-verify the BT firmware update matrix** (see Current Work Focus) — gates field rollout.
- **1-in-20 first-inference panic**: add coredump-to-flash + `ei_thread` stack high-water logging;
  re-test the suspect unit on ≥ V1.44.
- **LoRa Event Cooldown (Phase 2)** — Implement event start/ongoing/end state machine to reduce daily LoRa messages from hundreds to ~10-15. See `README-DutyCycle-and-LoRa-Cooldown.md` Section 3. Note: the `lorawan.eventCooldownS`/`eventEndTimeoutS` config fields already exist but are **dead** (defined + merged, never read) — wire them up or remove them as part of this.

### Medium Priority
- Firmware-update Phase 3 (Firestore release manifest + app "update available" UX).
- Fix automatic gain adjustment distortion issue
- Investigate and verify light sleep effectiveness during idle periods
- Add mutex guards to shared task variables
- Deferred minor findings from the 2026-07-10 fw-update review (listed under Recent Changes).

### Lower Priority
- Consider BLE migration path — assessed 2026-07-05: not worth it now; forced only if moving to an
  ESP32-S3/C-series part (no Classic BT) or if an iOS app becomes required. Throughput would drop
  vs SPP, which hurts the BT firmware-update feature.
- Pin Bluedroid host tasks to core 0 (`CONFIG_BT_BLUEDROID_PINNED_TO_CORE`) to speed up app connects
  during active AI — needs an audio-dropout test first.
- Improve error recovery for SD card hot-swap scenarios
- Expand unit test coverage for LoRa persistence and AI detection logic

## Learnings and Project Insights

- **MicVolume2_pwr gain mechanism fully documented** in systemPatterns.md — formula, defaults per mic, and automatic gain bug identified (uses `>>` / `<<` instead of `±1`). Key reference for future microphone feature work.
- ESP32 APLL is unreliable below 16 kHz sample rate — falls back to PLL_D2
- RadioLib session restoration must load nonces ONLY from NVS (not RTC) to prevent DevNonce regression
- Edge Impulse SDK requires manual patches for some ESP32 compilation warnings-as-errors
- Double buffering buffer sizes should be multiples of 512 bytes (SD card block size) for optimal write performance
- The high-power partition must be present for this firmware to function — it handles initial BT bootstrap
- **Weak-symbol overrides (e.g. `ei_malloc`) must live in `src/`, never in a `lib/` static archive** — the linker never extracts an override member from the same archive as the weak definition (verify with `nm`: want `T`, not `W`)
- **CPU frequency must not be changed while Bluetooth is up** — switching between the 320/480 MHz PLLs relocks the BBPLL the BT radio is clocked from (reboot loop). Apply at boot or right after `SerialBT.end()`.
- **The buzzer and the LIS3DH share the PCB** — buzzer vibration registers as knocks; any knock/click feature needs the buzzer→knock guard (`C_BUZZER_KNOCK_GUARD_MS`)
- **The stock BluetoothSerial RX queue is 512 B and silently drops on overflow** — bulk/binary RX must bypass it via `SerialBT.onData()` into a stream buffer
- **The exFAT patch (`tools/patch_fatfs_exfat.py`) edits the shared PlatformIO package** — a platform reinstall reverts it until the next `pio run` re-applies it
