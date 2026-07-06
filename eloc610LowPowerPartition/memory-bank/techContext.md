# Tech Context

## Development Environment

| Item | Detail |
|------|--------|
| IDE | Visual Studio Code with PlatformIO extension |
| Build System | PlatformIO (platformio.ini) |
| Platform | espressif32 @ 6.9.0 |
| Framework | ESP-IDF + Arduino (hybrid) |
| Board | esp32dev (ESP32-WROVER with PSRAM) |
| Flash | 16 MB |
| Language | C++ (C++11/14) |
| Test Framework | Unity (via PlatformIO test runner) |
| OS | Windows 11 |

## Build Configuration

### Key Build Flags
```
-DBOARD_HAS_PSRAM                  # Enable PSRAM support
-DCONFIG_SPIRAM_CACHE_WORKAROUND   # ESP32 PSRAM cache bug workaround
-Wl,-Map,firmware.map              # Generate linker map
-DEDGE_IMPULSE_ENABLED             # (esp32dev-ei only) Enable AI inference
```

### Build Environments
- **esp32dev** — Standard build without AI
- **esp32dev-ei** — Build with Edge Impulse inference enabled
- **target_unit_selected_tests** — Run selected unit tests on hardware
- **target_unit_all_tests** — Run all unit tests on hardware
- **generic_unit_tests** — Desktop/native unit tests

### Pre-build Scripts
- `tools/patch_fatfs_exfat.py` — Patches `FF_FS_EXFAT 0 → 1` in the packaged IDF FatFs `ffconf.h` to enable exFAT (see below)
- `tools/genVersion.py` — Generates version.h with build timestamp
- `tools/genNVS.py` — Compiles nvs.csv into nvs.bin for NVS partition
- `tools/setUploadMonitorPort.py` — Auto-detects serial port for upload/monitor

### SDK Configuration
Key settings managed via `sdkconfig.defaults`:
- Power management enabled (`CONFIG_PM_ENABLE`)
- FreeRTOS tickless idle (`CONFIG_FREERTOS_USE_TICKLESS_IDLE`)
- Long filenames on FAT filesystem
- SPI RAM enabled

### SD card filesystems: FAT32 and exFAT
SD cards may be formatted as **FAT32 or exFAT** (cards >32 GB ship factory-formatted as exFAT).
IDF 4.4.7 bundles FatFs R0.13c, which supports exFAT but hardcodes `FF_FS_EXFAT 0` in its
`ffconf.h` with no Kconfig switch (`CONFIG_FATFS_USE_EXFAT` only exists from IDF 5.1). The
pre-build script `tools/patch_fatfs_exfat.py` (first entry in `platformio.ini` `extra_scripts`)
patches the packaged header on every build — idempotent, self-heals after a package reinstall,
and fails loudly if a platform bump changes the FatFs revision (`FFCONF_DEF != 86604`). On a
future IDF ≥5.1 upgrade, replace the script with `CONFIG_FATFS_USE_EXFAT=y` in
`sdkconfig.defaults`. The mounted filesystem type is logged after mount in `SDCardSDIO::init()`
(`Filesystem: FAT32` / `exFAT`). After the very first patch, do one full clean build so no stale
fatfs objects remain.

## Dependencies

### PlatformIO Library Dependencies
| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoJson | ^6.21.2 | JSON config serialization/deserialization |
| CmdParser | 0.0.0-alpha | Bluetooth command parsing |
| EasyBuzzer | 1.0.4 | Non-blocking buzzer patterns |
| RadioLib | ^7.1.0 | LoRaWAN (SX1262) communication |

### Bundled Libraries (in /lib)
| Library | Source | Purpose |
|---------|--------|---------|
| edge-impulse | Edge Impulse Studio export | TFLite Micro AI inference |
| esp32Time | Third-party | RTC time management |
| CPPI2C | Third-party | I2C bus abstraction |
| CPPANALOGIO_Battery | Custom/third-party | ADC battery voltage reading |

