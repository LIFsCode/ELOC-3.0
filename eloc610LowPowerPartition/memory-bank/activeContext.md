# Active Context

## TFLite Micro runtime (Edge Impulse replacement, Phase 1) — implemented 2026-09-24, V1.79, hardware pending

New build env **`esp32dev-tflm`**. It runs the INT8 "device model package" that the web app's ELOC
Model Training builds after every job, directly on TensorFlow Lite Micro, with no Edge Impulse code
linked. The plan (T0–T8, decisions, acceptance) is `README-TFLM-Runtime-Plan.md`, and the user
guide is the top section of `README-ai.md`. `esp32dev-ei` stays the shipped build and the fallback.

**Shape.**
- `include/ai_runtime.h` makes `AiRuntime` either `EdgeImpulse` or `ElocDetector`.
- Shared code uses `ELOC_AI_ENABLED` and `aiRuntime`: sampler, status, LoRa, duty cycle, main loop.
- The per-window detection rules were pulled out of `ei_callback_func()` into
  `handleClassification()` in `main.cpp`, which both runtimes call. The EI log lines, CSV rows and
  threshold stayed as they were.
- `lib/eloc_ml` is the portable core, also built on the PC: package parser, spec-v1 mel front-end
  on KissFFT, TFLM classifier.
- `lib/eloc_detector` is the firmware glue: AI task, buffers, silence guard, counters.
- `lib/tflite-micro` + `lib/esp-nn` are esp-tflite-micro v1.3.4 + ESP-NN v1.1.2, unpatched.

**Verified on the PC:**
- T0: both vendored trees compile with GCC 8.4.
- `esp32dev-tflm` and `esp32dev-ei` build; the TFLM map has no Edge Impulse object.
- Native golden-vector test: quantized model inputs bit-identical on all 9 records.
- The on-target test builds.

**Not yet run on hardware:**
- `pio test -e target_tflm_tests` (golden vectors on the device, DSP/NN ms, `arena_used_bytes`);
- the acceptance list in the plan.

**Decisions and findings from the implementation:**
- **Full-scale sine: 1.6e-3 feature error, not the 1e-3 the plan asked for.** It is float32 FFT
  round-off in bands 100 dB below a pure tone that sits exactly on an FFT bin. The reference is
  float64-accurate, and a full complex FFT would get 6.4e-4 at twice the FFT cost. Real audio agrees
  within 3.7e-6 and the quantized input is still exact, so that single synthetic record's feature
  tolerance is 2e-3. Question for the user: accept, or pay for the complex FFT.
- **Quantization rounds half to even** (`rintf`, like NumPy), not `roundf` as the plan said. The
  result is exact parity instead of ±1.
- **Front-end working set (~12.6 KB, internal RAM) is held only while the AI task runs**, not from
  boot. This is because of the Bluetooth internal-heap sensitivity (see Current Work Focus). The
  model, arena, features and audio buffers are PSRAM, allocated at boot.
- **IRAM:** the EI build has ~330 B of IRAM left. `heap_caps_aligned_alloc()` pulled 1.5 KB of
  TLSF code into IRAM and overflowed it, so `MlAlloc` aligns by hand.
- **GCC 8.4 ICE** ("insn does not satisfy its constraints") when the mel band sum was inlined; worked
  around with a `noinline` helper (`bandSum()`).
- **base64** is decoded by a small built-in decoder, not `mbedtls_base64_decode`, so the native test
  runs the same code.
- **`ElocDetector` got its own library** (`lib/eloc_detector`), not `lib/eloc_ml` as the plan said.
  Inside `eloc_ml`, the on-target test dragged the whole firmware (config, LoRa...) into its build.
- **`generic_unit_tests` was broken on this machine before this work** (every native test failed to
  build). It now uses `lib_ldf_mode = off` plus explicit `lib_deps`, and all 19 native test cases
  pass.
- **Timer-wake CSV name** is now built after the model loads, because the TFLM name carries the job
  id. It is built by one function, `buildInferenceResultFilename()`, for both runtimes.
- Web-app follow-up still open: the training worker should add silent / very low-level windows to
  every model's background set. The first chainsaw model scores digital silence 0.95; the
  firmware's silence guard (`AI_SILENCE_PEAK`) is only the dead-mic net.

---

## Current Work Focus

**Bluetooth unconnectable while recording — STILL OPEN (2026-08-04/05, V1.66 → V1.67).**
Knock-wake brings Bluetooth up and the device is genuinely discoverable, but every connection attempt
fails after ~5.1–5.3 s (BR/EDR page timeout) with **zero `BT_HCI` lines** on the device side — no
`HCI_Connection_Complete` is ever emitted, so the failure is below SDP/RFCOMM. Diagnosed from
ELOC_00150's SD logs (`D:\log\eloc.log*`) cross-referenced against the app's logcat; the two were
aligned to wall clock via the `setTime` epoch and the DFS-immune WAV filenames, confirming the failed
attempts fell inside a live 60 s BT window.

**The CPU-frequency hypothesis was wrong.** The correlation looked perfect — all 10 successful
connections across 8 log files at 240 MHz, all failures under `RECORDING_LOW_POWER` — but CPU frequency
and "a session is running" are perfectly confounded, because that profile is applied if and only if
recording is active. V1.66's `BT_ACTIVE` profile held 240 MHz for the whole window and the connection
still failed, which falsifies it; V1.62 had already ruled out DFS. V1.67 reverts the 240 MHz switch and
keeps only the knock refresh (which works, hardware-verified).

**Current position:** the differentiator is that a recording session is active. Ruled out so far: CPU
frequency, DFS, internal-heap *leak* (the 46 KB/32 KB state is steady from 2 min to 40 h), pairing, the
app, and the EI buffers (they are in PSRAM despite the log saying "in RAM"). Heap *pressure* is still
live — see the V1.44 `SDP - no buf for search rsp` precedent, whose healthy benchmark was ~98 KB free
with BT up versus 46 KB here. Full state, next tests and the planned instrumentation are in
`progress.md` → Known Issues.

