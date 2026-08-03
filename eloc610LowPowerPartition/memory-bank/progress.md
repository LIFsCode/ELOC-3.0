# Progress

## What Works

### Core Functionality — ✅ Operational
- **I2S audio capture** from MEMS microphones (ICS-43434, DMM-4026-B-I2S-R)
- **WAV file recording** to SD card with configurable sample rate, duration, and channel
- **Double-buffered writing** pipeline with FreeRTOS tasks on separate cores
- **Three recording modes:** disabled, single (triggered), continuous
- **SD card** mounting via SDIO with free space checking and session folder creation
- **Recording refused without an SD card** (V1.62, 2026-08-02, build-verified): `setRecordMode` rejects
  record-ON modes when no card is mounted instead of reporting a session that writes nothing; the app
  shows the device's reason in an alert. Detection-only modes stay allowed (LoRa alerts still work).
- **SD card hot-swap** (V1.61, 2026-08-01, removal path tested on hardware, re-test pending): removal is
  detected from the card-detect switch on IO expander IO4 (polarity `SDCARD_DETECT_PRESENT_LEVEL`,
  self-checked against the boot mount, falls back to throttled filesystem probing if it disagrees);
  recording and SD logging are stopped before the volume is released, and inserting a card re-mounts
  it, rebuilds the session folder / EI CSV / log file and resumes the interrupted recording mode —
  no reboot. See `ElocSystem::handleSdCardHotSwap()` and `handleSdCardRemounted()` in `main.cpp`.
- **FAT32 + exFAT SD cards** (2026-07-06, build-verified, hardware verification pending): exFAT
  enabled by patching `FF_FS_EXFAT` in the packaged IDF 4.4.7 FatFs via pre-build script
  `tools/patch_fatfs_exfat.py`; filesystem type logged at mount; free-space 32-bit overflow fixed
  for large cards. See `README-exFAT-Support-Plan.md` and `techContext.md`.

### AI Inference — ✅ Operational
- **Edge Impulse integration** with TFLite Micro models
- **Both continuous and non-continuous** inference modes
- **Configurable detection:** threshold, observation window, required detections count
- **CSV logging** of inference results to SD card
- **AI-triggered recording:** detection can start WAV recording in single mode
- **Deferred AI startup** to prevent BT command timeouts
- **Reusable KissFFT plan** (V1.55, 2026-07-23, hardware-validated 2026-07-25):
  `numpy.hpp::software_rfft()` now creates the ~10.5 KB plan once and reuses it across all 32 MFE
  frames/inferences instead of regenerating twiddles and reallocating PSRAM per frame. This addresses
  the placement-sensitive V1.54 DSP regression (~616 ms -> ~900 ms). A two-day hardware soak held DSP
  at 52–54 ms and classification at 127–130 ms with stable internal/PSRAM heaps and no reported DSP
  allocation or classifier failures. The plan remains PSRAM-first and costs only 8 B of internal BSS.
- **Automatic FFT-cache patch recovery after model exports** (V1.56, 2026-07-25, build-verified):
  `tools/patch_ei_fft_cache.py` runs before PlatformIO compilation and restores the cached-plan edit
  when a new Edge Impulse SDK overwrites `numpy.hpp`. It is sentinel-idempotent and fails closed when
  the upstream `software_rfft()` no longer matches the verified implementation.

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

