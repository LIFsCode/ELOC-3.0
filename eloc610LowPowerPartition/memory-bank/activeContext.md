# Active Context

## Current Work Focus

**Duty-Cycle Deep Sleep (Phase 1) — COMPLETE.** The device can now cycle between 5-minute deep sleep and 30-second active AI inference, providing ~10-15× battery life extension. Next focus is Phase 2: LoRa Event Cooldown to reduce daily LoRa messages from hundreds to ~10-15.

**24h Heartbeat for Patrol Mode — COMPLETE.** Periodic LoRa status uplinks now use wall-clock epoch time persisted in RTC memory (`lastStatusLoraTimeS` in `rtc_duty_cycle_t`), ensuring the heartbeat interval is honoured across duty-cycle deep sleep cycles. Default uplink interval changed from 1 hour to 24 hours.

Recent completed work:
1. **Duty-cycle deep sleep** — Timer-based wake/sleep cycling with configurable durations
2. **Fast boot path** — Skips Bluetooth, Battery, PerfMonitor on timer wake (~5s faster)
3. **RTC state persistence** — Boot count, total detections, session ID survive across sleep cycles
4. **Session continuity** — Same SD card folder and CSV file shared across all wake cycles
5. **Bug fixes** — totalDetections sync, session persistence, timezone handling, awake timer accuracy, LED turn-off

## Recent Changes

- **GPS time sync on every duty-cycle wake + GPS-longitude auto-timezone** (2026-06-05). Closes the gap
  where the ESP32 RTC drifted across deep sleep and was never corrected (GPS used to run only on
  non-timer-wake boots). Changes:
  - **GPS now runs on every boot path.** GPS init was moved *out* of the `!gIsTimerWake` block in
    `main.cpp` so it powers up on a timer wake too. The other fast-boot skips (Battery eager init, BT,
    PerfMonitor, UART) are unchanged. (Note: Battery is a lazy Meyers singleton, so it still works on a
    timer wake via the main-loop `updateVoltage()` / LoRa heartbeat `getSoC()` — only its *eager* setup +
    bundled firmware-update-file check are skipped.)
  - **Bounded blocking wait for the first fix on timer wake.** New `ElocGPS::waitForTimeSync(timeoutMs)`
    polls `mLastUtcEpoch` until the clock is corrected or the timeout elapses. Called right after
    `ElocGPS::init()` on a timer wake, **before** the awake-duration timer is reset, so the wait does not
    shorten the inference window (it does extend awake time / power per cycle). Timeout is
    `GPS_TIME_SYNC_TIMEOUT_S` in `project_config.h` (default **30 s**; set 0 to start GPS without blocking
    and correct opportunistically). The module's **VBAT is tied to +3V3**, so it keeps RTC + ephemeris
    across the VCC gating → every wake *after the first cold acquisition* is a **warm/hot start**, fix in a
    few seconds (the 30 s is a cold-start/poor-sky ceiling, not the expected warm-wake wait). Earlier
    "cold start *each* wake" comments were wrong — only the very first boot after a full power-down is cold.
  - **First time-sync is now immediate.** `ElocGPS::gpsTask()` previously only synced every
    `GPS_LOG_INTERVAL_S` (30 s); it now attempts a sync on every read pass *until the first success*, then
    falls back to the periodic cadence for drift correction. `syncTimeFromGps()` returns silently on
    invalid UTC, so pre-lock attempts cost nothing and don't spam the log. Log cadence is unchanged.
  - **Auto-timezone from GPS longitude.** New `applyGpsDerivedTimezone()` in `main.cpp` sets the
    local-display offset to `round(longitude / 15)` so a device self-localises in any country.
    **Precedence: app-set TZ (RTC `timezoneOffsetValid`) > GPS-longitude > compile-time `TIMEZONE_OFFSET`.**
    Applied immediately after the timer-wake fix; on normal boot a one-shot in the main loop applies it
    when a background fix lands. **UTC stays the source of truth** — the RTC and all transmitted/stored
    epochs (LoRa, `rtc_duty_cycle` times) remain UTC; only human-readable strings / WAV+CSV filenames
    shift. Caveat by design: meridian offset ignores political borders and **DST** (fine for equatorial
    sites; app override covers the rest). This supersedes the older "CSV timestamps stay in BT-set TZ"
    entry below — that mechanism is now just the highest-priority tier of the chain.
  - **Resync gating — GPS is skipped on wakes where the clock is already fresh.** Re-acquiring GPS every
    2-minute cycle is wasted energy: the ESP32 RTC keeps time through deep sleep and **VBAT is hard-wired
    to the main LiFePO4 pack** (always ~3.3 V), so the GPS module's RTC/ephemeris never lose power. Added
    `GPS_RESYNC_INTERVAL_S` (project_config.h, default **3600 s**; 0 = every wake). On a timer wake, if the
    last sync was younger than that interval, GPS is **not powered on at all** this cycle. Three new
    `rtc_duty_cycle_t` fields (appended at the end so existing offsets are undisturbed): `lastGpsSyncS`
    (epoch of last sync), `gpsTimezoneOffset` + `gpsTimezoneValid` (the GPS-derived TZ, persisted so a
    skip-wake can still restore the right zone — the boot TZ restore precedence is now app-set >
    GPS-persisted > compile default). `lastGpsSyncS` is stamped from `main.cpp` via the existing
    `ElocGPS::lastUtcEpoch()` getter (blocking-success path + a main-loop poll that also catches a fix
    landing after a timeout); **deliberately not** stamped inside `ElocGPS.cpp` to avoid pulling
    `ElocStatus.hpp` (→ `WAVFileWriter.h`/`WString.h`) into the isolated `gps` lib. Guards (`lastGpsSyncS>0`,
    `ageS>=0`) make stale/garbage RTC data fall back to a safe re-sync. Net: cold-start timeout is a
    ~once-per-hour worst case instead of every cycle.
  - **Bring-up nuance (2026-06-05):** observed a 44 s fix on duty-cycle **boot #1** (cold start, expected —
    the standalone `Firmware/GPS-Test/` bench sketch also treats boot #1 as cold and gives it 2 min; its
    ~2 s fixes are *warm* wakes). With VBAT continuous, production **warm wakes** should also fix in ~2 s,
    so `waitForTimeSync()` returns in ~2 s on those (the 30 s only bites a true cold boot). If warm wakes
    are *also* slow, suspect on-board RF/digital interference (SX1262 + I2S + SDIO + 240 MHz CPU run
    concurrently with GPS in production; the bench sketch runs GPS alone) — the lever would be acquiring
    the fix before spinning up LoRa/AI, or a better antenna.
  - GPS deinit before sleep was already unconditional, so the per-wake GPS is still cleanly powered down
    (and `deinit()` is a no-op on skip-wakes where it was never inited). Builds clean on `esp32dev`.
