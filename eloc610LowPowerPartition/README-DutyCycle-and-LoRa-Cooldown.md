# Duty-Cycle Deep Sleep & LoRa Event Cooldown — Design & Implementation

> **Status:** ✅ Phase 1 (Duty Cycle) implemented & tested • Phase 2 (LoRa Cooldown) pending  
> **Date:** 2026-02-17  
> **Scope:** Two features to massively reduce LoRa messages and extend battery life  
> **See also:** `README-DutyCycle-BugFixes.md` for implementation details and bug fixes

---

## 1. Problem Statement

Illegal jungle chainsaw logging events typically last **several hours**. The current firmware detects every AI event and immediately sends a LoRa message for each one. This results in:

- **Hundreds of LoRa messages per day** during active logging
- **Rapid battery drain** (LoRa TX is power-hungry)
- **LoRa duty-cycle quota exhaustion** (regulatory limits on airtime)
- **No value in repeated messages** — rangers only need to know logging started, is ongoing, and stopped

---

## 2. Solution: Two Complementary Features

| Feature | Purpose | LoRa Reduction | Battery Savings |
|---------|---------|---------------|-----------------|
| **A: LoRa Event Cooldown** | Send max 1 event msg per cooldown period (e.g., 15 min) | ~95% fewer event msgs | Moderate (less TX) |
| **B: Duty-Cycle Deep Sleep** | Sleep/wake cycling (e.g., 5 min sleep, 30s awake) | Further reduction (only active 10% of time) | ~10-15× battery life |

Combined: Instead of ~500 LoRa messages/day → **~10-15 messages/day**.

---

## 3. Feature A: LoRa Event Cooldown

### 3.1 Concept

Instead of sending a LoRa message for every AI detection, use a state machine:

```
EVENT_IDLE  →  first detection  →  send EVENT_START  →  EVENT_ACTIVE
                                                              │
                              cooldown timer still running ←──┤
                                                              │
                              cooldown expired + still detecting → send EVENT_ONGOING
                                                              │
                              no detection for eventEndTimeoutS → send EVENT_END → EVENT_IDLE
```

### 3.2 Message Types

| Type | ID | When Sent | Payload |
|------|----|-----------|---------|
| `EVENT_MSG` (existing) | 1 | First detection → event start | timestamp + classifier labels + confidence |
| `EVENT_ONGOING_MSG` (new) | 2 | Every cooldown period during active event | timestamp + detection count since last msg |
| `EVENT_END_MSG` (new) | 3 | No detections for `eventEndTimeoutS` | timestamp + total detections + event duration |

### 3.3 Config Fields (added to `loraConfig_T`)

```cpp
uint32_t eventCooldownS;     // Min seconds between event LoRa msgs (default: 900 = 15 min)
uint32_t eventEndTimeoutS;   // Seconds without detection = event ended (default: 300 = 5 min)
```

- `eventCooldownS = 0` → legacy mode (send every detection, backward compatible)
- These values persist across deep sleep cycles via RTC memory

### 3.4 Implementation Location

- **`ElocLora.hpp`**: Add `EventState_t` enum, state machine vars, new message type enum values, save/restore methods
- **`ElocLora.cpp` → `ElocLoraLoop()`**: Replace direct `sendEventMessage()` with cooldown state machine logic
- **`ElocLora.cpp`**: Add `sendEventOngoingMessage()` and `sendEventEndMessage()` functions
- **`payload-formatters/radiolib-uplink-formatters.js`**: Decode new message types 2 and 3

---

## 4. Feature B: Duty-Cycle Deep Sleep

### 4.1 Concept

The device alternates between deep sleep and active inference:

```
┌──────────┐    timer     ┌──────────────┐    awake time    ┌──────────┐
│Deep Sleep │───wakeup───▶│ Active: AI + │───elapsed──────▶│  Prepare │──▶ Deep Sleep
│(5 min)    │             │ LoRa (30s)   │                 │  Sleep   │
└──────────┘              └──────────────┘                 └──────────┘
     ▲                          │
     │         button press     │
     │         ───────────────▶ Normal boot (duty cycle disabled)
     │
     └──── repeat ─────────────────────────────────────────┘
```

### 4.2 Config Fields (new `dutyCycleConfig_t` struct)

```cpp
typedef struct {
    bool     enable;           // Enable duty-cycle mode (default: false)
    uint32_t sleepDurationS;   // Deep sleep duration in seconds (default: 300 = 5 min)
    uint32_t awakeDurationS;   // Active inference duration in seconds (default: 30)
} dutyCycleConfig_t;
```

### 4.3 JSON Config