---

**Intruder V1.74: candidate reports, and an arming delay — 2026-09-02, build-verified only.**

Two changes on top of V1.73, both from EDsteve.

**A candidate now announces itself.** It sirens for 30 s and sends one LoRa message immediately,
rather than staying silent. The reasoning is a different requirement from the one V1.73 solved: even
if a monkey is playing with the device, that is worth knowing. If nothing then moves, the device
returns to whatever mode it was in and that single message is the only trace.

This collided head-on with the coupling V1.73 was built around: the siren shakes the LIS3DH, and the
buzzer guard suppresses motion sampling while it sounds. A 30 s siren over a 20 s confirm window would
have left the device **deaf for the entire window**, so a real thief who knocked and carried it off
would never have been confirmed — the exact failure V1.73 existed to prevent, reintroduced by the fix.

Resolved by starting the candidate's listening window only after the siren and the guard are over:
`listenFromMs = mCandidateStartMs + C_INTRUDER_SIREN_DURATION_MS + C_BUZZER_KNOCK_GUARD_MS`, with a
signed comparison so the not-yet-listening case is negative rather than a huge unsigned number. Total
candidate lifetime is therefore ~31 s + `confirmWindowS`. Nothing is missed: someone carrying the
device away is still moving 30 s later. The siren runs **once per episode** and is deliberately not
restarted on promotion to CONFIRMED — a second 30 s of blinding would land exactly when the tracking
uplinks start depending on `isDeviceMoving()`.

No new message type or flag bit. EDsteve chose no distinction, and it holds up better than it first
looks: a candidate sends **one** message while a confirmed theft sends a **stream** every 60 s, and the
flags byte's `moving` bit is false on a candidate. A single point versus a track already reads
differently on the map with no backend change.

No notification cooldown either. The premise for one was wrong — the LIS3DH click detector needs sharp
z-axis double-taps above threshold, and wind displaces rather than producing impulses. If the field
shows spurious candidates (hail, an animal worrying the case), a cooldown is the knob to add.

**Arming delay (`armDelayS`, default 300 s).** Knocks are ignored entirely for this long after
recording/detection becomes active, and after boot. Detected by polling the record-active edge inside
`handleSystemStatus()`, which already reads `wav_writer.get_mode()` and `ai_run_enable` — no hooks at
the `setRecordMode` call sites. Boot is covered by `mRecordActiveSinceMs == 0` stamping on the first
pass, so a battery swap in the field gets the same grace as a fresh deployment.

Also folded in: **knock detection is suppressed while a coverage survey runs.** A survey is a handheld
mode — the device is carried and knocked about all day and the buzzer belongs to the survey readout,
so an intruder siren in the middle of it would be both wrong and confusing. That interaction existed
unnoticed since V1.72.

**Not tested on hardware.** The nine-case table in `README-Intruder-Rework-Plan.md` needs two new
cases: six knocks on a table must now siren and send exactly one message and then go quiet; and a
knock-then-carry must still confirm, which is the case the siren timing could break.

---

**Intruder detection: candidate vs confirmed — implemented 2026-09-02, V1.73, build-verified only.**
Plan and code survey: `README-Intruder-Rework-Plan.md`.

The problem: knocks alone raised a full alarm, so a branch, an animal or a passing vehicle rattling a
device on a tree powered the GPS on, sounded a 30 s siren and started 10-minute LoRa tracking uplinks.

Now `notifyStatusRefresh()` promotes a knock burst to **CANDIDATE**, not to an alarm. The
accelerometer then has to see real movement within `confirmWindowS` for **CONFIRMED**; otherwise the
candidate expires having transmitted nothing and powered nothing on. Three interlocking details made
this harder than it looks:

- **`updateMotionState()` used to run only while an alarm was already active**, and zeroed
  `mLastMotionMs` otherwise — so there was no motion state *before* an alarm at all. It now runs for
  CANDIDATE too.
- **Its optimistic seed had to go.** The old code assumed "just knocked = being handled" so the first
  sample would not park the device. Under the candidate model that would confirm *every* candidate
  instantly, table included. Movement must now be observed.
- **The siren had to be gated on CONFIRMED.** It shakes the LIS3DH, and `C_BUZZER_KNOCK_GUARD_MS`
  blinds the accelerometer while it sounds — so a siren on the candidate would have blocked the very
  reading the candidate is waiting for, for its whole 30 s.

`settleMs` (2 s) covers the knock burst's own ringing, for the same reason.

**Off-by-one fixed.** `cntFastUpdates` started at 0 and the first knock took the else-branch, so
`thresholdCnt = 5` needed *seven* knocks. Bursts now start the run at 1: six knocks, as documented.

**Uplinks only while moving.** A device put down has nothing new to report and the GPS is the biggest
draw on the board, so both stop. Movement resuming clears the deadline and reports at once
(`mIntruderWasMoving` edge) rather than waiting out an interval set before it was set down. The first
uplink never waits for a fix — the payload's flags byte already distinguished "moving" from "has fix",
so "moving, location unavailable" needed no format change.

**The heartbeat is what stops a stopped device looking dead.** Since alarm uplinks are suppressed
while still, `loraConfig.alarmUpLinkIntervalS` (1 h) replaces the normal interval while an alarm is
latched; the normal default drops 24 h → 12 h. It costs no GPS — the status message carries battery
and recording state, not a position. The two schedules were already independent by construction
(`mNextIntruderUplinkS` vs `rtc_duty_cycle.lastStatusLoraTimeS`), so this needed no untangling.

