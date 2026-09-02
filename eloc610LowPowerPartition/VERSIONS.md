# Firmware version history

One line per firmware version — what changed and whether it was verified on hardware. This is the
quick index; the long-form analysis for each work stream lives in
[`memory-bank/changelog.md`](memory-bank/changelog.md) (finished work) and
[`memory-bank/activeContext.md`](memory-bank/activeContext.md) (work in flight).

**Convention:** every push bumps `VERSION` in [`include/project_config.h`](include/project_config.h)
by +0.01 **and** adds a line here, newest first. The version string is what the app shows and what
the OTA update flow compares, so a bump without an entry leaves a build nobody can identify later.

Status column: `HW` = verified on hardware, `build` = compiles and is reasoned through but not yet
bench-tested, `partial` = some paths tested (see the detail doc).

| Version | Date | Status | Change |
|---|---|---|---|
| V1.74 | 2026-09-02 | build | Intruder: a **candidate now sirens for 30 s and sends one LoRa message straight away**, so interference is reported even when it turns out to be an animal. Because the siren blinds the LIS3DH, the candidate's movement-listening window no longer starts until the siren and the knock guard are over — otherwise it would expire while the device was deaf and a real theft would be missed. The siren runs once per episode and is not restarted on promotion to CONFIRMED. New `intruderCfg.armDelayS` (default 300 s): knocks are ignored for this long after a recording/detection mode starts, and after boot, so setting a device up cannot trigger it. Knock detection is also suppressed while a coverage survey is running. `getStatus.intruder` gains `armsIn[s]` / `armDelay[s]`, and `armed` now reflects both. |
| V1.73 | 2026-09-02 | build | Intruder detection: knocks alone no longer raise an alarm. A knock burst opens a **candidate** (`IntruderState_t`); the accelerometer must then see real movement within `intruderCfg.confirmWindowS` for it to become **confirmed**, otherwise it expires having powered nothing on and sent nothing. Fixes the knock counter's off-by-one, so `thresholdCnt=5` now means six knocks, not seven. Alarm uplinks go out **only while moving**, immediately on movement resuming (no stale deadline), and never wait for a GPS fix. A stopped device transmits nothing on the alarm path; the heartbeat instead accelerates to `lorawan.alarmUpLinkIntervalS` (1 h) while latched, with the normal default lowered 24 h → 12 h. New timings `settleMs` / `confirmWindowS` / `quietS` / `alarmTimeoutH` (auto-clear, default 24 h); `intruderCfg.idleIntervalS` deprecated (parsed and reported, ignored). |
| V1.72 | 2026-09-01 | build | LoRa coverage survey ("signal strength mapper"): new `surveyCfg` config block and `ElocLora_survey.cpp`. Uplink-first - position uplinks carry no downlink and the map is built from TTN's gateway-side RSSI/SNR, with a LinkCheckReq only every Nth sample or on demand. Sampling triggers on **distance** with a per-SF duty-cycle time floor; adaptive SF ladder 7→9→10→12 driven by the link checks; ADR off and `setDutyCycle(true, 36000)` enforced for the session. GPIO0 forces an extra uplink, `getLinkCheck` (app) forces one with a downlink and beeps the 0-5 count readout. Every transmission is logged to `/sdcard/survey/*.csv` with its frame counter, so joining against Firestore turns a missing server record into a confirmed dead spot. KML + GPX are generated from the CSV at session close (and re-runnable over an older session), so a card pulled out of a finished survey opens straight in Locus Map or Avenza. New commands: `setSurveyMode`, `getSurveyStatus`, `getLinkCheck`, `setSurveyExport`. |
| V1.68 | 2026-09-01 | build | No functional change over V1.67. Version bump so this build can be published as the first GitHub prerelease under the new OTA convention (flat tag equal to the embedded version, plus an ELOC-P_V1.68-ei.bin asset), which is what the app update flow reads. |
| V1.67 | 2026-08-05 | build | Revert V1.66's `BT_ACTIVE` profile — hardware-falsified, see below. Keeps the knock refresh. Bluetooth-unconnectable-while-recording remains **open**; CPU frequency is ruled out. |
| V1.66 | 2026-08-04 | HW (negative) | Attempted fix for "Bluetooth unconnectable while recording": `BT_ACTIVE` profile pinning 240 MHz for each BT window, on the theory that the controller does not answer pages at 80 MHz. **Did not work** — on hardware, with the CPU confirmed at 240 MHz for the whole window and a session running, connections still failed with no `HCI_Connection_Complete`. The 240 MHz part is reverted in V1.67. Also added, and kept: a repeat knock restarts the idle timeout instead of being ignored (verified on hardware). |
| V1.65 | 2026-08-03 | build | Fix dangling `nodeName.c_str()` serialization that wrote freed-memory garbage to SPIFFS; invalid UTF-8/control-character names are recovered automatically from the factory serial. |
| V1.64 | 2026-08-03 | build | Correct serial-derived node names to use exactly the last five serial digits (`250700250` → `ELOC_00250`) and automatically migrate the incorrect full-serial name persisted by V1.63. |
| V1.63 | 2026-08-03 | build | Factory flashing derives `device.nodeName` from the NVS serial (`123` → `ELOC_00123`), migrates only missing/empty/`ELOC_NONAME` values while preserving commissioned names, and sets production defaults to 1 h files, 80/80 MHz, light sleep off, APLL off, ICS-43434. |
| V1.62 | 2026-08-02 | build | `setRecordMode` refuses record-ON modes with no SD card mounted (was silently accepted, so the app showed a session that wrote nothing); invalid-mode requests no longer fall through to a success response. Pairs with app-side alert. |
| V1.61 | 2026-08-01 | HW | Hot-swap teardown no longer starves Bluetooth: poll moved from the BT/status task to the main loop, non-blocking SD log abandon (`Logging::abandonSdCard()`), mount-retry backoff 2→10 s, sticky rejection of a card-detect line that lies. |
| V1.60 | 2026-08-01 | partial | SD card hot-swap: removal detected (card-detect on IO expander IO4, with filesystem-probe fallback), recording + SD logging stopped before unmount, re-insertion re-mounts and resumes without a reboot. Removal path tested; superseded by V1.61 for the BT starvation it exposed. |
| V1.59 | 2026-08-01 | HW | DFS corrupts both software timebases — the "audio is unusable" rate error was a false alarm. `RECORDING_LOW_POWER` fixed at 80 MHz, rate meter moved to `esp_rtc_get_time_us()`, automatic light sleep disabled and clamped, GPS-absent detection. Closes issue #77. |
| V1.58 | 2026-07-31 | HW | GPS could never finish a cold first fix in duty cycle: the patient 180 s path was gated on "clock unset", which the app's `setTime` at commissioning turned off. Now gated on `gpsFirstFixOutstanding()`, capped at 10 wakes. |
| V1.57 | 2026-07-30 | build | Config/commands read `battery.avgIntervalMs` under its real key. |
| V1.56 | 2026-07-25 | build | `tools/patch_ei_fft_cache.py` pre-build script reapplies the V1.55 KissFFT cached-plan patch after an Edge Impulse model export overwrites `numpy.hpp`. |
| V1.55 | 2026-07-25 | HW | Fixes the V1.54 DSP regression: KissFFT plan created once and reused across all MFE frames instead of per frame (~900 ms → 52–54 ms DSP). |
| V1.54 | 2026-07-22 | HW | GPS live fix/HDOP and clock-source markers in status, deployment uptime. Coordinated with app 5.42. |
| V1.53 | 2026-07-14 | partial | 24/7 spontaneous reboot fix: GPS task cooperative shutdown (the panic was `vTaskDelete` orphaning the `esp_log` mutex) plus a stack bump; `tools/checkProjectVer.py` guards PlatformIO's cached `PROJECT_VER`. Long soak still owed. |
| V1.52 | 2026-07-13 | HW | Real firmware version embedded in the app descriptor via `PROJECT_VER`, fixing the bogus downgrade prompt in the app's update dialog. |
| V1.47–V1.51 | 2026-07-10 | HW | Firmware update over Bluetooth SPP: binary frame transfer mode, CRC32 stop-and-wait acks, `setFwUpdateBegin`/`getFwUpdateStatus`/`setFwUpdateAbort`/`setFwUpdateApply`. |
| V1.46 | 2026-07-06 | HW | exFAT SD cards supported by patching `FF_FS_EXFAT` into the packaged IDF 4.4.7 FatFs (`tools/patch_fatfs_exfat.py`); filesystem type logged at mount. |
| V1.45 | 2026-07-05 | HW | ~10 s of unnecessary boot-time delays removed. |
| V1.44 | 2026-07-05 | HW | Heap headroom: BLE controller memory released (~30 KB) and PSRAM-first Edge Impulse allocators, fixing MFE −1002 and BT SDP no-buffer failures with BT + LoRa + GPS + AI running together. |
| V1.43 | 2026-07-05 | HW | Intruder alarm over LoRa (msgType 2) with GPS position, 24/7 modes only. |
| V1.42 | 2026-07-04 | HW | Reboot-safe CPU frequency changes, LoRa DevNonce persistence in NVS, `reboot` command. |
| V1.41 | 2026-04-17 | HW | Duty cycle with deep sleep; delayed AI startup for stable BT connections. |

Entries for V1.41–V1.59 were reconstructed from git history and the memory bank when this file was
added (2026-08-02); versions before V1.41 are not tracked here — see `git log`.