- **GPS power gate is ACTIVE-LOW, and a clean power-down path was added** — corrects an inverted-polarity
  bug discovered during hardware bring-up. The `GPS_VCC_EN` net (IO expander **IO5**) drives the gate of
  a **P-channel high-side MOSFET (AO3401A)**, with R12 (10 kΩ) pulling the gate to +3V3. So the logic is
  inverted vs. the old code's assumption: **IO5 LOW = GPS ON, IO5 HIGH = GPS OFF**. Fixes:
  - `ELOC_IOEXP::setGpsPower(bool)` now drives `!enable` to the gate; `init()` sets IO5 **high** for the
    default-off state (was clearing it low, which silently powered the GPS on the whole time). Header +
    `project_config.h` comments corrected (were "high = ON").
  - New **`ElocGPS::deinit()`** powers down cleanly: stop reader task → `uart_driver_delete` → **drive
    PIN_GPS_TX (GPIO4) LOW** → `setGpsPower(false)`. (Driving TX low before cutting VCC is good practice —
    avoids holding a powered-down chip's input high — though it was *not* the cause of the 2.6 V; see the
    root-cause note below.) Destructor now routes through `deinit()`.
  - **Wired into the duty cycle:** `enterCyclicDeepSleep()` in `main.cpp` calls `ElocGPS::deinit()` (under
    `#ifdef USE_GPS`) before `esp_deep_sleep_start()`. **Caveat:** GPS init lives in the `!gIsTimerWake`
    block, so on a timer wake the GPS is never inited and `deinit()` is a no-op — therefore the sleep path
    *also* calls `getIoExpander().setGpsPower(false)` unconditionally to force IO5 high on every boot path
    (the PCA9557 retains its output state across deep sleep, but this no longer relies on that).
    **UPDATE (2026-06-05):** GPS now *does* run per-wake (init moved out of the `!gIsTimerWake` block —
    see the GPS-time-sync entry at the top); the unconditional `setGpsPower(false)` in the sleep path is
    kept anyway as a belt-and-suspenders guarantee.
  - **ROOT CAUSE of the 2.6 V / ~25 µA-in-sleep was a HARDWARE fault, now fixed by board rework:** the
    AO3401A's **drain and source were swapped** on the PCB vs. the schematic. With source/drain reversed
    the MOSFET's body diode is forward-biased from +3V3 → GPS VCC, so it conducts regardless of the gate,
    clamping VCC at ~V_rail − 0.6 ≈ 2.6 V and leaking ~25 µA — the switch could never turn off. Earlier
    "phantom power via VBAT / RXD clamp" theories were red herrings (VBAT measured 0.2 V, disproving the
    VBAT path). **The firmware polarity fix is still correct** and is validated by this: the *schematic*
    (and therefore the reworked board) is a P-channel high-side active-low switch, which is exactly what
    the code now drives. Do **not** revert it.
  - **Separate hardware defects noted during bring-up (lower priority):** VBAT (pin 6) reads ~0.2 V — it
    is not getting its +3V3 (suspected net miswire in the +3V3/VBAT/C26 corner); and **C26 is not grounded**
    (decoupling cap with a floating ground pad — does nothing, but can't itself cause DC leakage). Also note
    **VCC_RF (pin 14)** shares the switched rail (RF/LNA bias via L2 to the active antenna). Verify these
    against the PCB; they did not cause the sleep current but should be corrected.
- **Buzzer no longer drones during LoRa uplinks** — `EasyBuzzer` is non-blocking; its tone is only
  switched off by `EasyBuzzer.update()`, which is pumped once per cycle in
  `ElocSystem::handleSystemStatus()`. That same cycle ends by running `ElocLora::ElocLoraLoop()`, and
  `node.sendReceive()` blocks for the full airtime + RX1/RX2 windows (seconds at AS923 SF10-SF12), so a
  beep started earlier in the cycle kept sounding in PWM hardware for the whole transmit. The classic
  symptom was the "Bluetooth ready" notification beep colliding with the immediate first heartbeat
  (`lastStatusLoraTimeS == 0`) at boot. Fixed by calling `EasyBuzzer.stopBeep()` at the top of
  `ElocLora::sendReceiveWithRecovery()` — the single blocking choke point for both heartbeat and event
  uplinks — so no uplink can ever leave a tone droning. (Direct-`EasyBuzzer` use mirrors `playJoinFeedback()`.)
- **GPS support added (ATGM336H GNSS, bring-up / test phase)** — new `lib/gps` library exposing the
  `ElocGPS` singleton. Reads NMEA over **UART_NUM_1 (RX=GPIO36, TX=GPIO4, 9600 baud)**, parses with
  **TinyGPS++** (`mikalhart/TinyGPSPlus`, added to `platformio.ini` `[env].lib_deps`), and a dedicated
  FreeRTOS task (Core 0, prio 2, ~3 KB stack) logs position + time to serial every `GPS_LOG_INTERVAL_S`
  (30 s) and syncs the system clock (`timeObject`) from GPS UTC. Module **VCC is gated by IO expander
  IO5** via a P-channel high-side MOSFET — **active-LOW** (IO5 LOW = ON; see the polarity-fix entry under
  "Recent Changes"). `ELOC_IOEXP::GPS_VCC_EN` (= IO5) is configured as output in `init()` (default OFF =
  IO5 high) with `setGpsPower(bool)`; `ElocGPS::init()` powers it on. **(GPS init was originally gated to
  `!gIsTimerWake`; as of 2026-06-05 it runs on every boot path — see the GPS-time-sync entry at the top.)**
  - **GPIO4 freed for GPS TX:** `STATUS_LED`/`BATTERY_LED` were *both* `#define`d to GPIO4 but the
    physical LEDs are on the PCA9557 IO expander, so the direct `gpio_set_level/direction(STATUS_LED…)`
    calls in `main.cpp` (boot + `prepareCyclicDeepSleep`) and the firmware-update success blink in
    `FirmwareUpdate.cpp` were vestigial. Removed/redirected to the IO expander so the UART can own GPIO4.
  - UTC→epoch uses a local `utc_tm_to_epoch()` helper (Hinnant days-from-civil) — `timegm()` is **not**
    available in ESP-IDF newlib. GPS time is set as absolute UTC epoch; the configured TZ offset is left
    untouched. Note: `ESP32Time::setTime(epoch, ms)` clamps the sub-second arg to 0..999, so pass 0.
  - Builds clean on `esp32dev-ei` (RAM 30.4% / Flash 20.7% static). **Hardware bring-up in progress** —
    polarity + phantom-power issues found and fixed (see above). Power-aware GPS in duty cycle (turn IO5
    off + drive TX low before sleep) is now **done** via `ElocGPS::deinit()`. Surfacing lat/lon in
    `getStatus`/LoRa remains a follow-up. `USE_GPS` is now **enabled** in `project_config.h` (validated on
    hardware 2026-06-05: GPS time sync + auto-timezone working).
- **Duty-cycle deep sleep now works in the record-ON modes** (tested on device 2026-05-29), not just AI-only patrol mode. Previously the sleep state machine was gated to `recordOFF_detectON` only (the activation lived inside the AI-enabling branch of `cmd_SetRecordMode`). It now activates for `recordON_detectON` and `recordON_detectOFF` as well. To make recording actually resume after a timer wake, two new fields were added to `rtc_duty_cycle_t`: `uint8_t recordMode` (the `WAVFileWriter::Mode` to restore) and `bool aiEnabled` (whether to auto-start AI). On wake, `main.cpp` restores `wav_writer` mode from RTC (the main loop's existing `mode==continuous` auto-start then begins a fresh WAV file — one file per wake) and only auto-starts AI when `aiEnabled`. `prepareCyclicDeepSleep()` now closes the open WAV file (sets mode disabled, waits for the write task's `finish()`) **before** stopping I2S, so the last file per cycle has a finalized header instead of being truncated. Files share the existing RTC-persisted session folder.
- **Bug 7 (SD free-space cache) fixed** — `WAVFileWriter`'s "SD Card is full" guard reads a cached `m_free_bytes` that was only ever refreshed by the Bluetooth status task (`sd_card.update()` in `BluetoothServer.cpp`). On a timer wake Bluetooth is skipped, so the cache stayed 0 and every record-ON wake aborted with a false "SD Card is full". Fixed by calling `sd_card.update()` once in `mountSDCard()` right after mount, so the value is valid on every boot path. This was a latent bug exposed only once recording resumed on wake. See `README-DutyCycle-BugFixes.md` Bug 7.
- **CSV timestamps now stay in the user's BT-set timezone across duty-cycle wakes** (Bug 6 in `README-DutyCycle-BugFixes.md`). Two `int8_t timezoneOffset` / `bool timezoneOffsetValid` fields added to `rtc_duty_cycle_t`; the BT `setTime` handler writes them, and the timer-wake path in `main.cpp` uses them in preference to the compile-time `TIMEZONE_OFFSET`. Fixes the 6-hour drift seen when a UTC+1 device runs `recordOFF_detectON` and falls back to the compile default of +7 after the first wake.
- **LoRa RSSI signal quality** exposed via `getStatus` Bluetooth command for deployment-time signal strength checking in Android app
  - `captureSignalQuality()` captures RSSI/SNR after OTAA join (Join-Accept), after rejoin, and after any downlink
  - New `lora` section in `getStatus` JSON: `enabled`, `joined`, `hasSignalInfo`, `RSSI[dBm]`, `SNR[dB]`
  - Public getters on `ElocLora`: `getLastRSSI()`, `getLastSNR()`, `hasSignalInfo()`, `isJoined()`
  - RSSI only available after fresh join (cold boot) or downlink — not after session restore from RTC
- **Duty-cycle deep sleep** (Phase 1) implemented and tested — see `README-DutyCycle-and-LoRa-Cooldown.md`
- **Bug fixes** documented in `README-DutyCycle-BugFixes.md`:
  - totalDetections now synced to RTC before sleep
  - Session ID persisted in RTC memory for folder continuity
  - Timezone restored on timer wake (TZ env var lost during deep sleep)
  - Awake timer reset after init (full 30s now available for inference)
  - LEDs turned off before sleep (IO expander retains register state)
- **LoRa delay skipped** on timer wake (saves 5s boot time)
- **LED animation skipped** on timer wake
- **Buzzer feedback skipped** on timer wake

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
- **LoRa Event Cooldown (Phase 2)** — Implement event start/ongoing/end state machine to reduce daily LoRa messages from hundreds to ~10-15. See `README-DutyCycle-and-LoRa-Cooldown.md` Section 3.

### Medium Priority
- Fix automatic gain adjustment distortion issue
- Investigate and verify light sleep effectiveness during idle periods
- Add mutex guards to shared task variables

### Lower Priority
- Consider BLE migration path for power savings
- Improve error recovery for SD card hot-swap scenarios
- Expand unit test coverage for LoRa persistence and AI detection logic

## Learnings and Project Insights

- **MicVolume2_pwr gain mechanism fully documented** in systemPatterns.md — formula, defaults per mic, and automatic gain bug identified (uses `>>` / `<<` instead of `±1`). Key reference for future microphone feature work.
- ESP32 APLL is unreliable below 16 kHz sample rate — falls back to PLL_D2
- RadioLib session restoration must load nonces ONLY from NVS (not RTC) to prevent DevNonce regression
- Edge Impulse SDK requires manual patches for some ESP32 compilation warnings-as-errors
- Double buffering buffer sizes should be multiples of 512 bytes (SD card block size) for optimal write performance
- The high-power partition must be present for this firmware to function — it handles initial BT bootstrap
