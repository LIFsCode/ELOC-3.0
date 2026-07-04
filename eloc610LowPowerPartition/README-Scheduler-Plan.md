# ELOC Recording Scheduler — Firmware + App Implementation Plan

Status: **planned, not yet implemented** (2026-07-04). Tracked in `memory-bank/progress.md` → Desired Improvements.
App-side counterpart lives in `c:\Development\ELOC\App\ELOC-Control-Panel` (paths below).

## Context

The ELOC device today has only a simple **duty cycle**: `enable / sleepDurationS (60–900s) / awakeDurationS (20–120s)`, activated via `setRecordMode` with `recordOff_detectOn`, driving an ESP32 deep-sleep loop. Field teams need real scheduling: record at **specific times per day**, anchored optionally to **sunrise/sunset** (± offset), on **selectable weekdays** — e.g. nighttime-only for HEC crop-raid monitoring, working hours for chainsaw detection, or "10 min every hour" battery-stretching. The app should offer adjustable **preset examples**.

**Confirmed decisions:**
1. **One global mode** — the scheduler only decides *when* the device is awake vs deep-sleeping; the existing record/detect mode applies during every active window.
2. **Sunrise/sunset computed on the firmware** from GPS lat/lon; phone can push location over BT as fallback; time comes from the existing `setTime` sync.
3. **Up to 4 schedule entries**, each with start/end anchor (time or sunrise/sunset±offset), weekday mask, and optional within-window duty cycle.
4. **Backward compatible** — legacy `config.dutyCycle` keys keep working in both directions (old app ↔ new firmware, new app ↔ old firmware).

**Key verified constraint:** `jsonutils::merge()` ([lib/utils/src/jsonutils.cpp](lib/utils/src/jsonutils.cpp) lines 28-39) merges JSON *objects* recursively but replaces arrays wholesale (`dst.set(src)`). **Schedule entries must be keyed objects `e0`..`e3`, not an array**, so per-entry `setConfig` fragments merge through the existing `updateConfig()` path and stay under the 512-byte BT command limit. No new BT command needed. Old firmware silently drops unknown keys.

---

## 1. Config schema (`config.scheduler`) — freeze this first

```json
"config": {
  "dutyCycle":  { "enable": false, "sleepDurationS": 300, "awakeDurationS": 30 },
  "scheduler": {
    "enable": false,
    "e0": { "en": true,  "startRef": "sunset", "startMin": -60, "endRef": "sunrise", "endMin": 60, "days": 127, "dcAwakeS": 600, "dcSleepS": 3000 },
    "e1": { "en": false, "startRef": "time", "startMin": 0, "endRef": "time", "endMin": 1440, "days": 127, "dcAwakeS": 0, "dcSleepS": 0 },
    "e2": { ... }, "e3": { ... }
  }
},
"device": { "locationLat": -0.5891, "locationLon": 101.3431 }
```
(`999.0` = location unset)

