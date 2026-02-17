# Duty-Cycle Deep Sleep — Implementation & Bug Fixes

> **Date:** 2026-02-17  
> **Status:** Phase 1 implemented + bug fixes applied  
> **Design doc:** See `README-DutyCycle-and-LoRa-Cooldown.md` for the full design plan

---

## Part 1: How the Duty Cycle Was Implemented

### Overview

The duty cycle feature makes the ELOC device alternate between **deep sleep** and **active AI inference**, extending battery life ~10-15× while still detecting events periodically.

```
┌──────────┐    timer     ┌──────────────┐    awake time    ┌──────────┐
│Deep Sleep │───wakeup───▶│ Active: AI + │───elapsed──────▶│  Prepare │──▶ Deep Sleep
│(5 min)    │             │ LoRa (30s)   │                 │  Sleep   │
└──────────┘              └──────────────┘                 └──────────┘
     ▲                          │                                │
     │         button press     │                                │
     │         ───────────────▶ Normal boot (BT, full init)      │
     │                                                           │
     └──── repeat ───────────────────────────────────────────────┘
```

### Configuration

Added `dutyCycleConfig_t` struct to `ElocConfig.hpp`:

```cpp
typedef struct {
    bool     enable;           // Enable duty-cycle mode (default: false)
    uint32_t sleepDurationS;   // Deep sleep duration in seconds (default: 300 = 5 min)
    uint32_t awakeDurationS;   // Active inference duration in seconds (default: 30)
} dutyCycleConfig_t;
```

JSON config (`/sdcard/eloc/config.json`):
```json
{
  "dutyCycle": {
    "enable": true,
    "sleepDurationS": 300,
    "awakeDurationS": 30
  }
}
```

**Files:** `lib/ElocHardware/src/ElocConfig.hpp` (struct + defaults), `lib/ElocHardware/src/ElocConfig.cpp` (JSON read/write + validation)

### RTC-Persistent State

State that survives deep sleep is stored in `RTC_DATA_ATTR` memory (`lib/ElocHardware/src/ElocStatus.hpp`):

```cpp
RTC_DATA_ATTR rtc_duty_cycle_t rtc_duty_cycle;

typedef struct {
    uint32_t magic;                  // 0xE10CDC1E — validates RTC data
    uint32_t bootCount;              // Wake-up counter
    uint32_t totalDetections;        // Running detection count across all cycles
    int64_t  lastEventLoraTimeS;     // LoRa cooldown: last event msg sent (epoch)
    int64_t  lastDetectionTimeS;     // LoRa cooldown: last detection (epoch)
    uint8_t  eventState;             // EVENT_IDLE(0) or EVENT_ACTIVE(1)
    uint32_t detectionsSinceLastMsg; // Aggregation counter
    int64_t  eventStartTimeS;        // When current event started (epoch)
    char     sessionId[80];          // Session folder name for SD card persistence
} rtc_duty_cycle_t;
```

### Sleep Cycle State Machine

Added to `src/main.cpp`:

```cpp
typedef enum {
    SLEEP_CYCLE_DISABLED = 0,       // Normal operation (no duty cycle)
    SLEEP_CYCLE_INFERENCE_ACTIVE,   // Awake, running AI inference
    SLEEP_CYCLE_PREPARING,          // Shutting down for sleep
    SLEEP_CYCLE_ENTERING_SLEEP      // About to enter deep sleep
} SleepCycleState_t;
```

### Key Functions Added to `main.cpp`

| Function | Purpose |
|----------|---------|
| `handleWakeUpCause()` | Called **first** in `app_main()`. Checks `esp_sleep_get_wakeup_cause()`, sets `gIsTimerWake`, validates RTC magic, increments `bootCount` |
| `prepareCyclicDeepSleep()` | Clean shutdown: sync detections → stop AI → stop I2S → turn off LEDs → configure timer + button wake |
| `enterCyclicDeepSleep()` | Calls `esp_deep_sleep_start()`. Never returns. |
| `handleSleepCycleStateMachine()` | Called each main loop iteration. Compares elapsed awake time vs `awakeDurationS`, triggers sleep when expired |