### Custom Libraries (in /lib)
| Library | Purpose |
|---------|---------|
| audio_input | I2S MEMS microphone driver (I2SMEMSSampler) |
| wav_file | WAV file reader/writer with double buffering |
| sd_card | SD card (SPI & SDIO) mount/unmount/space checking |
| ElocHardware | System init, config, status, LoRa, hardware management |
| Commands | Bluetooth server and command handlers |
| FirmwareUpdate | OTA firmware update from SD card |
| Accel | LIS3DH accelerometer driver |
| IO_expander | PCA9557 I/O expander driver |
| buzzer | Buzzer control wrapper |
| spiffs | SPIFFS filesystem wrapper |
| PerfMonitor | CPU/memory performance stats |
| utils | Logging, file utils, string utils, rotate file |
| uart_eloc | Test UART interface |

## Hardware Interfaces

### I2S (Audio)
- Port: I2S_NUM_0
- DMA: 8 buffers × 1024 samples
- Sample rate: Configurable (default 16 kHz)
- Bit depth: 24-bit (stored as 32-bit words)
- Channel: Mono (configurable left/right)

### SDIO (SD Card)
- 4-bit SDIO mode (fixed ESP32 pins)
- Mount point: `/sdcard`
- ELOC data directory: `/sdcard/eloc/`
- Session folders: `/sdcard/eloc/<header>_<timestamp>/`

### SPI (LoRa)
- Dedicated SPI bus for SX1262
- SPI speed: 2 MHz
- Custom pin mapping (not default VSPI/HSPI)

### I2C
- Speed: 100 kHz
- Shared bus for LIS3DH accelerometer and PCA9557 IO expander
- SDA: GPIO 23, SCL: GPIO 22

### UART
- UART0: Debug/monitor output (115200 baud)
- Optional test UART for command injection

## Flash Partition Layout (16 MB)
```
0x000000 ┌──────────────────┐
         │ Bootloader       │
0x009000 ├──────────────────┤
         │ NVS (20 KB)      │  ← Device identity, LoRa keys
0x00E000 ├──────────────────┤
         │ OTA Data (8 KB)  │
0x010000 ├──────────────────┤
         │ partition0 (~8MB) │  ← High-power partition (OTA_0)
0x7F0000 ├──────────────────┤
         │ partition1 (~8MB) │  ← THIS firmware (OTA_1)
0xFD0000 ├──────────────────┤
         │ SPIFFS (192 KB)  │  ← Config file fallback
0x1000000└──────────────────┘
```

## NVS Structure
Two namespaces:
1. **factory** — `hw_gen`, `hw_rev`, `serial` (device identity)
2. **loraKeys** — `devEUI`, `appKey`, `nwkKey` (LoRaWAN credentials, unencrypted)
3. **lorawan** — `nonces` (runtime DevNonce persistence)

## File System Structure on SD Card
```
/sdcard/eloc/
├── <session_folder>/
│   ├── <session>.config           # Session configuration snapshot
│   ├── <timestamp>_<seq>.wav      # Audio recordings
│   └── EI-results-*.csv           # AI inference results
├── firmware.bin                   # (Optional) OTA update file
└── eloc.config                    # Device configuration
```

## Technical Constraints

1. **Dual-framework complexity:** ESP-IDF + Arduino hybrid causes occasional build conflicts and requires careful include ordering
2. **RAM pressure:** ESP32 has ~520 KB SRAM; PSRAM available but slower. AI buffers placed in PSRAM by default (`EI_BUFFER_IN_PSRAM`)
3. **I2S power management lock:** I2S driver holds APB_FREQ_MAX lock while active, preventing full light sleep during recording
4. **Flash wear:** NVS writes for LoRa nonces occur after each uplink — flash wear consideration for high-frequency uplinks
5. **BT Classic limitation:** Uses Bluetooth Classic SPP (not BLE) for compatibility with existing Android app; consumes more power
6. **Edge Impulse SDK modifications:** Some EI SDK files need manual patching for ESP32 compilation (e.g., uninitialized variable warnings)
7. **APLL limitations:** ESP32 Audio PLL unreliable at sample rates below 16 kHz

## Tool Usage

### Flashing
```bash
# Standard firmware upload
pio run -t upload

# Flash NVS only (initial device provisioning)
python -m esptool write_flash 0x9000 .\.pio\build\esp32dev\nvs.bin

# Auto-flash tool for batch provisioning
python AutoFlasher.py
```

### Monitoring
```bash
pio device monitor  # 115200 baud, with esp32_exception_decoder
```

### Testing
```bash
pio test -e target_unit_selected_tests  # Selected tests on hardware
pio test -e target_unit_all_tests       # All tests on hardware
pio test -e generic_unit_tests          # Desktop tests
```