**Latching kept, with a way out.** A confirmed alarm survives being put down, so picking the device up
resumes tracking with no fresh knocks. `alarmTimeoutH` (24 h of stillness) auto-clears it — without
that, a device that alarmed once keeps the accelerated heartbeat for the rest of its deployment. No
reset command was added; EDsteve chose the timeout alone.

`intruderCfg.idleIntervalS` is deprecated: a stopped device now sends nothing, so there is no idle
cadence to configure. Still parsed and reported so older app builds keep working.

App side: the three new timings in Device Settings (greyed out below firmware 1.73 via
`reportsCandidateState`), and "Knocks seen · waiting for movement" on the status page — the only
outward sign a candidate exists, since it deliberately makes no sound.

**Not tested on hardware.** The nine-case table in `README-Intruder-Rework-Plan.md` is the bench plan;
cases 2 (knocks on a table cost nothing) and 7 (pick-up reports immediately) are the ones that would
falsify the design. Also watch for `Status JSON exceeded its 2048 byte document` on the first
getStatus — four fields were added to a doc that was already fairly full.

---

**LoRa coverage survey ("signal strength mapper") — implemented 2026-09-01, V1.72, build-verified only.**
New `lib/ElocHardware/src/ElocLora_survey.cpp` plus a `surveyCfg` config block. Walk or drive a route
and map where the gateway can hear the device.

The shape follows from one fact: a **Class A device cannot listen for a signal**. Its radio is asleep
except in the two RX windows after its own uplink, and gateways do not beacon, so "check the signal"
always means "transmit and see what comes back". A beacon box at the gateway was designed first (rev 1/2
of the plan) and abandoned — the gateway site is not reachable often enough to maintain one.

So the survey is **uplink-first**. Position uplinks go out continuously and request no downlink; TTN
attaches gateway-side RSSI/SNR to each one, which is the authoritative measurement and is strictly
richer than a LinkCheckAns margin (it is per-gateway). A downlink is spent only every
`linkCheckEveryN`-th sample (default 24 ≈ 10 downlinks per 2 h session, exactly TTN's daily allowance)
or when the ranger asks. That removed the whole fair-use argument: every legal and physical limit is met
with room to spare, and the only policy number in play is one we now stay inside.

Key decisions:
- **Distance triggering, not a fixed interval.** A fixed 30 s gives 36 m spacing on foot and 168 m from
  a vehicle at 20 km/h, and a device standing still would spend the day's allowance on one coordinate.
  `minDistanceM` sets resolution, `minIntervalS` is only a floor. Presets: 25 m walking, 100 m driving —
  both land near 3 h of surveying inside the daily uplink budget.
- **The per-SF floor always wins over the configured one** (`C_SF_MIN_INTERVAL_S`, SF7 10 s → SF12 180 s),
  so raising the SF slows the cadence automatically and the app never has to reason about airtime.
- **ADR off + `setDutyCycle(true, 36000)`** for the session. Nothing in this firmware had ever called
  `setDutyCycle`, so `dutyCycleEnabled` defaulted to false and the stack would transmit as fast as asked.
  ADR off also stops the `ADRACKReq` back-off, which after 64 unanswered uplinks (~30 min of an
  uplink-only survey) would force its own data-rate fallback and fight the ladder.
- **Every transmission is written to `/sdcard/survey/<node>_<ts>.csv` with its frame counter.** An uplink
  nobody hears leaves no trace on the server, so without this "no coverage" and "nobody walked here" are
  indistinguishable. Joining the CSV against Firestore on `fcnt` turns an absent record into a confirmed
  dead spot with a position on it — the half of the dataset that cannot be recovered any other way.
- **Triggers.** A double-knock gesture was designed and rejected: it is a *shock* gesture and these
  devices travel on bad roads, so every pothole would spend a downlink. Instead GPIO0 (free during a
  survey, since recording is suspended) forces an extra **uplink** with no downlink, and the app's
  `getLinkCheck` forces one **with** a downlink and beeps the result.
- **Buzzer readout counts the beeps** — level N = N beeps, pitch rising too, so it is coded twice and
  needs no reference tone; "no answer" is one long 175 Hz tone. Frequencies avoid the tones already in
  use (98 battery, 261 BT, 523 idle, 523/659/784 join OK, 400/300 join failed).
- **Survives a reboot by design** (`surveyCfg.enable` is in the config cascade), which is exactly why the
  session timeout and the daily uplink/downlink counters are not optional.

Two cross-task hazards were designed around: `setSurveyMode` deliberately does **not** call
`surveyStart()`/`surveyStop()` — both touch the radio (`setDatarate`/`setDutyCycle`) and the command runs
on the Bluetooth task, which could land inside a blocking `sendReceive()` on the main loop. It only flips
the persisted flag; the LoRa loop opens and closes the session itself. Likewise `buttonISR` only stores a
DRAM flag, drained with debounce by `handleSurveyButton()` in task context.

App side (V1.72-compatible): new `driver/Survey.kt`, a "LoRa signal survey" section in Device Settings
with enable / distance / interval / spreading-factor rows, and a `startSF == 0` probe that greys the
section out on older firmware.

**Not done yet:** KML/GPX export, the TTN payload formatter for msgType 3, the Cloud Function branch and
the web coverage map, the app's live readout and "measure here" button (the `getLinkCheck` command exists
and is what that button will call), and the wiki pages. **Nothing is hardware-tested.** The first bench
item is confirming `setADR(false)` really suppresses the back-off across 64+ unanswered uplinks, and that
`getMacLinkCheckAns()` returns what is expected against a real gateway.

---

