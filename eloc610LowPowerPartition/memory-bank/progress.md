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

## What's Left to Build / Fix

### Known Issues
- [ ] **Automatic gain adjustment** causes audio distortion — disabled
- [ ] **Light sleep during recording** may not actually occur due to I2S APB_FREQ_MAX lock
- [ ] **APLL unreliable** at sample rates below 16 kHz (falls back to PLL_D2)
- [ ] **Thread safety** — shared variables between tasks lack mutex guards (noted TODO)
- [ ] **SD card hot-swap** — removing and replacing SD card requires reboot ("spi bus already initialized")
- [ ] **BT not reconnecting** after certain restart scenarios (esp_restart() BLE issue)
- [ ] **NVS LoRaWAN keys unencrypted** — security risk with physical access

### Desired Improvements
- [ ] Migrate from Bluetooth Classic to BLE for power savings
- [ ] Add mutex/semaphore guards to all shared task variables
- [ ] Improve SD card hot-swap handling without reboot
- [ ] Verify and optimize light sleep power savings
- [ ] Expand unit test coverage (LoRa persistence, AI detection logic, config)
- [ ] Add deep sleep timer mode (auto-wake for periodic recording)
- [ ] Consider NVS encryption for LoRaWAN keys

## Current Status

**Overall:** The firmware is functional and field-deployable. All major subsystems (recording, AI, LoRa, Bluetooth, power management) are operational. Recent work has hardened LoRaWAN reliability with session persistence and improved AI detection flexibility with configurable observation windows. The main areas needing attention are power optimization (light sleep verification, BLE migration) and robustness improvements (thread safety, SD card handling).

## Evolution of Project Decisions

1. **Dual-partition architecture** chosen to separate high-power Bluetooth bootstrap from low-power recording — allows independent development of each partition
2. **ESP-IDF + Arduino hybrid** framework used to get both low-level ESP-IDF control (I2S, power management) and Arduino ecosystem convenience (BT Serial, libraries)
3. **Edge Impulse** selected for AI inference — provides model training studio, ESP32-optimized TFLite deployment, and good documentation
4. **RadioLib** chosen for LoRaWAN over LMIC — more actively maintained, cleaner API, better ESP32 support
5. **Bluetooth Classic SPP** used instead of BLE for compatibility with existing Android app — BLE migration planned but not yet prioritized
6. **SDIO** for SD card instead of SPI — significantly faster write speeds required for continuous audio recording
7. **Double buffering** pattern for audio pipeline — ensures continuous capture without gaps during SD card writes
8. **Dual-storage LoRa persistence** (RTC + NVS) — balances fast wake-up with permanent DevNonce continuity