- `startRef`/`endRef` ∈ `"time" | "sunrise" | "sunset"`. For `"time"`: `startMin` = minutes since local midnight 0..1439, `endMin` 0..1440 (24/7 = `0 → 1440`). For sun refs: `Min` = offset in minutes, clamped −240..+240.
- `days`: bitmask bit0=Mon … bit6=Sun, applied to the window's **start day**. Resolved `end ≤ start` ⇒ window crosses midnight.
- `dcAwakeS`/`dcSleepS`: within-window duty cycle; both > 0 to enable ("10 min every hour" = 600/3000); clamps awake 20..43200, sleep 60..43200 (legacy block keeps its old 20-120/60-900 clamps). `0/0` = continuous.
- **Size:** one full-entry `setConfig` ≈ 175 bytes (fits); all 4 + enable ≈ 580 bytes (doesn't). App writes up to 5 sequential `setConfig` fragments, **`enable:true` sent last** so a partial schedule never activates.
- **Precedence:** `scheduler.enable=true` ⇒ scheduler governs, `dutyCycle.enable` ignored (log warning if both). `scheduler.enable=false` (default; all existing devices) ⇒ bit-for-bit legacy behavior.

## 2. Firmware — new pure library `lib/ElocScheduler/`

Pure C++ (stdint + math.h only, **no Arduino/ESP includes**) so it runs in the native `generic_unit_tests` env. Each subsystem is its own PlatformIO lib; no `lib_deps` on `/lib` libraries (repo rule).

**`src/SunTimes.hpp/.cpp`** — NOAA solar equations, self-written (~90 lines, ±2 min):
```cpp
namespace suntimes {
bool compute(double latDeg, double lonDeg, int year, int month, int day,
             float tzOffsetHours, int16_t& sunriseMin, int16_t& sunsetMin);
} // false = polar day/night → caller falls back to 06:00/18:00
```

**`src/Scheduler.hpp/.cpp`**:
```cpp
enum class SchedRef : uint8_t { Time = 0, Sunrise, Sunset };
struct schedEntry_t  { bool en; SchedRef startRef; int16_t startMin;
                       SchedRef endRef; int16_t endMin; uint8_t days;
                       uint32_t dcAwakeS; uint32_t dcSleepS; };
struct schedConfig_t { bool enable; schedEntry_t e[4]; };
struct sunTimesMin_t { int16_t sunriseMin = 360; int16_t sunsetMin = 1080; bool valid = false; };
struct schedDecision_t { bool active; uint32_t secondsToNext; int8_t entryIdx; };

schedDecision_t evaluate(const schedConfig_t& cfg, const sunTimesMin_t& sun,
                         int64_t localEpochS, int64_t dcAnchorEpochS);
```
Algorithm: per enabled entry, resolve concrete windows with start-day = *today* and *yesterday* (covers midnight-crossers); weekday = `((localEpochS/86400)+3)%7` on start day; dc phase = `(now − windowStart) % (dcAwakeS+dcSleepS)`, active while `phase < dcAwakeS`. `active` = OR across entries (overlap = union; continuous beats dc). `secondsToNext` = min over upcoming boundaries, ≥1, capped 7 d.

## 3. Firmware — config plumbing

[lib/ElocHardware/src/ElocConfig.cpp](lib/ElocHardware/src/ElocConfig.cpp) (+ `.hpp`):
- Add `schedConfig_t schedulerConfig` to `elocConfig_T` (defaults: enable=false, entries en=false, time 0→1440, days=127, dc 0/0). Add `double locationLat/locationLon` (default 999.0) to `elocDeviceInfo_T`.
- Emit/parse in `buildConfigFile()` / `loadConfig()` (string refs parsed like `ParseMicChannel`).
- **`JSON_DOC_SIZE` 3072 → 4096** (line 40). Note `updateConfig()` holds two `static StaticJsonDocument<JSON_DOC_SIZE>` (lines 476/484) ⇒ +2 KB BSS — check RAM report after first build.
- `validateSchedulerConfig()`: clamp ranges; **fail-safe**: if `enable=true` but no effectively-active entry, force `enable=false` + warn (never sleep-forever). Extend the existing inference observation-window fit check to entries with `dcAwakeS > 0`.
- `const schedConfig_t& getEffectiveSchedule()`: returns `schedulerConfig` if enabled; else if `dutyCycleConfig.enable` returns a static **synthetic legacy config** (one entry: time 0→1440, days=127, dc = awake/sleepDurationS); else disabled. This single function is the whole legacy mapping.

## 4. Firmware — RTC state ([lib/ElocHardware/src/ElocStatus.hpp](lib/ElocHardware/src/ElocStatus.hpp))

Append to `rtc_duty_cycle_t` (keep new fields at end, ~+28 bytes) and **bump magic `0xE10CDC1E → 0xE10CDC2E`** so stale RTC content after OTA re-inits cleanly (existing invalid-magic path at main.cpp:657 handles it):
```c
float   lastLat, lastLon;   bool posValid;      // survives sleep, not power loss
int32_t sunCalcLocalDay;                        // localEpoch/86400 of cache (0=never)
int16_t sunriseMin, sunsetMin; bool sunValid;
int64_t scheduleAnchorEpochS;                   // armed-at epoch (legacy dc phase anchor)
```

## 5. Firmware — runtime integration ([src/main.cpp](src/main.cpp) lines ~633-1380)

- **`refreshSunTimes()`** — at boot after `readConfig()` and when local day changes. Position precedence: live GPS fix → RTC cache → config `device.locationLat/Lon` → none (06:00/18:00 default, `sunValid=false`).
- **`wallClockValid()`** = epoch ≥ 2025-01-01. Invalid ⇒ time/sun entries treated **always-active** (fail-safe: record, don't sleep); synthetic legacy entry still works via `scheduleAnchorEpochS` phase anchor.
- **`handleSleepCycleStateMachine()`** (line 784) becomes scheduler-driven: in `SLEEP_CYCLE_INFERENCE_ACTIVE`, call `evaluate()`; if `!active` → LoRa flush → `prepareCyclicDeepSleep(sleepS)` → `enterCyclicDeepSleep()`.
  `sleepS = min(secondsToNext, timeToNextLoraHeartbeatS, SCHED_MAX_SLEEP_CHUNK_S=6h)` − wake lead `max(5, sleepS/100)` s, floor 10 s. Heartbeat time derives from `rtc_duty_cycle.lastStatusLoraTimeS + upLinkIntervalS`. Chunked wakes self-correct RTC drift.
- **`handleOffWindowTimerWake()`** — new, in `app_main` right after config load (~line 1084): on timer wake, restore TZ, `refreshSunTimes()`, `evaluate()`; if inactive → **skip mic/AI/WAV init entirely** (preserve the fast-boot path — gate heavy init on wake cause per repo rules), send LoRa heartbeat if due (move `ElocLora` init above this hook), sleep the residual. If active → normal timer-wake boot; gate mode-restore (lines ~1338/1365) on `getEffectiveSchedule().enable` instead of `getDutyCycleConfig().enable`.
- **`prepareCyclicDeepSleep(uint32_t sleepS)`** — parameterize (replaces fixed `dcCfg.sleepDurationS` at line 743).
- **Button wake in off-window**: unchanged (maintenance boot with BT). Add: when BT times out (`bluetoothOffTimeoutSeconds`) with no connection and RTC shows an armed mode, re-arm `SLEEP_CYCLE_INFERENCE_ACTIVE` — a stray field button press must not kill the schedule.
- **GPS position persistence**: on live fix (in `manageGpsWhileAwake()` / boot burst ~1205-1225) store lat/lon into RTC; if config location unset or differs > 0.01°, update `gElocDeviceInfo` + `writeConfig()` (throttled — flash wear) so position survives full power loss.
- **`cmd_SetRecordMode`** ([lib/Commands/src/ElocCommands.cpp](lib/Commands/src/ElocCommands.cpp):465): gate on `getEffectiveSchedule().enable`; set `scheduleAnchorEpochS`.
- Constants in `include/project_config.h`: `SCHED_MAX_SLEEP_CHUNK_S`, `SCHED_WAKE_LEAD_MIN_S`, `SCHED_DEFAULT_SUNRISE_MIN 360`, `SCHED_DEFAULT_SUNSET_MIN 1080`.

## 6. Firmware — status (`printStatus()`, ElocCommands.cpp:116)

Bump `StaticJsonDocument<1024>` → 1536; add:
```json
"scheduler": { "enabled": true, "mode": "scheduler|legacyDutyCycle|off", "active": true,
               "entry": 0, "nextChangeEpoch": 1751500000,
               "sunriseMin": 371, "sunsetMin": 1092, "sunSource": "gps|rtc|config|default" }
```
LoRa payloads: no change.

## 7. App — driver layer

(App repo: `App/ELOC-Control-Panel`, package `de.eloc.eloc_control_panel`)

- **New `driver/Scheduler.kt`** (mirror `DutyCycle.kt` style): `enum StartRef {TIME, SUNRISE, SUNSET}`, `SchedulerEntry` with clamping setters, `Scheduler { present; enabled; entries[4] }` + status fields (`deviceSunriseMin/SunsetMin/sunSource/nextChangeEpoch`).
- **`driver/DeviceDriver.kt`**: key constants near lines 119-125; parse `payload/config/scheduler/*` (near line 1199) and `payload/scheduler/*` from status. Add **`JsonHelper.hasNode(path, json): Boolean`** — presence detection is how the app knows firmware supports the scheduler.
- **`data/Command.kt`**: `KEY_SCHEDULER_ENABLE` case in `createSetConfigPropertyCommand`; new `createSetSchedulerEntryCommand(index, entry, cb)` building the per-entry fragment (pattern: `createSetLocationCommand` at :477). Extend `createSetLocationCommand` to also send `locationLat`/`locationLon` (old firmware drops them harmlessly).

## 8. App — UI

- **`DeviceSettingsActivity` section**: "Duty Cycle" becomes **"Recording Schedule"**. If `scheduler.present`: master enable switch + "Configure schedule…" row → `SchedulerActivity`. If not (old firmware): render today's legacy duty-cycle rows unchanged (graceful degradation).
- **New `SchedulerActivity`** (`activities/themable/` + `activity_scheduler.xml`): header with device sun times + source ("Sunrise 06:11 · Sunset 18:12 · GPS"); warning banner + "Send phone location" button (reuse `LocationHelper`) when `sunSource=default` and any entry is sun-anchored; "Presets" button; **4 static entry cards** (no RecyclerView) with enable switch + summary line ("Mon–Fri · Sunset−1h → Sunrise+1h · 10 min every hour"); **Apply** sends changed fragments sequentially, `enable` last, then `refresh()`.
- **New `ScheduleEntryEditorActivity`** (`editors/eloc_settings/`, extends `BaseEditorActivity`): anchor dropdowns (Time/Sunrise/Sunset) for start & end; `MaterialTimePicker` for time anchors; −240..+240 min slider for sun offsets; weekday `ChipGroup` (7 checkable chips + "Every day"/"Weekdays" quick-toggles); duty-cycle switch revealing "Active for"/"Then pause for" sliders with `TimeSliderHelper` quick-picks. Saves in-memory; device write happens on SchedulerActivity Apply (one atomic fragment per entry).
- **Presets** (dialog; pre-fills entries locally, user tweaks, then Apply):
  1. **Record 24/7** (0→1440, all days)
  2. **Daytime** — sunrise → sunset, every day
  3. **Nighttime** — sunset → sunrise, every day (HEC crop-raid default)
  4. **Dawn & dusk chorus** — sunrise−60→sunrise+60 + sunset−60→sunset+60 (2 entries)
  5. **10 minutes every hour, 24/7** (dc 600/3000)
  6. **Working hours** — Mon–Fri 06:00→18:00 (chainsaw/logging detection)
  7. **Nighttime + hourly sampling** — sunset→sunrise with dc 600/3000 (battery-stretcher)
- Soft validation: warn on overlaps (firmware takes union) and on enable-with-no-entries. Strings → `values/strings.xml`; register activities in `AndroidManifest.xml`.

## 9. Edge cases (decided)

- Midnight-crossing windows: end ≤ start ⇒ +24 h; weekday bit on start day; today + yesterday evaluated.
- Invalid clock: time/sun entries always-active (record, never silently sleep); legacy dc keeps cadence via anchor.
- No position: 06:00/18:00 fallback, `sunSource:"default"`, app warns + one-tap location push.
- Scheduler enabled with no usable entries: validation forces off.
- DST: not supported — fixed hour offsets only (matches existing TZ model); document in both memory banks.

## 10. Implementation order

1. Freeze schema (§1).
2. **Firmware pure engine + native tests** (`lib/ElocScheduler`, `test/test_generic_scheduler/`) — verifiable off-hardware.
3. Firmware config plumbing (ElocConfig, ElocStatus/magic bump).
4. Firmware runtime integration (main.cpp, ElocCommands) + bench test.
5. App driver layer (Scheduler.kt, DeviceDriver.kt, Command.kt, JsonHelper.hasNode) — testable via existing CommandLineActivity against step-4 firmware.
6. App UI (section switch, SchedulerActivity, entry editor, presets, location push).
7. Compat matrix + multi-day soak.

Firmware-first: the app degrades gracefully against old firmware by design, and app testing needs a real scheduler-speaking device (no BT test automation exists).

## 11. Verification

1. **Native unit tests** (`pio test -e generic_unit_tests`, new `test/test_generic_scheduler/`): `suntimes::compute` vs NOAA reference (Sumatra ≈06:1x/18:1x; London summer/winter; Tromsø polar → false); `evaluate()` — simple window, midnight-crosser, Mon-only sunset→sunrise still active Tue 03:00, sun offsets, dc phase/flip timing, overlap union, `secondsToNext` exactness, all-disabled, `endMin=1440`.
2. **Target test**: extend `test/test_target_ElocConfig` — scheduler JSON round-trip, per-entry `updateConfig` merge, validation clamps (`pio test -e target_unit_selected_tests`).
3. **Builds**: `pio run -e esp32dev` and `-e esp32dev-ei`; check RAM delta from JSON doc bumps.
4. **Bench** (shortened times via `/sdcard/eloctest.txt`): window starting +3 min / 4 min long with dc 30/60 set from the app; serial monitor: sleep-until-start with wake lead, off-window heartbeat chunk wake, recording resumes with same session ID, sleep at window end, button wake → BT timeout → re-arm.
5. **Compat matrix**: old APK + new firmware (duty-cycle behaves as today); new APK + old firmware (legacy UI shown); OTA over live RTC state (magic bump → clean re-init).
6. **Soak**: sunset→sunrise preset ≥3 days; verify sun-time drift, LoRa heartbeats, file timestamps.

## Critical files

| Change | Path |
|---|---|
| Duty-cycle state machine, wake paths, GPS mgmt | `src/main.cpp` |
| Config structs/load/build/validate, `getEffectiveSchedule()` | `lib/ElocHardware/src/ElocConfig.{hpp,cpp}` |
| RTC struct + magic bump | `lib/ElocHardware/src/ElocStatus.hpp` |
| setRecordMode gate, printStatus | `lib/Commands/src/ElocCommands.cpp` |
| New scheduler engine + sun calc + native tests | `lib/ElocScheduler/`, `test/test_generic_scheduler/` |
| App parsing/keys | `App/ELOC-Control-Panel/.../driver/DeviceDriver.kt`, new `driver/Scheduler.kt` |
| App commands | `.../data/Command.kt` |
| App UI | `.../activities/themable/DeviceSettingsActivity.kt`, new `SchedulerActivity`, new `ScheduleEntryEditorActivity`, layouts, `strings.xml`, manifest |

After completion: update both repos' `memory-bank/activeContext.md` + `progress.md` (repo convention).
