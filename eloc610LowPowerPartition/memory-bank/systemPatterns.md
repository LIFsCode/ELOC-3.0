# System Patterns

## Architecture Overview

The ELOC firmware follows a **FreeRTOS multi-task architecture** running on an ESP32 dual-core MCU. The design separates time-critical audio capture from CPU-intensive AI processing across the two cores, with producer-consumer patterns for data flow and queue-based inter-task communication for control.

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32 Dual Core                       │
│                                                         │
│  Core 0                          Core 1                 │
│  ┌──────────────┐               ┌──────────────┐       │
│  │ I2S Read     │──┬──────────▶│ AI Inference  │       │
│  │ (Prio 10)    │  │            │ (Prio 7)      │       │
│  └──────────────┘  │            └───────┬───────┘       │
│         │          │                    │               │
│         ▼          │                    ▼               │
│  ┌──────────────┐  │            ┌──────────────┐       │
│  │ WAV Writer   │  │            │ LoRa Uplink  │       │
│  │ (Prio 8)     │  │            │ (Event Msg)  │       │
│  └──────┬───────┘  │            └──────────────┘       │
│         │          │                                    │
│         ▼          │                                    │
│  ┌──────────────┐  │                                    │
│  │ SD Card      │  │                                    │
│  │ (SDIO)       │  │                                    │
│  └──────────────┘  │                                    │
│                    │                                    │
│  ┌──────────────┐  │            ┌──────────────┐       │
│  │ Main Loop    │◀─┘            │ Bluetooth    │       │
│  │ (Control)    │◀─────────────▶│ Server       │       │
│  └──────────────┘               │ (Prio 1)     │       │
│                                 └──────────────┘       │
└─────────────────────────────────────────────────────────┘
```

## Key Design Patterns

### 1. Double Buffering (Producer-Consumer)

The core data pipeline uses double buffering for both WAV writing and AI inference:

- **I2SMEMSSampler** (producer) reads from I2S DMA and fills the active buffer
- When the active buffer is full, buffers are swapped atomically
- **WAVFileWriter** (consumer) writes the inactive buffer to SD card
- **EdgeImpulse** (consumer) processes the inactive buffer through the AI model

```
Buffer A ◄── I2S fills ──┐
                          │ swap when full