**Intruder alarm: siren, auto-off, status reporting (2026-09-01, V1.69 / V1.71), build-verified.**
The knock alarm's buzzer is now a swept siren - 600 -> 1800 Hz over 1 s, then a 200 ms silence gap,
repeating - driven by `ElocSystem::updateIntruderSiren()`, and it switches itself off 30 s after the
trigger (V1.69 shipped 5 min; EDsteve cut it to 30 s in V1.71 - long enough to startle someone and to
be located by ear, short enough not to keep announcing the device). The alarm itself is unaffected: LoRa alarm uplinks keep going out every
`intruderCfg.alarmIntervalS` with the live GPS position and duty-cycle sleep stays blocked; the siren
simply stops draining the battery and telling whoever took the device where it is. The siren owns the
buzzer while it runs, so the generic `EasyBuzzer.update()` in the unchanged-status path is suppressed
and the intruder branch of the LED/buzzer chain no longer fires the old one-shot 494 Hz beep (it still
comes first in the chain so battery/SD/BT feedback cannot cut into an alarm). `getStatus` gained an
`intruder` section - `enabled`, `armed`, `alarmActive`, `sirenActive`, `alarmAge[s]`,
`alarmInterval[s]` - so the app can finally show whether an alarm is firing; `armed` is false in
duty-cycle mode, where knock detection is disabled by design. The Android app renders it as an "Alarm"
row in the Intruder section (firmware < 1.69 reads back as "not triggered") and finally exposes
`alarmIntervalS` as a seconds-range editor (60 s..1 h, the 60 s matching the firmware clamp), and the
web dashboard and LoRa map now draw intruder alarms in blue with their reported positions as a
movement track, annotated with the GPS fix age the alarm carried.
**Not hardware-tested yet** - the siren pattern, the 30 s cut-off and the buzzer/knock guard
interaction want a bench check on a real unit.

**V1.70 follows immediately: motion-gated alarm reporting.** During an alarm the GPS, not the radio, is
what drains the battery, so it is now powered only while the device is actually being moved. The
accelerometer is sampled at 4 Hz (its data registers are already high-pass filtered, so gravity is out
of the way and stillness reads as ~0); after C_MOTION_QUIET_MS (5 min) without a sample above 0.08 g the
device counts as parked, the GPS is powered down and the uplink cadence backs off from
`intruderCfg.alarmIntervalS` to the new `intruderCfg.idleIntervalS` (default 1 h), repeating the last
known fix - whose growing `fixAge` the app and web UI now show. It deliberately never goes silent: a
device that stops transmitting is indistinguishable from one that was destroyed or shielded. Movement
resumes both within one sample. The payload gained a "moving" bit (flags bit1), carried through the TTN
formatter, the Cloud Function, `getStatus` and the web UI (parked positions draw hollow on the map).
The buzzer/knock guard also suppresses motion samples, so the siren cannot hold the device "moving".
Same caveat: build-verified only, no bench test yet.

---

**Factory provisioning identity/defaults (2026-08-03, V1.65), build-verified.**
`AutoFlasher.py` now reports and records a device name derived from the exact serial placed in NVS
using its last five digits (`250700250` → `ELOC_00250`). Firmware uses that same factory serial at
boot whenever `device.nodeName` is missing, empty or the legacy `ELOC_NONAME`, persists a migrated
SPIFFS name, also repairs the exact full-serial name produced by V1.63, and preserves names deliberately
assigned through the app. Compiled defaults are now
1 h WAV files, CPU 80/80 MHz, automatic light sleep off, APLL off and ICS-43434. The flasher records
keys only after a successful flash and offers explicit `--factory-reset-config` to erase SPIFFS;
ordinary reflashes retain commissioned settings. Focused AutoFlasher and target config coverage was
added for the five-digit suffix rule. V1.65 fixes a dangling `String::c_str()` pointer in JSON
serialization and treats invalid UTF-8/control-character SPIFFS names as recoverable corruption.
The `esp32dev-ei` environment builds successfully with this repair.

---

**SD card hot-swap (2026-08-01, V1.60 → V1.61), first hardware test done (removal path), re-test
owed — including whether IO4 is really the card-detect line.** Pulling the SD card left the device permanently broken until a reboot: a
re-inserted card was never recognised, and the log filled with `sdmmc_req: sdmmc_host_wait_for_event
returned 0x107` / `diskio_sdmmc: Check status failed (0x107)` roughly once a second. Root cause:
`SDCardSDIO::m_mounted` was set true in `init()` and **never cleared at runtime**, so `isMounted()`
kept reporting success, the FATFS volume stayed registered around a dead `sdmmc_card_t`, and the
driver never re-ran the card init sequence — a re-inserted card powers up unaddressed and cannot
answer against the stale RCA. The existing remount branch in `update()` was unreachable, could not
have worked anyway (`esp_vfs_fat_sdmmc_mount()` returns `ESP_ERR_INVALID_STATE` while the path is
still registered) and gave up permanently after `m_MAX_FAILED_MOUNTS = 4`. Each failed probe also
blocked the Bluetooth/status task ~1 s, and `checkSDCard()` returned OK off stale cached free space,
so the main loop respawned a doomed `wav_writer` task every second.

**Hardware:** the micro-SD socket's card-detect switch is on **PCA9557 IO expander IO4** (previously
documented as spare `NC_IO4`), so presence is a ~1 ms I2C read, not a 1 s SDMMC timeout.