```json
{
  "config": {
    "dutyCycle": {
      "enable": false,
      "sleepDurationS": 300,
      "awakeDurationS": 30
    },
    "lorawan": {
      "loraEnable": true,
      "upLinkIntervalS": 3600,
      "loraRegion": "AS923_2",
      "eventCooldownS": 900,
      "eventEndTimeoutS": 300
    }
  }
}
```

All settings are individually exposed for development/testing. Later, the app will hide them behind a single "Patrol Interval" preset.

### 4.4 RTC-Persistent State

State that must survive deep sleep (stored in `RTC_DATA_ATTR`):

```cpp
RTC_DATA_ATTR struct {
    uint32_t magic;                  // Validation: 0xE10CDC1E
    uint32_t bootCount;              // Wake-up counter
    uint32_t totalDetections;        // Running detection count across cycles
    int64_t  lastEventLoraTimeS;     // Cooldown: when last event LoRa sent
    int64_t  lastDetectionTimeS;     // Cooldown: when last detection occurred
    uint8_t  eventState;             // EVENT_IDLE or EVENT_ACTIVE
    uint32_t detectionsSinceLastMsg; // Aggregation counter
    int64_t  eventStartTimeS;        // When current event session started
    char     sessionId[80];          // Session folder name for SD card persistence
} rtc_duty_cycle;
```

Note: The LoRaWAN session is already persisted in RTC memory (`rtc_lorawan_session_t rtc_session`) — this existing mechanism ensures no re-join is needed after each sleep cycle.

### 4.5 Sleep Cycle State Machine (in `main.cpp`)

```cpp
typedef enum {
    SLEEP_CYCLE_DISABLED = 0,       // Normal operation
    SLEEP_CYCLE_INFERENCE_ACTIVE,   // Awake, running AI inference
    SLEEP_CYCLE_PREPARING,          // Shutting down for sleep
    SLEEP_CYCLE_ENTERING_SLEEP      // About to enter deep sleep
} SleepCycleState_t;
```

### 4.6 Boot Path Optimization (Timer Wake vs Normal Boot)

On timer wake-up, skip non-essential initialization to save ~3-5 seconds:

| Component | Normal Boot | Timer Wake | Reason |
|-----------|:-----------:|:----------:|--------|
| `initArduino()` | ✅ | ✅ | Required |
| GPIO setup | ✅ | ✅ | Required for I2S mic |
| `ElocSystem::GetInstance()` | ✅ | ✅ | Needed for IO expander (LED control before sleep) |
| SPIFFS + SD card mount | ✅ | ✅ | Config file needed |
| `readConfig()` | ✅ | ✅ | Required |
| Logging setup | ✅ | ✅ | Useful for debug |
| `Battery::GetInstance()` | ✅ | ❌ Skip | Not needed for 30s cycle |
| Firmware update check | ✅ | ❌ Skip | Not during duty cycle |
| LoRa init | ✅ | ✅ | Session restores from RTC |
| LoRa `delay(5000)` | ✅ | ❌ Skip | Serial monitor helper, wasted 5s |
| LED startup animation | ✅ | ❌ Skip | No one watching |
| LoRa join buzzer | ✅ | ❌ Skip | No one listening |
| Bluetooth setup | ✅ | ❌ Skip | Major power/time saving |
| PerfMonitor | ✅ | ❌ Skip | Debug only |
| Button ISR | ✅ | ❌ Skip | Button is EXT0 wake source instead |
| Edge Impulse setup | ✅ | ✅ | Core function |
| I2S mic init | ✅ | ✅ | Core function |
| WAV writer | ✅ | ⚠️ Disabled mode | No recording in duty cycle |
| AI auto-start | ❌ Manual | ✅ Auto | Starts immediately on timer wake |

### 4.7 Key Functions to Add to `main.cpp`

```
handleWakeUpCause()            — Called FIRST in app_main(), sets gSleepCycleState
prepareCyclicDeepSleep()       — Clean shutdown (stop AI → stop I2S → save RTC state → configure timer)
enterCyclicDeepSleep()         — esp_deep_sleep_start()
handleSleepCycleStateMachine() — Called each main loop iteration, manages state transitions
```

---

## 5. Inference Timing Validation

The 30-second awake window must accommodate the AI observation window:

```
0s   ──── Wake from deep sleep, ESP32 boots, mount SD, read config
~3s  ──── I2S mic starts
~4s  ──── Mic settled, first useful inference
         ├── ~25 inference cycles at ~1s each
~9s  ──── Earliest possible event trigger (5 detections in ~5s)
~14s ──── Full 10s observation window satisfied
~29s ──── Last useful inference  
30s  ──── Go to deep sleep
```