### Boot Path Optimization (Timer Wake vs Normal Boot)

On timer wake, heavy subsystems are skipped to minimize boot time:

| Component | Normal Boot | Timer Wake | Why |
|-----------|:-----------:|:----------:|-----|
| `Battery::GetInstance()` | ✅ | ❌ | Not needed for 30s cycle |
| Firmware update check | ✅ | ❌ | Not during duty cycle |
| Bluetooth setup | ✅ | ❌ | Major time/power saving |
| PerfMonitor | ✅ | ❌ | Debug only |
| LoRa `delay(5000)` | ✅ | ❌ | Serial monitor helper |
| LED startup animation | ✅ | ❌ | No one watching |
| LoRa buzzer feedback | ✅ | ❌ | No one listening |
| SD card + config | ✅ | ✅ | Required |
| LoRa init (session restore) | ✅ | ✅ | Required |
| Edge Impulse + I2S | ✅ | ✅ (auto-start) | Core function |

### Activation Flow

1. **User boots device** → normal boot (BT enabled, duty cycle DISABLED)
2. **User sends `recordOff_detectOn` via Bluetooth** → `ElocCommands.cpp` activates duty cycle:
   - Sets `gSleepCycleState = SLEEP_CYCLE_INFERENCE_ACTIVE`
   - Sets `gDutyCycleActivationTimeUS = esp_timer_get_time()`
   - Starts AI inference
3. **After `awakeDurationS` elapses** → `handleSleepCycleStateMachine()` triggers sleep
4. **Timer fires** → ESP32 wakes, `handleWakeUpCause()` detects timer wake, auto-starts AI
5. **Repeat** until battery dies or button press triggers normal boot

### Session Persistence Across Sleep Cycles

The session folder and CSV file created on the first boot are reused across all wake cycles:
- Session ID saved to `rtc_duty_cycle.sessionId` on first creation
- On timer wake, restored from RTC → `session_folder_created = true`
- CSV file opened with `FILE_APPEND` (no header re-creation)

### Time Preservation Across Sleep Cycles

The ESP32's RTC hardware maintains accurate epoch time during deep sleep. On timer wake:
- `setBuildTimeOnly()` sets internal references without overwriting RTC time
- `setTimeZone()` restores the `TZ` environment variable (lost during deep sleep)
- Result: CSV timestamps and LoRa messages have correct local time

### Validation

`validateDutyCycleConfig()` in `ElocConfig.cpp` auto-clamps values:
- `awakeDurationS`: 20–120 seconds
- `sleepDurationS`: 60–900 seconds
- Observation window must fit within awake duration minus startup overhead

### Files Modified for Implementation

| File | Changes |
|------|---------|
| `lib/ElocHardware/src/ElocConfig.hpp` | `dutyCycleConfig_t` struct, extended `loraConfig_T` with cooldown fields |
| `lib/ElocHardware/src/ElocConfig.cpp` | Defaults, JSON read/write, validation function |
| `lib/ElocHardware/src/ElocStatus.hpp` | `rtc_duty_cycle_t` struct, `SleepCycleState_t` enum, global externs |
| `lib/ElocHardware/src/ElocStatus.cpp` | RTC variable definitions |
| `src/main.cpp` | Wake handler, state machine, boot branching, sleep functions, session persistence |
| `lib/Commands/src/ElocCommands.cpp` | Duty cycle activation on `recordOff_detectOn` command |
| `lib/ElocHardware/src/ElocSystem.cpp` | Skip LED animation on timer wake |
| `lib/ElocHardware/src/ElocLora.cpp` | Skip delay + buzzer on timer wake |
| `lib/esp32Time/src/ESP32Time.cpp` | Added `setBuildTimeOnly()` and `setTimeZone()` methods |

---

## Part 2: Bug Fixes (found during testing)

The following bugs were discovered by analyzing serial logs from two consecutive duty cycle wake cycles.