**Implementation:** `ELOC_IOEXP::SD_DETECT` (IO4, configured as input, `getSdDetectLevel()`);
polarity via `SDCARD_DETECT_PRESENT_LEVEL` in `project_config.h` (default 0 = LOW means inserted).
`SDCardSDIO` takes the detect line as an injected `CardDetectFn` hook rather than including
ELOC_IOEXP — `lib/ElocHardware` already depends on `lib/sd_card`, so the reverse include would make
the PlatformIO LDF graph cyclic. The hook is installed from the `ElocSystem` constructor, which runs
before the first `mountSDCard()`, and **the polarity self-checks**: a successful mount is ground truth
for "card present", so if IO4 disagrees at that moment the line is declared unusable (warning logged)
and the code falls back to throttled filesystem probing. Detect readings are debounced (300 ms insert
settle, 100 ms removal). `m_mounted` (logical usability) is now split from `m_vfsRegistered` (teardown
still owed) so removal stops new file opens instantly while the volume is released only when safe.
Sequencing lives in `ElocSystem::handleSdCardHotSwap()` (BOOT → MOUNTED → STOPPING → ABSENT): on
removal it stops the wav writer, switches SD logging off, waits for `wav_recording_in_progress` to
clear (5 s deadline, re-armed rather than forced — unmounting under a task inside `fwrite()` is a
use-after-free), then unmounts; on insertion it re-mounts and raises a one-shot event that the main
loop turns into `handleSdCardRemounted()` (folder layout, session folder, EI results CSV, SD logging,
`prepareWavWriter()`, resume the interrupted recording mode). The inference thread's CSV writer is the
one asynchronous writer left, so it takes the new `SDCardSDIO::claimFs()/releaseFs()` mutex, which
`unmount()` also waits on (2 s, then proceeds — never unmounting would recreate the original bug).
`checkSDCard()` now fails when unmounted, which kills the doomed-task respawn loop.

**V1.61 (2026-08-01) — first hardware test found the teardown starved Bluetooth; fixed.** Pulling the
card while the app was connected dropped the SPP link (`BT_RFCOMM: port_rfc_closed ... res: 19`) and
the app could not reconnect. Two multi-second blockers, both running on the **BT Server task**:
(1) `Logging::esp_log_to_scard(false)` → `RotateFile::close()` takes the file lock with
**`portMAX_DELAY`** while another task sits in `vprintf()` → `rotate()` doing `getFileSize()`/
`rename()`/`remove()` on the dead card — measured **12 s** between "Redirecting log output BACK" and
"this should not be on sd card", and the RFCOMM close lands at the end of it; (2) every
`esp_vfs_fat_sdmmc_mount()` retry blocks **~4 s** in `sdmmc_init_ocr`/`send_op_cond`, repeatedly.
**Fixes:** the hot-swap poll moved out of `handleSystemStatus()` (BT task) into the **main loop**,
which can absorb seconds of blocking — while there is no card there is nothing to record — and which
also runs on duty-cycle timer wakes where the BT task never starts (this removes the limitation noted
below). New `Logging::abandonSdCard()` + `RotateFile::abandon(timeout)` detach the log hook **first**
(so no task can re-enter the dead file and any writer inside it drains within one SDMMC timeout), then
release the handle with a bounded lock wait and no `fsync()`/rotate; the STOPPING state retries it and
refuses to unmount until it succeeds. Mount retries now back off 2→4→8→10 s (measured from the end of
the blocking attempt), reset by a successful mount or a detect-line insert edge. `update()` holds the
(now recursive) fs mutex across mount/unmount so the BT task's `FwUpdateTransfer` call cannot race it.

**The IO4 detect line does not track presence on the test unit.** The 1 s retry cadence in the log
proves it validated at boot and still read "inserted" with the card physically out (a blind-mode
retry would have been 5 s apart) — so IO4 is either not the CD switch on this build, floating (the
PCA9557 has no internal pull-ups), or stuck low. The firmware now self-heals: if the filesystem says
the card is gone while the detect line says "inserted", the line is rejected **stickily for the boot**
(`m_cdRejected`) with an explicit error, and everything falls back to throttled polling.

**V1.62 (2026-08-02) — no card, no recording.** With an empty socket the app could still start
`recordOn_detectOff`: the firmware accepted it, set the mode, spun up I2S and reported "recording",
while `WAVFileWriter::open_file()` silently refused every file — a field tech would walk away from a
device that records nothing. `cmd_SetRecordMode` now resolves the requested wav mode first and rejects
any record-ON mode (`recordOn_detectOff`, `recordOn_detectOn`, `recordOnEvent`) with
`ESP_ERR_NOT_FOUND` + "No SD card - cannot start recording" **before touching any state**.
Detection-only modes stay allowed — they still raise LoRa alerts without a card. Also fixed in passing:
an invalid mode string used to `setError()` and then fall through to `setResultSuccess()`, reporting
success *and* enabling AI for a mode that was never accepted; it now returns immediately.
App side (5.41 working copy, not released): `SetCommandCompletedCallback` carries the device's `error`
string, and `DeviceActivity` shows it in a modal alert when a mode change is refused (previously the
screen just snapped back to "not recording" with no explanation).

**Owed (bench):** boot once with the card in and once with the socket empty and compare the
`SD card-detect (IO4) reads N` line — if N is identical, IO4 is not wired to the switch (check the
schematic for the pull-up); if it differs but is inverted, flip `SDCARD_DETECT_PRESENT_LEVEL`. Then:
pull the card while the app is connected and confirm the app **stays connected**; confirm the 0x107
spam stops, the LED shows not-mounted and `sdCardMounted=false`; re-insert and confirm a new session
folder plus resumed recording without a reboot. Known and accepted: the WAV being written at removal
is truncated with an unfixed header, and `VDD_SDIO` feeds both the card and PSRAM, so a rough insert
can still glitch PSRAM — no software remount covers that.

---