Buffer B ──► WAV write ──┘
```

Key fields: `buf_select`, `buf_count`, `buf_ready`

### 2. Singleton Pattern

Hardware subsystems use the singleton pattern via `GetInstance()`:
- `ElocSystem::GetInstance()` — I2C, IO expander, accelerometer, status LEDs
- `ElocLora::GetInstance()` — LoRaWAN radio
- `Battery::GetInstance()` — voltage monitoring

### 3. Queue-Based Command Dispatch

Control flow uses FreeRTOS queues for thread-safe communication:
- `rec_req_evt_queue` — recording mode change requests (from BT commands or button ISR)
- `rec_ai_evt_queue` — AI enable/disable requests (from BT commands)

The main loop polls these queues and orchestrates state transitions.

### 4. Callback Pattern for AI Inference

Due to namespace and static function pointer constraints in Edge Impulse, AI inference uses a callback pattern:
- `EdgeImpulse` class stores a `std::function<void()>` callback
- `ei_callback_func()` in main.cpp runs the actual inference pipeline
- The EI thread calls this callback repeatedly when status is `running`

### 5. Configuration Cascade

Configuration loading follows a priority cascade:
1. **SD card** (`/sdcard/eloc/*.config`) — highest priority
2. **SPIFFS** (`/spiffs/*.config`) — fallback
3. **Code defaults** — built-in defaults if no config file found

Runtime changes via Bluetooth are written back to SD card/SPIFFS.

### 6. Deferred Startup Pattern

AI thread startup is deferred by ~3 seconds after Bluetooth command to allow BT to serve follow-up status/config requests before the AI thread consumes CPU resources:
```cpp
g_ai_start_pending = true;
g_ai_deferred_start_time = esp_timer_get_time() + AI_DEFERRED_START_DELAY_US;
```

### 7. Dual-Storage Persistence (LoRaWAN)

LoRaWAN state uses a dual-storage strategy:
- **RTC memory** — session data (fast access, survives deep sleep, lost on power cycle)
- **NVS flash** — DevNonce/nonces (permanent, survives everything)

This ensures fast wake-up from deep sleep while maintaining DevNonce sequence across power cycles.

### 8. Duty-Cycle Deep Sleep State Machine

The device can alternate between deep sleep and active AI inference for extended battery life:

```
┌──────────┐    timer     ┌──────────────┐    awake time    ┌──────────┐
│Deep Sleep │───wakeup───▶│ Active: AI + │───elapsed──────▶│  Prepare │──▶ Deep Sleep
│(5 min)    │             │ LoRa (30s)   │                 │  Sleep   │
└──────────┘              └──────────────┘                 └──────────┘
     ▲                          │
     │         button press     │
     │         ───────────────▶ Normal boot (BT enabled, duty cycle off)
     │
     └──── repeat ─────────────────────────────────────────┘
```

State machine enum in `ElocStatus.hpp`:
```cpp
typedef enum {
    SLEEP_CYCLE_DISABLED = 0,       // Normal operation (no duty cycle)
    SLEEP_CYCLE_INFERENCE_ACTIVE,   // Awake, running AI inference
    SLEEP_CYCLE_PREPARING,          // Shutting down for sleep
    SLEEP_CYCLE_ENTERING_SLEEP      // About to enter deep sleep
} SleepCycleState_t;
```

Key functions in `main.cpp`:
- `handleWakeUpCause()` — Called FIRST in `app_main()`, determines timer vs button wake
- `prepareCyclicDeepSleep()` — Clean shutdown: sync detections → stop AI → stop I2S → turn off LEDs
- `enterCyclicDeepSleep()` — Calls `esp_deep_sleep_start()`, never returns
- `handleSleepCycleStateMachine()` — Called each main loop iteration, triggers sleep when awake time expires

### 9. RTC Persistence for Duty Cycle

State that survives deep sleep is stored in `RTC_DATA_ATTR` memory:

```cpp
RTC_DATA_ATTR rtc_duty_cycle_t rtc_duty_cycle;

typedef struct {
    uint32_t magic;                  // 0xE10CDC1E — validates RTC data
    uint32_t bootCount;              // Wake-up counter
    uint32_t totalDetections;        // Running detection count across cycles
    int64_t  lastEventLoraTimeS;     // Cooldown: when last event LoRa sent
    int64_t  lastDetectionTimeS;     // Cooldown: when last detection occurred
    uint8_t  eventState;             // EVENT_IDLE or EVENT_ACTIVE
    uint32_t detectionsSinceLastMsg; // Aggregation counter
    int64_t  eventStartTimeS;        // When current event session started
    char     sessionId[80];          // Session folder name for SD card persistence
} rtc_duty_cycle_t;
```

Session ID persistence ensures all wake cycles write to the same SD card folder and CSV file.

### 10. Timer Wake Fast-Boot Path

On timer wake (vs button wake or fresh boot), heavy subsystems are skipped to minimize boot time:

| Skipped on Timer Wake | Why |
|-----------------------|-----|
| `Battery::GetInstance()` | Not needed for 30s cycle |
| Firmware update check | Not during duty cycle |
| LoRa `delay(5000)` | Serial monitor helper, wastes 5s |
| LED startup animation | No one watching |
| LoRa buzzer feedback | No one listening |
| Bluetooth setup | Major time/power saving |
| PerfMonitor | Debug only |

Detection via `esp_sleep_get_wakeup_cause()` returning `ESP_SLEEP_WAKEUP_TIMER`.

## Component Relationships

```
project_config.h          ← Board-level pin/feature definitions
    │
    ├── config.h/cpp       ← I2S & accelerometer hardware config structs
    │
    ├── ElocSystem         ← HW singleton: I2C bus, IO expander, LIS3DH, LEDs
    │      │
    │      ├── Battery     ← ADC voltage monitoring
    │      └── StatusLED   ← LED state machine
    │
    ├── ElocConfig         ← JSON config read/write, runtime settings
    │
    ├── ElocLora           ← LoRaWAN (SX1262 via RadioLib)
    │      └── ElocLora_persistence  ← RTC + NVS session/nonce storage
    │
    ├── I2SMEMSSampler     ← I2S DMA audio capture
    │      ├── WAVFileWriter  ← Double-buffered SD card WAV output
    │      └── EdgeImpulse    ← AI inference buffer filling
    │
    ├── BluetoothServer    ← BT Classic SPP server
    │      └── ElocCommands   ← Command parser & handlers
    │
    ├── FirmwareUpdate     ← SD card OTA update
    │
    └── PerfMonitor        ← Optional CPU/memory stats
```

## Critical Implementation Paths

### Audio Recording Pipeline
```
I2S DMA → I2SMEMSSampler::read() → scale/shift samples → fill WAV double buffer
    → WAVFileWriter::write() → SD card (SDIO)
```

#### MicVolume2_pwr — Digital Gain via Bitshifting

The `MicVolume2_pwr` field (`micInfo_t.MicVolume2_pwr`) controls digital gain. The core formula in `I2SMEMSSampler::read()`:

```cpp
overall_bit_shift = (32 - I2S_BITS_PER_SAMPLE) - volume2_pwr;
processed_sample_32bit = raw_samples[i] >> overall_bit_shift;
processed_sample_16bit = processed_sample_32bit;  // truncate to 16-bit for WAV
```

- Raw I2S data is 24-bit MSB-justified in a 32-bit word → neutral correction shift = 8
- `volume2_pwr = 0` → shift right by 8 → neutral (no gain)
- Each `+1` = one fewer right-shift = **×2 amplitude (+6 dB)**
- Each `-1` = one more right-shift = **÷2 amplitude (−6 dB)**
- Defaults: **ICS-43434: −4** (÷16), **DMM-4026-B-I2S-R: −3** (÷8), **SPH0645: −3** (÷8)
- Clipping protection clamps 32-bit result to INT16 range before storing
- Configurable at runtime via JSON config `"mic"."MicVolume2_pwr"`

**⚠️ Automatic gain bug:** The disabled `ENABLE_AUTOMATIC_GAIN_ADJUSTMENT` code uses `volume2_pwr >>= 1` / `<<= 1` (halving/doubling the exponent) instead of `±1` increments — this causes non-linear jumps and is the likely source of the distortion issue.

### AI Inference Pipeline
```
I2S DMA → I2SMEMSSampler::read() → fill EI inference buffer
    → EdgeImpulse::microphone_inference_record() (blocking)
    → run_classifier() → threshold check → event logging + LoRa uplink
```

### Bluetooth Command Flow
```
BT Serial input → CmdParser → ElocCommands handler
    → Queue message to rec_req_evt_queue / rec_ai_evt_queue
    → Main loop processes → state change
```

## State Management

Recording state is managed through `WAVFileWriter::Mode`:
- `disabled` — no recording, I2S may be stopped
- `single` — record one session (AI-triggered)
- `continuous` — record indefinitely in segments

AI state is managed through `EdgeImpulse::Status`:
- `not_running` — inference stopped
- `running` — inference thread active

The main loop coordinates these states, ensuring I2S is started when either recording or AI needs it.

## Error Handling Patterns

- **SD card failures:** Checked before each recording start; recording disabled if card unavailable
- **LoRa failures:** Conservative auto-rejoin with minimum 10-minute interval; session restoration attempted first
- **AI failures:** Individual inference failures logged but don't halt the thread; retried next cycle
- **I2S failures:** `install_and_start()` retried in main loop until successful
