# Progress

## What Works

### Core Functionality — ✅ Operational
- **I2S audio capture** from MEMS microphones (ICS-43434, DMM-4026-B-I2S-R)
- **WAV file recording** to SD card with configurable sample rate, duration, and channel
- **Double-buffered writing** pipeline with FreeRTOS tasks on separate cores
- **Three recording modes:** disabled, single (triggered), continuous
- **SD card** mounting via SDIO with free space checking and session folder creation

### AI Inference — ✅ Operational
- **Edge Impulse integration** with TFLite Micro models
- **Both continuous and non-continuous** inference modes
- **Configurable detection:** threshold, observation window, required detections count
- **CSV logging** of inference results to SD card
- **AI-triggered recording:** detection can start WAV recording in single mode
- **Deferred AI startup** to prevent BT command timeouts

### LoRaWAN — ✅ Operational
- **SX1262 radio** via RadioLib 7.2.1
- **OTAA join** with TTN (The Things Network)
- **Status uplink** messages (battery, recording, AI stats)
- **Event uplink** messages on sound detection
- **Session persistence** via RTC memory (survives deep sleep)
- **DevNonce persistence** via NVS flash (survives power loss)
- **Conservative auto-rejoin** with 10-minute minimum interval
- **Payload formatters** for TTN (uplink & downlink)
- **RSSI/SNR signal quality** captured after join and downlinks, exposed via `getStatus` BT command for Android app deployment checking

### Bluetooth Control — ✅ Operational
- **BT Classic SPP** server for ELOC Control Panel Android app
- **Command protocol** via CmdParser for config, status, recording control, AI control
- **Configurable timeout,** auto-off, enable-on-tapping (accelerometer)
- **Runtime configuration** changes written to SD card/SPIFFS

### Power Management — ✅ Operational
- **Dynamic frequency scaling** (DFS) with configurable min/max
- **Light sleep** configuration (effectiveness during recording uncertain)
- **Deep sleep** with GPIO button wake-up
- **Battery monitoring** with configurable intervals and averaging

### 24h Heartbeat for Patrol Mode — ✅ Operational
- **RTC-persisted heartbeat timing** using wall-clock epoch time (`lastStatusLoraTimeS` in `rtc_duty_cycle_t`)
- **Default 24h interval:** `upLinkIntervalS` changed from 3600 to 86400 (configurable via JSON)
- **Survives deep sleep:** heartbeat interval honoured across duty-cycle sleep/wake cycles
- **Immediate on first boot:** `lastStatusLoraTimeS = 0` triggers first heartbeat immediately after fresh start
- **Only updates on success:** timestamp only saved to RTC if `sendStatusUpdateMessage()` returns ESP_OK

### Duty-Cycle Deep Sleep — ✅ Operational (Phase 1)
- **Timer-based wake/sleep cycling** for extended battery life (~10-15× improvement)
- **Configurable parameters:** `awakeDurationS` (20-120s), `sleepDurationS` (60-900s)
- **Fast boot path:** skips Bluetooth, Battery, PerfMonitor on timer wake (~5s faster)
- **RTC persistence:** boot count, total detections, session ID survive deep sleep
- **Session continuity:** same SD card folder and CSV file across wake cycles
- **LoRaWAN session preservation** via existing RTC persistence mechanism
- **Auto-start AI inference** on timer wake (only when AI was enabled before sleep, tracked via `rtc_duty_cycle.aiEnabled`)
- **Recording resumes after wake** in record-ON modes (tested on device 2026-05-29): `wav_writer` mode is persisted in RTC (`recordMode`) and restored on timer wake; one WAV file per wake; WAV file is cleanly closed (`finish()`) before deep sleep so headers aren't truncated
- **SD free-space cache refreshed on mount** (Bug 7) so record-ON wakes don't hit a false "SD Card is full" (cache was previously only updated by the BT status task, which is skipped on timer wake)
- **LED turn-off** before sleep (IO expander retains state)
- **Button wake escape:** press button during sleep to return to normal boot
- **Triggered via:** Bluetooth `recordOff_detectOn` (AI-only), `recordOn_detectOn`, or `recordOn_detectOff` command activates duty cycle (when `dutyCycle.enable` is set)
- **BT-set timezone persisted to RTC** (Bug 6 in `README-DutyCycle-BugFixes.md`) so CSV detection timestamps survive duty-cycle wakes in the user's local TZ, not the compile-time `TIMEZONE_OFFSET` default. As of 2026-06-05 this is the **highest-priority tier** of the TZ chain (app-set > GPS-longitude-derived > compile default); see the GPS section.
- **See:** `README-DutyCycle-and-LoRa-Cooldown.md`, `README-DutyCycle-BugFixes.md`