---

## Bug 1: `totalDetections` Always 0

**Problem:** The `rtc_duty_cycle.totalDetections` counter was never updated before entering deep sleep. Each wake cycle ran AI inference and counted detections locally in `edgeImpulse.get_detectedEvents()`, but this value was lost when the ESP32 entered deep sleep because it was never synced to RTC memory.

**Symptom:** LoRa status messages and logs always showed `totalDetections: 0` regardless of how many events were detected across cycles.

**Fix** (`src/main.cpp` — `prepareCyclicDeepSleep()`):
```cpp
// Sync detection count to RTC before sleep
#ifdef EDGE_IMPULSE_ENABLED
{
    uint32_t thisWakeDetections = edgeImpulse.get_detectedEvents();
    rtc_duty_cycle.totalDetections += thisWakeDetections;
    ESP_LOGI(TAG, "This wake detections: %u, total across cycles: %u",
        thisWakeDetections, rtc_duty_cycle.totalDetections);
}
#endif
```

---

## Bug 2: New Session Folder Created on Every Wake Cycle

**Problem:** On each timer wake, `gSessionIdentifier` and `session_folder_created` were reset (they're regular RAM variables, not RTC-persistent). This caused every wake cycle to create a **new session folder** with a new timestamp-based name and a new CSV file with headers, scattering detections across multiple folders.

**Symptom:** SD card had multiple session folders like `LoraTest_1771328513843`, `LoraTest_1771328687686`, `LoraTest_1771328861234` — each with its own CSV file containing only the detections from that single wake cycle.

**Expected:** All duty cycle wakes should share **one session folder** and **append** to the same CSV file.

### Fix (3 parts):

**Part A** — Add `sessionId` field to RTC struct (`lib/ElocHardware/src/ElocStatus.hpp`):
```cpp
typedef struct {
    // ... existing fields ...
    char     sessionId[80];          // Session folder name, persisted across sleep cycles
} rtc_duty_cycle_t;
```

**Part B** — Save session ID to RTC when creating session folder (`src/main.cpp` — `createSessionFolder()`):
```cpp
session_folder_created = true;

// Persist session ID in RTC memory for duty cycle wake continuity
strncpy(rtc_duty_cycle.sessionId, gSessionIdentifier.c_str(), sizeof(rtc_duty_cycle.sessionId) - 1);
rtc_duty_cycle.sessionId[sizeof(rtc_duty_cycle.sessionId) - 1] = '\0';
```

**Part C** — Restore session ID from RTC on timer wake (`src/main.cpp` — `app_main()`):
```cpp
if (gIsTimerWake && rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC 
    && rtc_duty_cycle.sessionId[0] != '\0') {
    gSessionIdentifier = String(rtc_duty_cycle.sessionId);
    session_folder_created = true;  // Folder already exists from initial session

    #ifdef EDGE_IMPULSE_ENABLED
    // Build the CSV filename so save_inference_result_SD() can append directly
    // without calling create_inference_result_file_SD() which would overwrite with headers
    ei_results_filename = "/sdcard/eloc/" + gSessionIdentifier + "/EI-results-ID-...csv";
    inference_result_file_SD_available = true;  // Skip header re-creation, just append
    #endif
}
```

**How it works:** On the first boot (BT-triggered), `createSessionFolder()` creates the folder and saves the session ID to RTC. On subsequent timer wakes, the session ID is restored from RTC, `session_folder_created` is set to `true` (skipping folder creation), and the CSV filename is reconstructed with `inference_result_file_SD_available = true` so `save_inference_result_SD()` opens the existing file with `FILE_APPEND` instead of creating a new one with headers.

---

## Bug 3: Wrong Timezone on Timer Wake (CSV Timestamps in UTC)

**Problem:** The `TZ` environment variable lives in regular RAM and is lost during deep sleep. On fresh boot, `initBuildTime()` calls `setTimeZone(TIMEZONE_OFFSET)` which sets the `TZ` env var. But on timer wake, `setBuildTimeOnly()` was called instead — which does NOT set the timezone. As a result, `getLocalTime()` / `getTimeDate()` defaulted to UTC.

**Symptom:** CSV timestamps showed UTC time (e.g., `11:44:29`) instead of local Bangkok time (e.g., `18:44:29`). The RTC epoch value itself was correct — only the timezone interpretation was wrong.

**Fix** (`src/main.cpp` — time initialization on timer wake):
```cpp
if (gIsTimerWake) {
    timeObject.setBuildTimeOnly(__TIME_UNIX__);
    timeObject.setTimeZone(TIMEZONE_OFFSET);  // TZ env var lost during deep sleep
}
```

---

## Bug 4: Only ~15 Seconds of Inference Instead of 30

**Problem:** The 30-second awake timer (`gDutyCycleActivationTimeUS`) was set at the very beginning of boot in `handleWakeUpCause()`. But boot initialization takes ~15 seconds (I2C scan, LoRa init, SD card mount, config load, I2S setup, etc.), leaving only ~15 seconds for actual AI inference.

The biggest time waster was a **`delay(5000)`** in `ElocLora::init()` intended for developers to switch to the serial monitor — completely unnecessary on timer wake.

### Fix (2 parts):

**Part A** — Skip LoRa 5-second delay on timer wake (`lib/ElocHardware/src/ElocLora.cpp`):
```cpp
if (!gIsTimerWake) {
    delay(5000);  // Give time to switch to serial monitor (skip on duty cycle wake)
}
```

**Part B** — Reset awake timer after all init completes (`src/main.cpp` — right before main loop):
```cpp
// For timer wake: reset the awake duration timer AFTER all initialization completes
// so the full awakeDurationS is available for actual inference, not eaten by boot overhead
if (gIsTimerWake) {
    gDutyCycleActivationTimeUS = esp_timer_get_time();
    ESP_LOGI(TAG, "Timer wake: awake timer reset after init (boot took %lld ms)",
        gDutyCycleActivationTimeUS / 1000);
}
```

**Result:** The full 30-second `awakeDurationS` now counts from when inference actually starts, not from the beginning of boot.

---

## Bug 5: LEDs Stuck ON During Deep Sleep

**Problem:** The LEDs on the ELOC board are controlled via a **PCA9557 I2C IO expander**, which retains its output register state even after the ESP32 enters deep sleep. If LEDs were blinking or ON at the moment sleep was entered, they stayed ON permanently during the entire sleep period, wasting battery.

**Fix** (`src/main.cpp` — `prepareCyclicDeepSleep()`):
```cpp
// Turn off LEDs before sleep — IO expander retains register state during deep sleep
gpio_set_level(STATUS_LED, 0);
gpio_set_level(BATTERY_LED, 0);
if (ElocSystem::GetInstance().hasIoExpander()) {
    ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_STATUS, false);
    ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_BATTERY, false);
}
```

Both direct GPIO LEDs and IO expander LEDs are explicitly turned off before entering deep sleep.

---

## Files Changed

| File | Changes |
|------|---------|
| `lib/ElocHardware/src/ElocStatus.hpp` | Added `char sessionId[80]` to `rtc_duty_cycle_t` struct |
| `lib/ElocHardware/src/ElocLora.cpp` | Skip `delay(5000)` on timer wake |
| `src/main.cpp` | All other fixes (totalDetections sync, session restore, timezone, awake timer reset, LED turn-off) |

---

## Testing Checklist

- [ ] Flash firmware and trigger duty cycle via BT (`recordOff_detectOn`)
- [ ] Verify only ONE session folder is created across multiple wake cycles
- [ ] Verify CSV file grows (appends) across wake cycles, not overwritten
- [ ] Verify `totalDetections` in logs is cumulative across cycles
- [ ] Verify CSV timestamps are in correct local timezone
- [ ] Verify inference runs for the full `awakeDurationS` (30s), not 15s
- [ ] Verify LEDs are OFF during deep sleep
- [ ] Verify button press still wakes from deep sleep to normal mode