**DFS timebase corruption — "audio is unusable" was a false alarm (2026-08-01, V1.59), BUILD-VERIFIED,
hardware verification owed.** A 2-day `recordOn_detectOff` run logged
`I2S sample rate wrong: measured ~11300 Hz ... audio is unusable` continuously while the audio was
in fact real-time to 0.03 %. The meter was measuring a broken clock. Root cause: `esp_timer`
(TG0 LACT ÷ APB) and the FreeRTOS tick behind every `ESP_LOGx` stamp (CCOUNT) are **different** clocks,
both rescaled on every DFS transition by `on_freq_update()` through different paths — so under the
10/80 MHz `RECORDING_LOW_POWER` profile they ran **+41 %** and **+23 %** fast respectively and
disagreed with each other by a constant 0.876 (measured two independent ways). Proven against a third,
DFS-immune base: WAV filenames come from `gettimeofday()` → `esp_rtc_get_time_us()`, and 21 files ×
3600 s of audio landed in 75,622 s of wall clock. Audio is APLL-clocked, which `esp_pm` never touches.
**Fixes:** `RECORDING_LOW_POWER` → fixed 80 MHz; `C_ElocConfig_Default.cpuMinFrequencyMHZ` → 80 (so
`CONFIG_DEFAULT`, the profile in force during the duty-cycle GPS wait, is DFS-free too); rate meter +
clip throttle moved to `esp_rtc_get_time_us()`. Also: light sleep off by default **and** clamped
(`ALLOW_AUTOMATIC_LIGHT_SLEEP` + `clampLightSleep()`, required because a present config key beats the
compiled default on every already-provisioned unit); GPS-absent detection; two `WAVFileWriter` bugs.
**This closes issue #77**, whose 2024 stopwatch ratios (1.40 / 1.20 / 1.27) are these same skews and
whose closing summary recorded the opposite conclusion.
**Owed (bench):** flash V1.59 and confirm `CPU max 80 MHz, min 80 MHz, light sleep off`, steady
`I2S measured sample rate: ~16000 Hz` with no rate errors, a real `WorstCase` value, and no console
garbling. Note `cpuMinFrequencyMHZ` is **not** clamped, so existing units keep their stored `10` until
changed from the app. Full analysis: `README-I2S-Clock-And-LightSleep-Issues.md`.

**GPS cold first fix in duty cycle (2026-07-31, V1.58), HARDWARE-VERIFIED (outdoor cold start).** A unit commissioned into duty cycle *before* its first-ever GPS fix could stay fix-less
indefinitely. Every wake it did get ~30 s of blocking `waitForTimeSync` plus the awake window, and it
never stopped retrying (`lastGpsSyncS` stays 0, so `gpsClockFresh` is never true) — but no window was
ever long enough to finish a true COLD start (no ephemeris/almanac in VBAT-backed RAM ⇒ tens of seconds
of 50 bps nav-message decode, far more under canopy). The patient `GPS_FIRST_FIX_TIMEOUT_S` (180 s) path
existed for exactly this, but was gated on **`clockUnset`** (RTC still at firmware build time) — and the
app's `setTime` at commissioning advances the clock, so the gate went false the moment a field tech
connected, capping every burst at the 30 s trim ceiling. Fix: new `gpsFirstFixOutstanding()` helper
(`rtc_duty_cycle.magic` invalid **or** `lastGpsSyncS == 0`) replaces the `clockUnset` proxy in
`manageGpsWhileAwake`, and the boot-path blocking wake wait now uses `GPS_FIRST_FIX_TIMEOUT_S` while the
first fix is outstanding. Bounded by new `GPS_FIRST_FIX_PATIENT_WAKES` (10, counted via
`rtc_duty_cycle.bootCount`) so a no-sky install (bench indoors) can't hold itself awake +180 s every
cycle forever — after the cap it falls back to the 30 s ceiling and keeps retrying cheaply. No RTC struct
change, no magic bump. **Side effect to watch:** 24/7 (non-duty-cycle) units with no fix yet now burst
180 s/h instead of 30 s/h until the first fix lands.
**Verified on hardware (2026-07-31, timer wake, testELOC168):** patient path fired
(`waiting up to 180 s ... [cold first fix, patient]`) and the fix landed at **145.7 s** — over the old
30 s ceiling, so the pre-V1.58 build could not have acquired it (30 s boot wait + ~30 s burst ≈ 60 s of
GPS-on, then power-down and repeat forever). RTC epoch at wait start `1785471504` vs GPS `1785471650` =
exactly the 146 s elapsed, i.e. **the clock was already accurate** — the precise case where the old
`clockUnset` gate was false and disarmed the patient path. `boot took 148271 ms` confirms the wait sits
before the awake-timer reset (inference window intact); burst adoption then powered GPS down ~1 s into
the main loop (`gpsUp=0` at the first DSP-pre) before the first inference. Note during acquisition
`sats=0, sentences=0` the whole time (GGA/fix-gated counters) — only `chars` climbing shows progress.
**Owed (bench):** indoors/no-sky → confirm it stops being patient at wake 11
(`GPS_FIRST_FIX_PATIENT_WAKES`) and the cycle returns to `awakeDurationS + sleepDurationS`; confirm the
next wake after a fix logs "GPS time still fresh — skipping GPS".

---

**Duty-cycle bench session (2026-07-25, on V1.55): I2S clock + light sleep — full writeup in
[`README-I2S-Clock-And-LightSleep-Issues.md`](../README-I2S-Clock-And-LightSleep-Issues.md).**
Three independent bugs. (1) **I2S ran at exactly 8× the configured rate after every duty-cycle
wake** — BCK straight off the APLL with `mclk_div`×`bclk_div` (2×4) not in effect; surfaced only as
mass clipping + buffer overruns, no error anywhere. Fixed two ways, both hardware-verified:
`MicUseAPLL: false` (config) and an explicit `i2s_set_clk()` re-apply after install in
`I2SMEMSSampler::install_and_start()` (code, uncommitted). (2) **`rst:0x10 RTCWDT_RTC_RESET` reboots**
— the device hangs inside automatic light sleep and `esp_light_sleep_start()`'s own 1 s safety-net
watchdog resets it; only reachable when BT is down and not recording, i.e. idle-after-BT-timeout
(deterministic) and the duty-cycle GPS wait (~1 in 8). Costly because the reset is not a deep-sleep
wake, so the RTC duty-cycle state is memset and the unit silently drops out of duty cycling.
Mitigation `cpuEnableLightSleep: false`. (3) **OPEN:** `RECORDING_LOW_POWER` at min 10 MHz shows a
~8 % audio deficit and garbled console; not yet separated from possible `esp_timer` inaccuracy under
DFS — needs a stopwatch-vs-WAV-duration test. **Owed:** re-test the idle/BT-timeout reboot with light
sleep off; soak APLL=true + re-apply across many wakes; the stopwatch test; then decide the compiled
defaults for `MicUseAPLL` / `cpuEnableLightSleep` / `cpuMinFrequencyMHZ`.