### Hardware Support — ✅ Operational
- **LIS3DH accelerometer** for intruder detection and double-tap BT wake
- **PCA9557 IO expander** for expanded GPIO
- **Buzzer** for audio feedback (boot, BT events, LoRa join)
- **Status LEDs** for system state indication
- **Firmware update** via SD card binary

### Configuration — ✅ Operational
- **JSON-based config** on SD card and SPIFFS
- **NVS factory provisioning** (HW gen/rev, serial, LoRa keys)
- **Per-session config snapshot** saved with recordings

### GPS (ATGM336H) — ✅ Operational (time sync + auto-timezone validated on HW 2026-06-05)
- **`lib/gps` / `ElocGPS` singleton** parses NMEA via TinyGPS++ on UART_NUM_1 (RX=GPIO36, TX=GPIO4, 9600)
- **Power via IO expander IO5 MOSFET** (`ELOC_IOEXP::setGpsPower`, `GPS_VCC_EN`) — **ACTIVE-LOW**:
  P-channel high-side switch (AO3401A), gate pulled up by R12, so IO5 LOW = ON / HIGH = OFF.
- **`ElocGPS::deinit()`** powers down cleanly: stop task → delete UART → drive TX (GPIO4) low → IO5 high.
  Called from `enterCyclicDeepSleep()` so the GPS is off during deep sleep.
- **System clock sync** from GPS UTC (`utc_tm_to_epoch` → `timeObject.setTime`); UTC is the source of truth.
- **GPS runs on every boot path** (init moved out of the `!gIsTimerWake` block 2026-06-05).
- **Time sync on every duty-cycle wake** corrects RTC drift across deep sleep: `ElocGPS::waitForTimeSync()`
  blocks up to `GPS_TIME_SYNC_TIMEOUT_S` (default 30 s) after a timer wake, before the awake timer resets so
  the inference window isn't shortened. VBAT hard-wired to the LiFePO4 pack (always ~3.3 V) → warm start,
  fix in seconds (only the very first boot after a full power-down is a cold ~30-90 s fix). `gpsTask()` now
  takes the first sync immediately (not on the 30 s log interval), then re-syncs periodically for drift.
- **Resync gating to save power**: GPS is skipped entirely on a timer wake whose clock is still fresh.
  `GPS_RESYNC_INTERVAL_S` (default 3600 s; 0 = every wake) — if `getEpoch() - rtc_duty_cycle.lastGpsSyncS`
  is within the interval, the wake doesn't power GPS at all (RTC + VBAT-backed module clock hold time).
  Three new `rtc_duty_cycle_t` fields (appended): `lastGpsSyncS`, `gpsTimezoneOffset`, `gpsTimezoneValid`.
  `lastGpsSyncS` is stamped from `main.cpp` via `ElocGPS::lastUtcEpoch()` (not from the gps lib, to avoid
  cross-lib include coupling). Cold-start timeout is now a ~once-per-hour worst case, not every cycle.
- **Auto-timezone from GPS longitude** (`applyGpsDerivedTimezone()`): local-display offset =
  `round(longitude / 15)`. Precedence: **app-set TZ (RTC) > GPS-longitude > compile-time `TIMEZONE_OFFSET`**.
  Affects only human-readable strings / WAV+CSV filenames; epochs stay UTC. Ignores DST/borders by design.
- **Logs position + time to serial every 30 s**
- **GPIO4 reclaimed** from the vestigial direct-GPIO status LED (LEDs are on the IO expander)
- Builds clean on `esp32dev`. `USE_GPS` **enabled** in `project_config.h`. **TODO:** surface lat/lon in
  `getStatus`/LoRa; record measured heap/power delta of the per-wake GPS window.

#### Bring-up findings (2026-05-31)
- **Fixed inverted power polarity (firmware)** — code assumed IO5 high = ON; the schematic is active-low
  (P-channel high-side). `setGpsPower()` now inverts, and `init()` defaults IO5 high (off). Keep this — it
  matches the schematic/reworked board. Do **not** revert.