**Validation rules (firmware auto-clamps):**
- `observationWindowS` must be < `awakeDurationS - 5` (5s startup overhead)
- `requiredDetections` must be ≤ `observationWindowS` (~1 inference/sec)
- `awakeDurationS` clamped to 20-120 seconds
- `sleepDurationS` clamped to 60-900 seconds

Detection window state is in regular RAM (zeroed after deep sleep) — this is **correct behavior**: each wake cycle is an independent sampling event with no stale data.

---

## 6. User-Facing Configuration (Future App Integration)

For development, all settings are individually exposed. For field deployment, the app will present:

**Setting 1: Monitoring Mode**
| Mode | Description |
|------|-------------|
| `Continuous` | Always listening & recording (current default) |
| `Patrol` | Periodic wake/listen/sleep |

**Setting 2: Patrol Interval** (only if Patrol mode)
| Option | Label | Sleep | Awake | Cooldown |
|--------|-------|-------|-------|----------|
| 2 min | High Alertness | 90s | 30s | 15 min |
| 5 min | Balanced (default) | 270s | 30s | 15 min |
| 10 min | Power Saver | 570s | 30s | 30 min |
| 15 min | Maximum Battery | 870s | 30s | 45 min |

All derived parameters computed by firmware. Impossible to create conflicting settings.

---

## 7. Implementation Status

### ✅ Phase 1: Duty-Cycle Deep Sleep — COMPLETE
1. ✅ Add `dutyCycleConfig_t` to `ElocConfig.hpp`
2. ✅ Add defaults + JSON load/save to `ElocConfig.cpp`
3. ✅ Add validation function to `ElocConfig.cpp`
4. ✅ Add RTC state struct to `ElocStatus.hpp`
5. ✅ Add wake-up handler + sleep state machine to `main.cpp`
6. ✅ Add conditional boot path (skip BT, Battery, etc. on timer wake)
7. ✅ Add `prepareCyclicDeepSleep()` and `enterCyclicDeepSleep()`
8. ✅ Add auto-start AI on timer wake
9. ✅ Test duty cycle operation
10. ✅ Bug fixes documented in `README-DutyCycle-BugFixes.md`

### Phase 2: LoRa Event Cooldown — PENDING
1. Add cooldown fields to `loraConfig_T` in `ElocConfig.hpp`
2. Add JSON load/save for cooldown fields
3. Add event state machine to `ElocLora.hpp/cpp`
4. Add RTC cooldown state save/restore
5. Add new message types and payload formatter decoders
6. Test cooldown in continuous mode
7. Test cooldown across duty-cycle sleep cycles

### Phase 3: App Integration — FUTURE
1. Map `patrolIntervalS` preset to derived config values
2. Hide individual settings behind simplified UI

---

## 8. Files Modified

### Phase 1 (Duty Cycle) — Complete

| File | Changes |
|------|---------|
| `lib/ElocHardware/src/ElocConfig.hpp` | `dutyCycleConfig_t` struct |
| `lib/ElocHardware/src/ElocConfig.cpp` | Defaults, JSON read/write, validation |
| `lib/ElocHardware/src/ElocStatus.hpp` | `rtc_duty_cycle_t` struct, `SleepCycleState_t` enum, global externs |
| `lib/ElocHardware/src/ElocStatus.cpp` | RTC variable definitions |
| `src/main.cpp` | Wake handler, state machine, boot branching, sleep functions, session persistence |
| `lib/Commands/src/ElocCommands.cpp` | Duty cycle activation on `recordOff_detectOn` |
| `lib/ElocHardware/src/ElocSystem.cpp` | Skip LED animation on timer wake |
| `lib/ElocHardware/src/ElocLora.cpp` | Skip delay + buzzer on timer wake |
| `lib/esp32Time/src/ESP32Time.cpp` | Added `setBuildTimeOnly()` and `setTimeZone()` |
| `lib/esp32Time/src/ESP32Time.h` | Method declarations |

### Phase 2 (LoRa Cooldown) — Pending

| File | Changes |
|------|---------|
| `lib/ElocHardware/src/ElocConfig.hpp` | Extend `loraConfig_T` with cooldown fields |
| `lib/ElocHardware/src/ElocConfig.cpp` | JSON for cooldown fields |
| `lib/ElocHardware/src/ElocLora.hpp` | Event state machine, new msg types |
| `lib/ElocHardware/src/ElocLora.cpp` | Cooldown logic, new message functions |
| `payload-formatters/radiolib-uplink-formatters.js` | Decode msg types 2 and 3 |