### Firmware Update — 🔨 Built, hardware verification pending (2026-07-06, V1.47)
- **SD updater fixed** (Phase 0): NULL-deref/error-handling bugs gone, date-comparison removed
  (integrity checks only, downgrades allowed), optional SHA-256 via `elocupdate.json`,
  `result.json` outcome file, **bootloader rollback** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`)
  with post-boot confirm in `main.cpp`. Manual SD-swap path unchanged and still supported.
- **BT transfer protocol** (Phase 1): `setFwUpdateBegin`/binary frames/`getFwUpdateStatus`/
  `setFwUpdateAbort`/`setFwUpdateApply`; preflight (recording/AI off, SD, free space, battery
  3.1 V LiFePo / 3.5 V LiPo, slot size); resume across disconnects; `fwUpdateProto`/`buildVariant`
  advertised in `getStatus`. Counterpart UI in the Android app (Device Settings → Advanced).
- **Review fixes 2026-07-10 (V1.51)**: fully-staged resume no longer deadlocks (`begin()` skips
  binary mode when the partial is already complete; Begin response gained `state:
  "receiving"|"staged"`; app applies directly, or sends the len==0 sentinel against V1.47–V1.50
  firmware); `validateImageFile()` now refuses images from a different project
  (`esp_app_desc_t.project_name` vs the running image — foreign images that boot stably would
  escape rollback); app variant guard reads `OpenableColumns.DISPLAY_NAME` instead of the opaque
  `lastPathSegment`. Details + deferred minor findings in `activeContext.md`.
- Verification still owed: SD-swap regression, power-pull during flash,
  crashing-image auto-rollback, full BT transfer + mid-transfer drop + resume on hardware
  (including the fully-staged-resume path: drop BT right after the last frame, then re-run the
  update). Partition0 ground truth on deployed devices still gates *field rollout* (see plan doc).

### Power Management — ✅ Operational
- **Dynamic frequency scaling** (DFS) with configurable min/max
- **Automatic mode-based CPU frequency profiles** (2026-07-05, not yet hardware-verified): AI modes
  → fixed 240 MHz; recording-only (no AI/LoRa) → min 10 / max 80 MHz; otherwise configured values.
  Deferred while BT is up (PLL relock constraint), applied on BT teardown or at duty-cycle wake boot.
  See `ElocSystem::pm_requestProfile()` and `README-Config-Restart-Semantics.md`.
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

### Intruder Alarm over LoRa — ✅ Implemented (2026-07-04, V1.43, awaiting hardware validation)
- **Knock-based trigger** (existing LIS3DH click counting in `notifyStatusRefresh()`) now raises a
  LoRa alarm instead of only beeping the buzzer
- **`INTRUDER_MSG` (msgType 2) uplink:** epoch, GPS fix flag, lat/lng (×1e5), fix age, battery SoC
- **Periodic tracking:** re-sent every `intruderCfg.alarmIntervalS` (default 600 s, live-applied,
  min 60 s) while the alarm is active; failed sends retried after 60 s
- **GPS held powered** during an active alarm (burst power-down suspended) for a live position
- **24/7-only feature**: intruder detection is disabled whenever `dutyCycle.enable` is set. An
  EXT1 knock-wake-from-deep-sleep variant was tried and reverted after a failed hardware test
  (buzzer vibration false-triggered the knock sensor on every BT connect; heap exhaustion —
  `EIDSP_OUT_OF_MEM` -1002 / classifier -5 — when BT+LoRa+GPS+AI all run after the full-boot
  knock wake; device failed to re-enter sleep). See activeContext for details.
- **Buzzer→knock-sensor guard** (hardware-test fix, kept): clicks are ignored while the buzzer is
  active + 1 s settle time after; the buzzer shares the PCB with the LIS3DH and its vibration
  registers as knocks (previously every app-connect beep counted, and the alarm's own beeping
  re-triggered it indefinitely)
- **Decoder + backend:** TTN payload-formatter case 2 (paste into TTN console!), `TnnType.intruder`
  in Cloud Functions, `lastIntruderAlert` in `device_status_cache`, `alert_history` entry
- **Remaining limitations:** dashboard/frontend UI for the alert still open; deep sleep is
  knock-blind by design (24/7 mode is the intended deployment for this feature)

### Hardware Support — ✅ Operational
- **LIS3DH accelerometer** for intruder detection and double-tap BT wake
- **PCA9557 IO expander** for expanded GPIO
- **Buzzer** for audio feedback (boot, BT events, LoRa join)
- **Status LEDs** for system state indication
- **Firmware update** via SD card binary

### Configuration — ✅ Operational
- **JSON-based config** on SD card and SPIFFS
- **NVS factory provisioning** (HW gen/rev, serial, LoRa keys)
- **Serial-derived factory names and production defaults** (V1.65, 2026-08-03, build-verified):
  AutoFlasher uses the last five serial digits (`250700250` becomes `ELOC_00250`); firmware migrates
  missing/empty/legacy `ELOC_NONAME` values and the exact full-serial name produced by V1.63 while
  preserving commissioned names. V1.65 also repairs the freed-memory garbage V1.64 could persist
  while serializing the derived name. Defaults: 1 h files, CPU 80/80 MHz,
  light sleep off, APLL off, ICS-43434. Optional `--factory-reset-config` erases stale SPIFFS.
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
- **Live accuracy & clock-source for the app** (2026-07-21, V1.54, build-green, bench pending):
  `getStatus` `hasFix` is now **live-gated** (`ElocGPS::hasLiveFix()`, valid && age < 3 s) — fixes the
  app's indoor "Fix (0 sats)"; internal `hasFix()` stays latched for last-known-position consumers. GPS
  is **held powered while a BT client is connected** (`manageGpsWhileAwake`, 10 s failed-init backoff)
  so the app sees a live fix + HDOP; on disconnect the burst logic adopts and powers it down. New
  getStatus keys `gps.fixAge[s]`, `gps.hdop` (0.0 = no live solution; HDOP→m stays app-side),
  `device.timeSource`, `device.tzSource`; new persisted `clock_source_t clockSource` in
  `rtc_duty_cycle_t` (magic `0xE10CDC2E → 0xE10CDC3E`), stamped in `cmd_SetTime`/`manageGpsWhileAwake`.
  `printStatus` JSON doc 1536→2048. `GPS_RESYNC_INTERVAL_S` restored 200→3600.
- Builds clean on `esp32dev`. `USE_GPS` **enabled** in `project_config.h`. Lat/lon now in `getStatus`
  (and LoRa via the intruder uplink). **TODO:** record measured heap/power delta of the per-wake GPS
  window; measure `printStatus` stack high-water under recording+LoRa+BT (validate the 2048 doc).

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

### Recording Scheduler — ⏮️ REVERTED, not shipped (removed 2026-07-21)
The full scheduler (firmware `lib/ElocScheduler/` NOAA sun-calc engine + config/RTC/sleep-path
integration; app `SchedulerActivity`/`ScheduleEntryEditorActivity`/`driver/Scheduler.kt` + DeviceSettings
section) was code-complete but never hardware-verified. It was surgically removed before the V1.54 push
(user decision) so the GPS-accuracy/uptime work could ship on its own. Entangled files were reverted to
HEAD and the V1.54 hunks re-applied; both builds green after removal. **Full pre-removal state is backed
up** (git-diff patches + untracked dirs) under the session scratchpad `sched-removal-backup/{fw,app}/` —
reapply from there to resurrect it. See `activeContext.md` for the removal detail.

### Internal-heap headroom (BT + LoRa + AI) — ✅ Implemented & HW-validated (2026-07-05, V1.44)
- **Problem:** with recording + AI + BT + LoRa concurrent, internal DRAM ran dry — `MFE failed
  (-1002 = EIDSP_OUT_OF_MEM)` / classifier -5 and `BT_SDP: SDP - no buf for search rsp` (app could
  not connect during detection)
- **`esp_bt_controller_mem_release(ESP_BT_MODE_BLE)`** at boot frees ~30 KB internal DRAM (Classic-
  SPP-only build never used the BLE reserve). HW-validated: free ≈ 98 KB with BT + LoRa up
- **`ei_malloc`/`ei_calloc` strong overrides** (`src/ei_porting_overrides.cpp`): allocations ≥ 8 KB
  go PSRAM-first, smaller stay internal-first, each falls back to the other heap. Two traps hit on
  the way: (1) the file must live in `src/`, not the edge-impulse lib archive, or the linker keeps
  the SDK's weak symbols (verify with `xtensa-esp32-elf-nm`); (2) pure internal-first let the MFE
  matrices starve BT (<4 KB contiguous) and SDP failed again — hence the size threshold
- **HW-validated end state:** during recording + AI + BT, internal min-free ≥ 20.4 KB, ~31 KB DSP
  matrices in PSRAM, DSP time unchanged (615-735 ms), zero -1002/SDP errors incl. knock → connect
  mid-detection. Residual: connects during detection are slow (core-1 CPU contention BT↔AI, not
  memory; possible future fix = pin Bluedroid to core 0)
- **`ENABLE_HEAP_MONITOR`** (`project_config.h`) gates the periodic internal+PSRAM heap log
- **Follow-up:** re-test the 1-in-20 first-inference panic (likely same root cause)

## What's Left to Build / Fix

### Recently Fixed
- [x] **DFS corrupts `esp_timer` and the FreeRTOS tick (V1.59, 2026-08-01; hardware verification
  pending)** — under the old 10/80 MHz `RECORDING_LOW_POWER` profile, `esp_timer` (TG0 LACT ÷ APB) ran
  **+41 %** fast and the FreeRTOS tick behind every `ESP_LOGx` timestamp (CCOUNT) **+23 %** fast; both
  are rescaled on each DFS transition by `on_freq_update()` through different paths. The audio was
  always correct — proven against WAV filename timestamps, which come from `gettimeofday()` →
  `esp_rtc_get_time_us()` (RTC slow clock, DFS-immune): 21 files × 3600 s of audio in 75,622 s of wall
  clock, 0.03 % off. So `I2S sample rate wrong ... audio is unusable` was a **false alarm** from a
  meter reading a broken clock. Real damage was elsewhere: anything scheduled off `esp_timer` fired
  ~41 % early, cutting the duty-cycle GPS acquisition window from 30 s to ~21 s real (and the patient
  cold-start window from 180 s to ~128 s), below what a cold GNSS fix needs. Fixed by removing DFS from
  both low-power paths (`RECORDING_LOW_POWER` → fixed 80 MHz; default `cpuMinFrequencyMHZ` → 80) and
  moving the rate meter onto `esp_rtc_get_time_us()`. **Closes issue #77** (closed 2024-04 without a
  root cause; its stopwatch ratios 1.40/1.20/1.27 are these same skews). Full analysis in
  `README-I2S-Clock-And-LightSleep-Issues.md`.
- [x] **Automatic light sleep RTCWDT resets (V1.59, 2026-08-01)** — `esp_light_sleep_start()` arms a
  1 s RTC watchdog as a safety net; the device hangs inside the sleep sequence and is reset by it
  (reason 0x10, no panic, no backtrace), which wipes the duty-cycle RTC block and returns the unit not
  recording and not duty cycling. Default is now `false` **and** clamped via
  `ALLOW_AUTOMATIC_LIGHT_SLEEP` + `clampLightSleep()` — the clamp is required because a present config
  key always beats the compiled default, so already-provisioned units would otherwise have kept
  `"cpuEnableLightSleep": true`. Underlying hang still unexplained (VDD_SDIO/PSRAM/SDIO suspect);
  blocks open issue #118.
- [x] **`WAVFileWriter` divide-by-zero + dead max-tracker (V1.59, 2026-08-01)** — `speed` divided by
  `writeDurationMs` unguarded (Xtensa traps on integer divide-by-zero, so a sub-millisecond cached
  write would have panicked the `wav_writer` task), and `longestWriteMs` was seeded with `UINT32_MAX`
  so the `WorstCase` field printed a constant `4294967295 ms` for the lifetime of the build.
- [x] **Spontaneous panic-reboot during 24/7 recording (V1.53, 2026-07-14; hardware soak pending)** —
  `ElocGPS::deinit()`'s `vTaskDelete()` of the gps reader task eventually landed while the task held
  the global esp_log mutex inside an `ESP_LOGx`, orphaning the mutex; later log-lock takes timed out
  (interleaved garbage output) until FreeRTOS's `vTaskPriorityDisinheritAfterTimeout
  (pxTCB->uxMutexesHeld)` assert panicked (serial-confirmed on V1.52 after ~33 h; earlier SD log
  showed the same death at ~3 h). Every GPS burst timeout→deinit rolled these dice. Fixed with a
  cooperative shutdown handshake in `lib/gps/ElocGPS.{hpp,cpp}` (`mShutdownReq`/`mTaskExited`, task
  self-deletes, deinit polls ack ≤2 s) + `GPS_TASK_STACK` 3072→4096. Needs a 24/7 bench soak past the
  33 h failure horizon to close.
- [x] **Buzzer drones for seconds during LoRa uplink** — `EasyBuzzer` is non-blocking and its tone is only switched off by `EasyBuzzer.update()`, pumped once per cycle in `ElocSystem::handleSystemStatus()`. The LoRa loop runs at the tail of that same cycle, and `node.sendReceive()` blocks for the full airtime + RX1/RX2 windows (seconds at AS923 SF10-SF12). A beep started earlier in the cycle (classically the "Bluetooth ready" notification colliding with the immediate first heartbeat) kept sounding in PWM hardware for the whole transmit. Fixed by calling `EasyBuzzer.stopBeep()` at the top of `ElocLora::sendReceiveWithRecovery()` (the single blocking choke point for both heartbeat and event uplinks) so no uplink can leave a tone droning.

### Known Issues
- [ ] **Automatic gain adjustment** causes audio distortion — disabled
- [ ] **Automatic light sleep is disabled and clamped** (V1.59) — it hangs this board and the
  safety-net RTC watchdog resets it. The hang itself is unexplained; VDD_SDIO carries both the PSRAM
  and the SDIO card and light sleep power-cycles that rail. Must be root-caused before open issue #118
  ("light sleep between inference runs") can proceed.
- [ ] **Wall-clock drift without GPS** — the RTC source is the internal RC (`INT_8MD256`), calibrated
  once at boot: −2 s/h hot, +4 s/h cold, ~25 s/day net (measured over 21 h). There is no 32.768 kHz
  crystal on ELOC 3.0, so `EXT_CRYS` is not an option. Units built without a GPS module have no drift
  correction at all beyond an app `setTime` per visit. Drift is smallest near the boot temperature.
- [ ] **`cpuMinFrequencyMHZ` is not clamped** — unlike light sleep, the default change to 80 (V1.59)
  does *not* reach already-provisioned units, whose stored config keeps `10` and therefore keeps DFS
  and its timebase skew in the pre-recording window. Deliberate (it is a validated, app-exposed
  setting), but it means each existing device must be changed from the app.
- [ ] **APLL unreliable** at sample rates below 16 kHz (falls back to PLL_D2)
- [ ] **Thread safety** — shared variables between tasks lack mutex guards (noted TODO)
- [ ] **SD card hot-swap** — removing and replacing SD card requires reboot ("spi bus already initialized")
- [ ] **BT not reconnecting** after certain restart scenarios (esp_restart() BLE issue)
- [ ] **NVS LoRaWAN keys unencrypted** — security risk with physical access
- [ ] **`generic_unit_tests` whole-env run fails** — pre-existing (predates the scheduler work),
  unrelated `test_generic_fw_frame_parser` lib-isolation defect: its test.cpp includes only the pure
  `lib/FirmwareUpdate/src/FwFrameParser.hpp`, but PlatformIO's LDF pulls in the rest of
  `FirmwareUpdate`'s hardware-dependent sources (`esp_log.h`/FreeRTOS/`esp_partition.h`), which can't
  compile natively. Needs `FwFrameParser` split into its own pure lib, or `FirmwareUpdate.cpp`
  otherwise isolated from the test's LDF resolution.
- [ ] **`target_unit_selected_tests` build fails at HEAD** — pre-existing LDF chain-mode defect:
  a project lib only reachable via a *second-hop* include (e.g. `lib/Accel`'s `lis3dh_types.h`, via
  `lib/ElocHardware/src/config.h`) isn't resolved when the test file (no `src/main.cpp` entry point)
  is the LDF's only entry point. `esp32dev`/`esp32dev-ei` are unaffected (their entry point is
  `src/main.cpp`, one hop from every project lib). Discovered while adding the scheduler's target
  tests (`lib/ElocScheduler` hits the same defect via `lib/edge-impulse`); needs a fix in
  `lib/Accel`/`lib/CPPANALOGIO_Battery`/`platformio.ini` LDF config.

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