- **ROOT CAUSE of 2.6 V on VCC / ~25 µA in sleep = HARDWARE: AO3401A drain↔source swapped on the PCB.**
  With source/drain reversed the body diode is forward-biased +3V3 → GPS VCC and conducts regardless of the
  gate, clamping VCC at ~2.6 V and leaking ~25 µA. The switch could never turn off, which is why a correct
  IO5=3.3 V made no difference. **Fixed by board rework.** Earlier phantom-power theories (VBAT diode, RXD
  ESD clamp) were red herrings — VBAT measured 0.2 V, ruling out the VBAT path.
- **Other hardware defects to verify (did not cause the leak):** VBAT (pin 6) ≈ 0.2 V → not receiving +3V3
  (suspected net miswire in the +3V3/VBAT/C26 corner); **C26 ground pad floating**; **VCC_RF (pin 14)** is
  on the switched rail (active-antenna/LNA bias via L2). Check these against the PCB.

## What's Left to Build / Fix

### Recently Fixed
- [x] **Buzzer drones for seconds during LoRa uplink** — `EasyBuzzer` is non-blocking and its tone is only switched off by `EasyBuzzer.update()`, pumped once per cycle in `ElocSystem::handleSystemStatus()`. The LoRa loop runs at the tail of that same cycle, and `node.sendReceive()` blocks for the full airtime + RX1/RX2 windows (seconds at AS923 SF10-SF12). A beep started earlier in the cycle (classically the "Bluetooth ready" notification colliding with the immediate first heartbeat) kept sounding in PWM hardware for the whole transmit. Fixed by calling `EasyBuzzer.stopBeep()` at the top of `ElocLora::sendReceiveWithRecovery()` (the single blocking choke point for both heartbeat and event uplinks) so no uplink can leave a tone droning.

### Known Issues
- [ ] **Automatic gain adjustment** causes audio distortion — disabled
- [ ] **Light sleep during recording** may not actually occur due to I2S APB_FREQ_MAX lock
- [ ] **APLL unreliable** at sample rates below 16 kHz (falls back to PLL_D2)
- [ ] **Thread safety** — shared variables between tasks lack mutex guards (noted TODO)
- [ ] **SD card hot-swap** — removing and replacing SD card requires reboot ("spi bus already initialized")
- [ ] **BT not reconnecting** after certain restart scenarios (esp_restart() BLE issue)
- [ ] **NVS LoRaWAN keys unencrypted** — security risk with physical access

### Desired Improvements
- [ ] **LoRa Event Cooldown** — Phase 2 of duty cycle (reduce LoRa msgs to ~10-15/day)
- [ ] Migrate from Bluetooth Classic to BLE for power savings
- [ ] Add mutex/semaphore guards to all shared task variables
- [ ] Improve SD card hot-swap handling without reboot
- [ ] Verify and optimize light sleep power savings
- [ ] Expand unit test coverage (LoRa persistence, AI detection logic, config)
- [ ] Consider NVS encryption for LoRaWAN keys

## Current Status

**Overall:** The firmware is functional and field-deployable. All major subsystems (recording, AI, LoRa, Bluetooth, power management, duty-cycle deep sleep, GPS) are operational. The newest feature (2026-06-05) is **GPS time sync on every duty-cycle wake plus GPS-longitude auto-timezone** — the device now corrects RTC drift each wake and self-localises its display timezone anywhere, while keeping UTC as the source of truth (validated on hardware). Earlier, **duty-cycle deep sleep** (Phase 1) enabled 5-min sleep / 30s awake cycling for ~10-15× battery life extension. Next up is Phase 2: LoRa Event Cooldown to reduce event messages from hundreds per day to ~10-15 by implementing event start/ongoing/end state machine.

## Evolution of Project Decisions

1. **Dual-partition architecture** chosen to separate high-power Bluetooth bootstrap from low-power recording — allows independent development of each partition
2. **ESP-IDF + Arduino hybrid** framework used to get both low-level ESP-IDF control (I2S, power management) and Arduino ecosystem convenience (BT Serial, libraries)
3. **Edge Impulse** selected for AI inference — provides model training studio, ESP32-optimized TFLite deployment, and good documentation
4. **RadioLib** chosen for LoRaWAN over LMIC — more actively maintained, cleaner API, better ESP32 support
5. **Bluetooth Classic SPP** used instead of BLE for compatibility with existing Android app — BLE migration planned but not yet prioritized
6. **SDIO** for SD card instead of SPI — significantly faster write speeds required for continuous audio recording
7. **Double buffering** pattern for audio pipeline — ensures continuous capture without gaps during SD card writes
8. **Dual-storage LoRa persistence** (RTC + NVS) — balances fast wake-up with permanent DevNonce continuity
