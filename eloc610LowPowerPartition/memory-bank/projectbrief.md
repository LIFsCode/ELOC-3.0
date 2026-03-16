# ELOC 3.0 — Project Brief

## Project Overview

**ELOC** (Electronic Listening and Observation Console) is an open-source, ESP32-based wildlife acoustic monitoring device developed for the **International Elephant Project** in partnership with **Wildlife Conservation International (WCI)**. The firmware runs on the ELOC 3.0 hardware platform and is designed for long-duration, low-power field deployment to continuously record environmental audio and optionally perform on-device AI inference to detect target animal sounds (e.g., elephant vocalizations).

**Repository:** [https://github.com/LIFsCode/ELOC-3.0](https://github.com/LIFsCode/ELOC-3.0)  
**License:** MIT  
**Primary Contributors:** Fabian Lindner, Owen O'Hehir, and others

---

## Core Purpose & Problem Statement

Wildlife conservation efforts require persistent acoustic monitoring in remote jungle and savanna environments. Traditional approaches demand expensive, bulky equipment or manual observation. ELOC addresses this by providing:

- A **low-cost, battery-powered, field-deployable** acoustic recorder
- **On-device AI sound classification** using Edge Impulse models to detect target species
- **LoRaWAN connectivity** for remote status reporting and event alerts without cellular infrastructure
- **Bluetooth Classic control** via a companion Android app (ELOC Control Panel) for device configuration, recording control, and status monitoring

---

## Dual-Partition Architecture

The ELOC firmware uses a **dual-partition OTA scheme** on ESP32 with 16MB flash:

| Partition    | Offset     | Size     | Purpose |
|-------------|-----------|----------|---------|
| `partition0` | 0x10000   | ~8 MB    | High-power / Bluetooth bootstrap partition |
| `partition1` | 0x7F0000  | ~8 MB    | **This firmware** — Low-power recording partition |
| `nvs`        | 0x9000    | 20 KB    | Non-volatile storage (device identity, LoRaWAN keys) |
| `spiffs`     | 0xFD0000  | 192 KB   | Configuration file storage |

**This codebase is the low-power recording partition (`partition1`).** It cannot run standalone — it requires the high-power partition for initial Bluetooth setup and boot orchestration. This partition is where ongoing feature development and refinements are focused.

---

## Key Functional Requirements

### 1. Continuous Audio Recording
- Record audio from I2S MEMS microphones to SD card as WAV files
- Supported microphones: **TDK InvenSense ICS-43434** (primary), **PUI DMM-4026-B-I2S-R**, SPH0645
- Configurable sample rates (4 kHz – 51.6 kHz, default 16 kHz), 24-bit resolution
- Configurable recording duration per file (default 60 seconds)
- Double-buffered WAV writing with FreeRTOS task-based architecture
- Three recording modes: **disabled**, **single** (triggered), **continuous**

### 2. On-Device AI Inference (Edge Impulse)
- Optional build flag (`EDGE_IMPULSE_ENABLED`) to include Edge Impulse TFLite models
- Both **continuous** and **non-continuous** inference modes
- Configurable confidence threshold, observation window, and required detections count
- Detection events logged to CSV on SD card
- Detections can trigger sound recording (single-shot mode) and LoRa event messages
- AI processing runs on a dedicated CPU core (Core 1) to avoid impacting audio capture (Core 0)

### 3. LoRaWAN Communication
- LoRaWAN 1.1 via **RadioLib 7.2.1** with **SX1262** radio
- OTAA (Over-The-Air Activation) with TTN (The Things Network) support
- Periodic status uplink messages (battery, recording status, AI detections)
- Event-triggered uplink on sound detection
- Downlink command support
- **Session persistence** via RTC memory (survives deep sleep) and **DevNonce persistence** via NVS (survives power loss)
- Configurable region (AS923_2 default), uplink interval, enable/disable

### 4. Bluetooth Classic Control Interface
- Bluetooth Serial (SPP) for communication with the **ELOC Control Panel** Android app
- Command-based protocol using `CmdParser` library
- Commands for: start/stop recording, get/set configuration, get status, set time, firmware update, AI control
- Configurable auto-off timeout, enable-at-start, enable-on-tapping (accelerometer double-tap)
- Can be disabled during recording to save power

### 5. Power Management
- ESP32 dynamic frequency scaling (DFS) with configurable min/max CPU frequencies
- Automatic light sleep support during idle periods
- Deep sleep capability with GPIO wake-up (button press)
- Battery voltage monitoring with configurable read intervals and averaging
- USB-only mode (no-battery mode) for bench testing
- Status LED indication for system state and battery level

### 6. Device Configuration
- JSON-based configuration stored on SD card and/or SPIFFS
- Configuration includes: microphone settings, recording parameters, CPU frequencies, Bluetooth behavior, logging, battery monitoring, LoRa settings, AI inference parameters, intruder detection
- Runtime configuration changes via Bluetooth commands
- Per-device factory settings in NVS (hardware generation, revision, serial number, LoRaWAN keys)

### 7. Firmware Update
- OTA firmware update via SD card (place firmware binary on SD card, device auto-detects and flashes)
- Dual OTA partition scheme allows rollback

### 8. Ancillary Hardware Support
- **LIS3DH accelerometer** (I2C) for intruder/tamper detection and double-tap wake-up
- **PCA9557 I/O expander** (I2C) for expanded GPIO
- **Buzzer** for audio feedback (boot, Bluetooth connect/disconnect, LoRa join status)
- **SD card** via SDIO interface for high-speed data logging
- **Status LEDs** for visual system state indication

---

## Technical Stack

| Component | Technology |
|-----------|-----------|
| MCU | ESP32 (dual-core, 240 MHz max, PSRAM) |
| Framework | ESP-IDF + Arduino (hybrid via PlatformIO) |
| Build System | PlatformIO |
| Language | C++ (C++11/14) |
| RTOS | FreeRTOS |
| AI Framework | Edge Impulse (TFLite Micro) |
| LoRa Stack | RadioLib 7.2.1 (LoRaWAN 1.1) |
| JSON | ArduinoJson 6.x |
| Command Parser | CmdParser |
| Bluetooth | BluetoothSerial (Classic SPP) |
| Test Framework | Unity (PlatformIO test runner) |
| Companion App | Android (ELOC Control Panel) |

---

## Hardware Platform: ELOC 3.0

- **MCU:** ESP32-WROVER (with PSRAM)
- **Flash:** 16 MB
- **Microphone:** I2S MEMS (ICS-43434 default)
- **Storage:** MicroSD card via SDIO
- **Radio:** SX1262 LoRa module (SPI)
- **Sensors:** LIS3DH accelerometer (I2C), battery voltage ADC
- **Peripherals:** PCA9557 I/O expander, buzzer, status LED, user button
- **Power:** Battery-powered with voltage monitoring, USB charging/power

### Key Pin Assignments (ELOC 3.0 Board)

| Function | GPIO |
|----------|------|
| I2S WS (LRCK) | GPIO 5 |
| I2S SCK (BCLK) | GPIO 18 |
| I2S SD (Data) | GPIO 19 |
| LoRa MISO | GPIO 32 |
| LoRa CLK | GPIO 33 |
| LoRa MOSI | GPIO 26 |
| LoRa CS | GPIO 27 |
| LoRa DIO1 | GPIO 21 |
| LoRa RST | GPIO 25 |
| LoRa BUSY | GPIO 35 |
| I2C SDA | GPIO 23 |
| I2C SCL | GPIO 22 |
| Button | GPIO 0 |
| Status LED | GPIO 4 |
| Battery ADC | GPIO 34 (ADC1_CH6) |
| Buzzer | GPIO 13 |
| LIS3DH INT | GPIO 12 |

---

## Project Structure

```
eloc610LowPowerPartition/
├── src/main.cpp                  # Application entry point & main loop
├── include/project_config.h      # Board-specific HW config & build flags
├── platformio.ini                # Build system configuration
├── elocPartitions.csv            # Flash partition table
├── sdkconfig.defaults            # ESP-IDF SDK configuration defaults
├── lib/
│   ├── audio_input/              # I2S MEMS microphone driver
│   ├── wav_file/                 # WAV file reader/writer
│   ├── sd_card/                  # SD card (SPI & SDIO) drivers
│   ├── ElocHardware/             # System, config, status, LoRa management
│   ├── Commands/                 # Bluetooth command server & protocol
│   ├── edge-impulse/             # Edge Impulse AI inference wrapper
│   ├── FirmwareUpdate/           # OTA firmware update logic
│   ├── Accel/                    # LIS3DH accelerometer driver
│   ├── IO_expander/              # PCA9557 I/O expander driver
│   ├── buzzer/                   # Buzzer control
│   ├── CPPANALOGIO_Battery/      # Battery voltage monitoring
│   ├── CPPI2C/                   # I2C bus abstraction
│   ├── esp32Time/                # RTC time management
│   ├── PerfMonitor/              # CPU & memory performance monitoring
│   ├── spiffs/                   # SPIFFS file system wrapper
│   ├── utils/                    # Logging, file utilities, string helpers
│   ├── uart_eloc/                # Test UART interface
│   └── mock/                     # Mock objects for testing
├── test/                         # Unit & integration tests
├── tools/                        # Build & flash helper scripts
├── payload-formatters/           # TTN LoRaWAN payload formatters (JS)
└── memory-bank/                  # Project documentation (this directory)
```

---

## Task Architecture (FreeRTOS)

| Task | Core | Priority | Purpose |
|------|------|----------|---------|
| Main loop | 0 | Default | Recording orchestration, status monitoring, queue processing |
| I2S Read | 0 | 10 (highest) | Reads I2S DMA buffers, fills WAV & AI buffers |
| WAV Writer | 0 | 8 | Writes audio buffers to SD card as WAV files |
| AI Inference | 1 | 7 | Runs Edge Impulse model on audio data |
| Bluetooth | — | 1 | Handles BT serial commands |
| LoRa | — | — | Periodic uplink & event messaging |
| Perf Monitor | — | — | Optional CPU/memory usage tracking |

---

## Build Variants

| Environment | Description |
|-------------|-------------|
| `esp32dev` | Standard build (no AI) |
| `esp32dev-ei` | Build with Edge Impulse AI inference |
| `target_unit_selected_tests` | Selected unit tests on target hardware |
| `target_unit_all_tests` | All unit tests on target hardware |
| `generic_unit_tests` | Desktop/native unit tests |

---

## Non-Functional Requirements

- **Field Longevity:** Must operate continuously for days/weeks on battery
- **Reliability:** Robust error handling for SD card failures, LoRa connectivity loss, power brownouts
- **Environmental:** Deployed in tropical jungle and savanna environments (heat, humidity)
- **Configurability:** All operational parameters adjustable via Bluetooth without re-flashing
- **Maintainability:** OTA firmware update via SD card for field service
- **Data Integrity:** WAV files properly finalized with headers, no data corruption on power loss
- **Security:** LoRaWAN encryption (though NVS keys stored unencrypted — known limitation)

---

## Known Constraints & Limitations

1. This partition **requires the high-power partition** to be present and cannot run standalone
2. NVS storage for LoRaWAN keys is **unencrypted** — physical access allows key extraction
3. Edge Impulse builds significantly increase binary size and RAM usage
4. Bluetooth Classic (SPP) used instead of BLE for compatibility with existing Android app
5. APLL (Audio PLL) has known issues at sample rates below 16 kHz on ESP32
6. Automatic gain adjustment feature exists but **causes distortion** — currently disabled
7. Light sleep integration with I2S requires careful power management lock handling

---

## Success Criteria

1. **Reliable continuous recording** to SD card for extended field deployments
2. **Accurate on-device AI detection** of target animal sounds with configurable sensitivity
3. **Timely LoRaWAN alerts** when target sounds detected, with robust session management
4. **Seamless Bluetooth configuration** via the ELOC Control Panel companion app
5. **Minimal power consumption** through aggressive power management (light sleep, DFS)
6. **Easy field servicing** via SD card firmware updates and Bluetooth configuration