---

**GPS live accuracy & clock-source markers (2026-07-21, V1.54), build-green, awaiting bench
verification.** Coordinated firmware + app (5.42) change; full record in `changelog.md`. The app's new
30 s status auto-refresh exposed a latch bug (indoor "Fix (0 sats)") and gaps in the GPS/time UI.
Firmware side: `getStatus` `hasFix` is now **live-gated** via new `ElocGPS::hasLiveFix()` (internal
`hasFix()` stays latched for last-known-position consumers); GPS is **held powered while a BT client is
connected** (`manageGpsWhileAwake`, 10 s failed-init backoff) so the app sees a live fix + HDOP; new
getStatus keys `gps.fixAge[s]`, `gps.hdop`, `device.timeSource`, `device.tzSource`; new persisted
`clockSource` marker in `rtc_duty_cycle_t`, stamped in `cmd_SetTime` (app) / `manageGpsWhileAwake` (gps,
last-writer-wins); `printStatus` JSON doc 1536→2048; `GPS_RESYNC_INTERVAL_S` restored 200→3600.
**Uptime fix (same V1.54):** `device.Uptime[h]` was really "wall-clock since firmware **build** time"
(`ESP32Time::getUpTimeSecs()` = `getEpoch() - boot_time_unix`, and `boot_time_unix` is init'd to the
build epoch on a fresh boot) — showed e.g. 3h44m right after a brownout reboot. Now `printStatus`
computes uptime from a new persisted `rtc_duty_cycle.firstBootEpochS` (deployment wall-clock, **survives
duty-cycle deep sleep**; stamped the first time the clock is set by app or GPS), falling back to
`getUpTimeSecs()` — itself rewritten to `esp_timer_get_time()` (true since-boot/wake) — when the clock
isn't set yet. On a duty-cycle wake this shows real deployment age, not the tiny awake-window value.
RTC magic bumped `0xE10CDC1E → 0xE10CDC5E` (appended `clockSource` + `firstBootEpochS`); OTA'd devices
re-init RTC once (TZ + deployment-uptime clock restart until next connect/GPS). **Owed (bench):** BT-connect GPS-hold + no burst power-off while connected; indoor latch
regression (never "Fix (0 sats)"); time suffix GPS↔Phone; disconnect → burst adoption/power-down; skip
auto time-sync when timeSource=gps & tzSource=gps; `printStatus` stack high-water under recording+LoRa+BT
(validate 2048); OTA magic-mismatch re-init (no boot loop); uptime shows deployment age after a
duty-cycle wake (not <1m). Keep the still-open V1.53 GPS cooperative-shutdown 24/7 soak running on V1.54
**including repeated BT connect/disconnect cycles** (this change adds init/deinit at BT boundaries +
extends GPS runtime).

---

**24/7-recording spontaneous-reboot fix — GPS task cooperative shutdown (2026-07-14, V1.53), awaiting
hardware soak verification.** Symptom: 24/7 recording (no LoRa, no schedule) panic-rebooted after
hours (~3 h and ~33 h observed). Serial backtrace (decoded, V1.52) pinned it: `assert failed:
vTaskPriorityDisinheritAfterTimeout tasks.c:5007 (pxTCB->uxMutexesHeld)` — `ElocGPS::deinit()` used
`vTaskDelete()` on the gps reader task, which sooner or later landed while the task was inside an
`ESP_LOGx` holding the **global esp_log mutex**, orphaning it. Every later log-lock take then timed
out (visible as interleaved binary garbage in the serial log), and once the dead task's freed TCB was
reused, the FreeRTOS priority-disinherit assert panicked the system. The GPS-burst timeout→deinit
path rolled these dice every `GPS_RESYNC_INTERVAL_S` (crash observed on ~17th and ~200th burst).
Fix in `lib/gps/ElocGPS.{hpp,cpp}`: cooperative shutdown handshake — `deinit()` raises
`mShutdownReq` (volatile), the task exits its loop within one 200 ms `uart_read_bytes` timeout, acks
via `mTaskExited` **as its last action** (no UART/log/parser access after the ack, since `deinit()`
then runs `uart_driver_delete`), and self-deletes; `deinit()` polls the ack up to 2 s with an
ESP_LOGE + force-delete fallback that should never fire. Also bumped `GPS_TASK_STACK` 3072→4096
(`%f` printf headroom, cheap insurance against the second suspect). Version → V1.53;
`esp32dev-ei` build green (RAM 31.2%). **Owed:** re-run the 24/7 bench soak (ideally with
`GPS_RESYNC_INTERVAL_S` at the 600 s test value or lower to accumulate burst cycles fast) past the
previous 33 h failure horizon, then restore the normal resync interval.

---

