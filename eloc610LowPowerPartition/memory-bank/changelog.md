# Changelog (moved out of activeContext.md)

Dated history of completed work, newest first. Entries are moved here verbatim from
`activeContext.md` → 'Recent Changes' once a work stream is finished, so activeContext stays
focused on current work. The two newest/in-flight entries always stay in activeContext.

- **exFAT SD card support** (2026-07-06). SD cards may now be FAT32 **or exFAT** (cards >32 GB
  ship factory-formatted as exFAT; previously they silently failed to mount and had to be
  reformatted in the field). Root cause of the old restriction: IDF 4.4.7's bundled FatFs R0.13c
  hardcodes `FF_FS_EXFAT 0` with no Kconfig switch. Fix: new pre-build script
  `tools/patch_fatfs_exfat.py` (first `extra_scripts` entry in `platformio.ini`) patches the
  packaged `ffconf.h` to `FF_FS_EXFAT 1` — idempotent, self-heals after package reinstall, fails
  loudly if the FatFs revision changes (`FFCONF_DEF != 86604`; on IDF ≥5.1 use
  `CONFIG_FATFS_USE_EXFAT=y` instead). Alongside: `SDCardSDIO::updateFreeSpace()` 32-bit overflow
  fix (`fre_clust * csize` now widened before multiply — mattered for large exFAT cards),
  filesystem type logged after mount (`Filesystem: FAT32/exFAT`), clearer mount-failure messages
  in both `SDCardSDIO.cpp` and `SDCard.cpp`. Details in `README-exFAT-Support-Plan.md`.
  Build-verified; **hardware verification pending** (FAT32 regression, exFAT ≥64 GB card,
  duty-cycle remount).

- **Automatic mode-based CPU frequency profiles** (2026-07-05). Selecting a recording mode now
  selects the CPU frequency automatically; the stored `cpu*` config is untouched and remains the
  fallback:
  - **AI modes** (`recordOn_detectOn`, `recordOff_detectOn`, `recordOnEvent`) → fixed **240 MHz**
    (min = max, DFS off). **Recording-only** (`recordOn_detectOff`, no LoRa) → **min 10 / max 80 MHz**
    (min 20 if sample rate ≥ 30 kHz, issue #30). **No mode / record-only with LoRa** → configured
    values (LoRa still forces min = max, light sleep off).
  - Implementation: `ElocSystem::PmProfile` + `pm_requestProfile()` (`ElocSystem.hpp/.cpp`);
    `pm_configure()` is now profile-aware. The main loop calls
    `updatePmProfileFromRecordingMode()` (`main.cpp`) every iteration, which derives the profile
    from `ai_run_enable` + `wav_writer` mode — so BT commands, the physical button, and duty-cycle
    restores are all covered by one path.
  - **BT PLL constraint honoured**: a profile change while BT is up is deferred (switching between
    the 320 MHz and 480 MHz PLL relocks the BBPLL the BT radio runs from). It is applied in
    `disableBluetooth()` right after `SerialBT.end()` (`BluetoothServer.cpp` →
    `ElocSystem::setBluetoothActive(false)`); `enableBluetooth()` marks BT active *before*
    controller bring-up. Consequence: with `bluetoothEnableDuringRecord=true` (default) the switch
    happens only after the BT-off timeout (default 360 s without connection); with `=false` it
    happens seconds after recording starts. If BT never shuts down, the device stays at the
    boot-time frequency (physics, not a bug).
  - **Duty-cycle timer wakes** apply the profile at boot (before LoRa init) from
    `rtc_duty_cycle.aiEnabled`/`recordMode` (`main.cpp`, at the `pm_configure()` call site).
  - **Console UART moved to REF_TICK** (`configureConsoleUartRefTick()` in `main.cpp`, called
    early in setup before `pm_configure()`). Bench-observed: after the switch to the
    RECORDING_LOW_POWER profile the DFS drop of the APB clock to ~10 MHz garbled all ESP_LOG
    output (baud divisor is APB-derived). REF_TICK is a fixed 1 MHz reference DFS doesn't touch —
    the exact fix `ElocGPS::init()` already uses for the GPS UART (so GPS/recording were never
    affected, only the console). Cosmetic (no field console) but keeps bench logs readable.
  - See the new section in `README-Config-Restart-Semantics.md`. Remaining to hardware-verify:
    AI inference runs at the boot frequency (80 MHz default) until BT drops — DSP was measured
    615-735 ms at the old settings, so the 1 s slice budget still holds during that window.

- **Internal-heap headroom fix: BLE memory release + EI allocator PSRAM fallback** (2026-07-05,
  V1.44). Addresses the known borderline-heap issue with BT + LoRa + GPS + AI running concurrently
  (`MFE failed (-1002 = EIDSP_OUT_OF_MEM)` → `run_classifier` -5, and Bluedroid
  `BT_SDP: SDP - no buf for search rsp` making app connections fail during detection).
  - **BLE controller memory released at boot** (`main.cpp`, before `BluetoothServerSetup()`):
    `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` returns the never-used BLE share of the BT
    controller DRAM reserve (~30 KB) to the internal heap. The build is Classic-SPP-only
    (`CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY`), and neither the app nor Arduino's `btStart()` ever
    released it. Irreversible until reboot (fine — BLE is never used); safe across the runtime
    BT stop/start cycle. Hardware-verified: with BT + LoRa up, internal free ≈ 98 KB and the SDP
    no-buf failures are gone.
  - **`ei_malloc`/`ei_calloc`/`ei_free` strong overrides** in `src/ei_porting_overrides.cpp`:
    allocations ≥ 8 KB (`PSRAM_FIRST_THRESHOLD`) go PSRAM-first, smaller (hot per-frame FFT)
    buffers stay internal-first; both fall back to the other heap instead of failing. The stock
    espressif port uses plain `malloc()`, which with `CONFIG_SPIRAM_USE_CAPS_ALLOC` can never
    reach PSRAM. A pure internal-first version was tried first and re-broke BT: the MFE matrices
    consumed internal heap down to <4 KB contiguous / 70 % fragmentation and app connections
    failed again with `SDP - no buf for search rsp`.
    **⚠️ The file must stay in `src/`, NOT `lib/edge-impulse/`** — lib sources are linked as a
    static archive and the linker keeps the SDK's weak `ei_malloc` without ever extracting an
    override member from the same archive (verified with `xtensa-esp32-elf-nm`: symbols stayed
    `W`). Objects from `src/` are always on the link line, so the strong definitions win.
  - **Heap instrumentation**: the previously `if (0)`-disabled periodic heap log in `main.cpp` is
    now gated by `ENABLE_HEAP_MONITOR` (`project_config.h`, currently on) — logs internal + PSRAM
    min-free/free/largest-block/fragmentation every ~10 s with the battery line.
  - **Hardware-validated (V1.44 final, 2026-07-05):** boot with BT+LoRa: free ≈ 98 KB. With
    recording + AI + BT all active: internal min-free stays ≥ 20.4 KB, largest block 18-29 KB,
    fragmentation ~25 %; ~31 KB of DSP matrices resident in PSRAM; DSP time unchanged
    (615-735 ms, within the 1 s budget). Zero -1002 / classifier -5 / SDP no-buf across the run,
    including knock → BT enable → app connect during active detection.
  - **Known residual (accepted):** app connections during detection are slow — not memory, CPU:
    the whole BT stack shares core 1 with `ei_thread` (~85-90 % busy during inference), so the
    multi-round connect handshake gets stretched (DSP visibly rises ~618 → ~730 ms while BT is
    active). Possible future lever: pin Bluedroid host tasks to core 0
    (`CONFIG_BT_BLUEDROID_PINNED_TO_CORE`, core 0 has ~47 % idle) — needs an audio-dropout test.
  - Related open TODO (1-in-20 `ei_thread` first-inference panic) likely shares this root cause;
    re-test after this fix.

- **Intruder alarm over LoRa with GPS tracking** (2026-07-04, V1.43). The knock-based intruder
  detection (LIS3DH click-interrupt counting in `ElocSystem::notifyStatusRefresh()`) previously only
  beeped the buzzer. Now, while the alarm is active, the device reports over LoRa so a stolen/moved
  unit can be tracked:
  - **New LoRa uplink `INTRUDER_MSG` (msgType 2)**, 21 bytes: header, int64 epoch, flags (bit0 =
    hasFix), int32 lat×1e5 BE, int32 lng×1e5 BE, battery SoC, uint16 fix-age seconds (0xFFFF = no
    fix). Built in `ElocLora::sendIntruderAlarmMessage()`; decoder case added to
    `payload-formatters/radiolib-uplink-formatters.js` (**must be re-pasted into the TTN console
    uplink formatter**).
  - **Scheduling in `ElocLoraLoop()`**: first alarm sent immediately on the rising edge of
    `ElocSystem::isIntruderDetected()` (new getter), then every `intruderCfg.alarmIntervalS` (new
    config field, default 600 s, live-applied, clamped ≥ 60 s); failed uplinks retry after 60 s.
  - **GPS hold**: `manageGpsWhileAwake()` keeps the GPS powered continuously while the alarm is
    active (overrides burst power-down) so each alarm carries a fresh position.
  - **Sleep block**: `handleSleepCycleStateMachine()` refuses duty-cycle deep sleep while the alarm
    is active (a knock does NOT wake from deep sleep — only the timer and the button do).
  - **Stale-alarm fix**: `handleSystemStatus()` now clears an active alarm when `intruderCfg.enable`
    is switched off (previously the flag/buzzer stayed latched until the next knock event).
  - **Backend (ELOC_management)**: `TnnType.intruder`, msgType-2 handling in the TTN webhook, new
    Firestore path `the_things_network/intruder/{deviceId}`, `lastIntruderAlert`/`lastIntruderSeen`
    in `device_status_cache`, and an `alert_history` entry labelled `intruder` (with lat/lng).
    Frontend UI for the alert is still a follow-up.
  - **EXT1 knock wake from deep sleep: tried and REVERTED same day (hardware test failed).** An
    EXT1 wake on `LIS3DH_INT_PIN` was implemented (full-boot path + auto-return-to-sleep) but field
    testing showed: (a) the buzzer's BT-connect beep vibrates the PCB and false-triggers the knock
    sensor — every app connection raised the alarm, and the alarm's own beeping re-triggered it in
    a self-sustaining loop; (b) after a knock wake the device never re-entered sleep (likely the
    false alarm + sleep-block); (c) the knock-wake full boot brings up BT+LoRa+GPS simultaneously
    and, once AI starts, the Edge Impulse MFE/DSP mallocs fail intermittently with
    `EIDSP_OUT_OF_MEM` (-1002) / classifier error -5 — internal heap is too tight for that
    combination. Decision (with EDsteve): **intruder detection is a continuous-operation (24/7)
    feature only.** All EXT1/`gIsKnockWake` code was removed again.
  - **Fixes that came out of the field test (kept):**
    - **Buzzer→knock-sensor guard**: `notifyStatusRefresh()` ignores accelerometer clicks while
      the buzzer is active and for `C_BUZZER_KNOCK_GUARD_MS` (1 s) after it stops
      (`mLastBuzzerStopMs` set in `setBuzzerIdle()`). Fixes the false alarm on every app connect
      and the alarm self-sustain loop. The buzzer and LIS3DH share the PCB — any future buzzer
      use must keep this in mind.
    - **Duty-cycle gating**: `notifyStatusRefresh()` and the stale-alarm clear in
      `handleSystemStatus()` treat intruder detection as disabled whenever
      `getDutyCycleConfig().enable` is set (simple config-level rule, agreed with EDsteve). The
      intruder sleep-block in `handleSleepCycleStateMachine()` was removed again — it is
      unreachable now that detection can't be active in duty-cycle mode.
  - **Open issue observed during the test (pre-existing, now tracked):** with BT + GPS + LoRa +
    AI running concurrently the internal heap is borderline — MFE failed (-1002 =
    `EIDSP_OUT_OF_MEM`) → `run_classifier` -5, intermittently. Likely related to the known
    1-in-20 `ei_thread` first-inference panic. Needs its own investigation (heap headroom audit /
    moving more buffers to PSRAM / freeing BT when not needed).

- **GPS burst power-saving while awake + time-before-fix timezone bug fixed + device time/GPS in
  `getStatus`** (2026-06-24, validated on hardware). Three related changes; the first targets continuous
  (non-duty-cycled) operation, where the GPS was previously left powered the whole time.
  - **Hourly burst power-saving** — new `manageGpsWhileAwake()` in `main.cpp`, called every main-loop
    iteration. The ATGM336H draws nearly as much as the ESP32 at a 16 kHz sample rate, so for 24/7
    recording (no deep sleep) it now runs in **bursts**: power up only long enough to get a fix, then
    `ElocGPS::deinit()` (gate VCC off) until `GPS_RESYNC_INTERVAL_S` (3600 s) elapses. Reuses the existing
    tested `init()`/`deinit()`. Cadence is tracked with the **monotonic `esp_timer`** (session-local
    statics), so it is **independent of the duty-cycle RTC `magic`/`lastGpsSyncS`** — works the same with
    or without an app `setTime`/duty-cycle config. A one-time *seed* honours the boot path's fresh-clock
    skip (a duty-cycle timer wake that left GPS off is not re-powered for the short awake window). New
    `GPS_FIRST_FIX_TIMEOUT_S` (180 s) gives a **cold** first fix more time than the warm-trim ceiling
    `GPS_TIME_SYNC_TIMEOUT_S` (30 s); "still cold" is detected by the RTC clock sitting at build-time
    (`getEpoch() < getBuildTimeSecs() + 1 h`). `GPS_RESYNC_INTERVAL_S = 0` disables bursting (GPS stays
    powered — old behaviour). The `GPS_RESYNC_INTERVAL_S` comment in `project_config.h` now documents both
    the timer-wake skip and the awake-burst roles.
  - **Time model clarification (drove the simplification):** the device is operated **only via the
    Android app**, so time is **not solely from GPS** — the app's `setTime` (phone epoch + TZ) is
    authoritative and the RTC holds it. GPS here is only a drift-trimmer, so a burst that fails to fix is
    harmless (keep current clock, retry next interval). That is why there is no "block until first fix"
    path in the awake manager.
  - **Timezone bug fixed — burst now ends on a LIVE POSITION fix, not a time sync.** GPS decodes UTC
    seconds a few seconds *before* it has a position solution, and the local TZ is derived from the fix
    **longitude**. The first version powered GPS off on the time sync, before any fix, so
    `applyGpsDerivedTimezone()` had no longitude and the TZ stayed at the compile-time `TIMEZONE_OFFSET`
    (= 7, Sumatra) — observed on hardware in Germany (showed UTC+7). Fix: complete the burst only when
    `hasFix() && getFixAgeMs() < 3 s` (new `ElocGPS::getFixAgeMs()`; the age check rejects a *stale* fix
    that lingers as "valid" in TinyGPS++ after a previous burst's power-down). UTC time still syncs in the
    background the moment it is valid, so the clock is never lost even if no fix follows. (Reminder: the
    derived offset is solar `round(longitude/15)` and ignores DST — Germany reads +1, not +2; app-set TZ
    overrides. UTC epoch / recorded timestamps are always correct regardless of the display offset.)
  - **`getStatus` now reports device time + GPS** (completes the older "surface lat/lon in getStatus"
    follow-up): `device.time` (local string) + `device.epoch`, and a new `gps` section
    (`present`/`powered`/`hasFix`/`satellites`/`timeSynced`, plus `lat`/`lon` when fixed). The Android app
    surfaces these as a **Device Time** row and a **GPS** row (Idle/Searching/Fix) on the status page
    (separate ELOC-Control-Panel repo, `main`). Status JSON pool bumped 768→1024. Builds clean on
    `esp32dev`. Note: with bursting, the GPS row reads "Idle" most of the time **by design** (module
    powered down between bursts).
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