**Recording Scheduler — REVERTED out of the working tree (2026-07-21), not shipped.** The full
scheduler (firmware `lib/ElocScheduler/` NOAA sun-calc engine + config/RTC/sleep-path integration; app
`SchedulerActivity`/`ScheduleEntryEditorActivity`/`driver/Scheduler.kt` + DeviceSettings section) was
code-complete but never hardware-verified, and the user decided not to push it with the V1.54 GPS work.
It was surgically removed so V1.54 could ship alone: firmware entangled files (`main.cpp`,
`ElocConfig.*`, `ElocSystem.cpp`, docs, `test.cpp`, `README-Scheduler-Plan.md`) reverted to HEAD
(`995e7d5`), scheduler blocks stripped from `ElocStatus.hpp`/`ElocCommands.cpp`/`project_config.h`, the
RTC struct's scheduler fields dropped (magic re-landed at `0xE10CDC5E`); app entangled files
(`DeviceDriver.kt`, `DeviceSettingsActivity.kt`, `Command.kt`, `JsonHelper.kt`, manifest, layouts)
reverted to HEAD and the V1.54 hunks (`isIdle`, GPS/time parsing) re-applied. Both builds green after
removal (firmware RAM 30.5%, app `compileDebugKotlin` OK). **The complete pre-removal state is backed up**
as git-diff patches + copied untracked dirs under the session scratchpad
(`…/scratchpad/sched-removal-backup/{fw,app}/`) — reapply from there if the scheduler is resurrected.

---

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

- **`battery.avgIntervalMs` was silently dead; setRecordMode literals normalised** (2026-07-30,
  V1.57, build-green, not yet bench-verified). Two config/command-surface fixes:
  1. **`ElocConfig.cpp:267` read the key as `avgIntervalMrs`** (stray `r`) while
     `buildConfigFile()` writes it as `avgIntervalMs` (`:410`). Because `updateConfig()` merges the
     incoming `setConfig` JSON into a freshly built config doc and then re-runs `loadConfig()`,
     the misspelt lookup never matched — the value fell back to the compile default (`0`) and
     `writeConfig()` then persisted that `0` back to SD/SPIFFS. Net effect: the app's *Battery avg
     interval* knob round-tripped to 0 on every set and on every boot, so `Battery::avgIntervalMs`
     was permanently 0 and `readRawVoltage()` sampled its `avgSamples` ADC reads back-to-back with
     no spacing. No field config file can contain the misspelt key (nothing ever wrote it), so the
     plain rename needs no backward-compatibility alias. Still **reboot-only** to take effect —
     `Battery` latches it into a `const` member in its ctor init list (already documented in
     `README-Config-Restart-Semantics.md:19`).
  2. **`cmd_SetRecordMode` string literals + `getHelp` used mixed capitalisation** (`recordOn_DetectOFF`,
     help example `recordOff_DetectOn`) that matched neither the `RecState` enum spelling nor what
     the Android app sends (`recordOn_detectOff`). Harmless today — the dispatcher uses
     `strcasecmp`, the app parses state back from the numeric `code` not the string, and the web
     dashboard lowercases before matching (`ElocMapPage.tsx:627`) — but it made the published
     command reference wrong. Literals now match the enum exactly; `getHelp` lists all five accepted
     modes and states that matching is case-insensitive while `getStatus` reports the canonical
     spelling. `strcasecmp` kept, so any existing integration sending the old casing still works.

- **Stale-PROJECT_VER guard pre-script** (2026-07-19): the V1.52 `CMAKE_CONFIGURE_DEPENDS`
  mechanism (next entry) turned out **not to work under PlatformIO** — PlatformIO builds with
  SCons and runs the CMake configure only once, caching `PROJECT_VER` in
  `project_description.json`, so a `VERSION` bump in `project_config.h` kept stamping the *old*
  version into `esp_app_desc_t` (symptom: app's update dialog showed a bogus V1.53→V1.52
  "downgrade" for a freshly built V1.53 bin). New `tools/checkProjectVer.py` (wired as a
  `pre:` script in `platformio.ini`) compares the header's `VERSION` against the cached
  `project_version` and deletes `CMakeCache.txt`/`project_description.json` on mismatch,
  forcing a reconfigure so version bumps are self-correcting. Verified: mismatch path fixed the
  embedded version manually; match path builds clean (4.5 min incremental).

- **Real version embedded in app descriptor; project name deliberately unchanged** (2026-07-11,
  V1.52): top-level `CMakeLists.txt` now derives `PROJECT_VER` at configure time by parsing the
  `VERSION` define out of `include/project_config.h` (no duplicated version string; header added
  to `CMAKE_CONFIGURE_DEPENDS` so editing it re-triggers CMake), set before `project()` so
  ESP-IDF's override picks it up. The built `esp_app_desc_t` now carries
  `version = "ELOC-P_V1.52"` instead of the git-describe fallback, which alone fixes the app's
  false "RolledBack" report on same-version reflashes and the confusing update dialog (it used
  to show the git hash instead of a real version).
  `project()` was **kept as `idf-wav-sdcard`** (an earlier attempt to rename it to `ELOC` was
  reverted): `FirmwareUpdate.cpp`'s `validateImageFile()` project-name guard (added 2026-07-10)
  runs on the *currently deployed, unchangeable* firmware and rejects any incoming image whose
  `project_name` differs from its own — since it gates both the BT-staged path and the manual
  SD-swap fallback (same `updateFirmware()`/`STAGED_BIN` code path), renaming the project in
  V1.52 would have made this release un-installable on any device still running V1.51 or earlier,
  via either update path. Instead, `validateImageFile()` was given a minimal forward-compat
  allowlist: it now also accepts an incoming image whose `project_name == "ELOC"` (logged as
  "known future project name, accepting"), so a genuine project rename can ship cleanly in a
  later release once the whole fleet is on ≥V1.52.

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
- **Hardware-verify V1.54** (GPS live accuracy + uptime fix, scheduler-removed): BT-connect GPS-hold,
  indoor "Searching…" latch regression, time-source suffix, deployment uptime across reboot/wake, and
  confirm the reverted duty-cycle deep-sleep path still works — gates the push.
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
