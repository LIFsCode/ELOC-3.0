# ELOC-3.0 Firmware

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Framework: ESP-IDF + Arduino](https://img.shields.io/badge/Framework-ESP--IDF%20%2B%20Arduino-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Latest Release](https://img.shields.io/github/v/release/LIFsCode/ELOC-3.0)](https://github.com/LIFsCode/ELOC-3.0/releases)

## Overview

ELOC (ElephantLocator) is a low-power wildlife audio recorder designed for long-term field deployment in wildlife conservation research. This firmware provides intelligent bioacoustic monitoring with AI-powered sound classification, specifically optimized for elephant trumpet detection.

**🌐 Website**: [https://wildlifebug.com/](https://wildlifebug.com/)

### Key Features

- **🎤 High-Quality Audio**: I2S MEMS microphone (10Hz-22kHz, up to 44.1kHz sample rate)
- **🧠 AI Detection**: Edge Impulse integration for real-time elephant trumpet detection
- **📱 Bluetooth Control**: Complete remote configuration via Android app -> Metadata to Database
- **🔋 Solar Powered**: Forever recording / detecting with LiFePO4/Li-Ion battery support
- **💾 Reliable Storage**: SD card with automatic file management
- **📡 Expandable**: Optional LoRa, GPS, and cellular modules

## Hardware Specifications

- **MCU**: ESP32-WROVER-E (240MHz, 8MB PSRAM, 16MB Flash)
- **Audio**: ICS-43434 I2S microphone (24-bit, 65dBA SNR)
- **Storage**: MicroSD up to 2TB (FAT32)
- **Power**: Solar 4-18V, Li-Ion/LiFePO4 battery, ~17mA @ 16kHz
- **Connectivity**: Bluetooth, optional LoRaWAN
- **Sensors**: 3-axis accelerometer, temperature sensor
- **Enclosure**: IP66 weatherproof

### System Architecture

![ELOC 3.4 Block Diagram](https://github.com/user-attachments/assets/your-image-id-here)

**Core Components**: ESP32, digital microphone, battery management, SD storage, sensors  
**Expansion Modules**: LoRa, 2G/3G/4G, GPS, external audio via SPI/I2C/I2S/UART

## Android App

**📱 [Download from Google Play Store](https://play.google.com/store/apps/details?id=de.eloc.eloc_control_panel_2)**

Control recording modes, configure settings, monitor battery/storage, and view AI detection results.


### Project Structure

```
eloc610LowPowerPartition/
├── src/                     # Main application
├── lib/                     # Custom libraries
│   ├── audio_input/         # I2S audio capture
│   ├── edge-impulse/        # AI inference
│   ├── ElocHardware/        # Hardware abstraction
│   └── Commands/            # Bluetooth interface
├── tools/                   # Build scripts
└── test/                    # Unit tests
```

### Build Environments
- **esp32dev**: Standard build
- **esp32dev-ei**: AI-enabled (recommended)
- **target_unit_*_tests**: Hardware tests

### Power Consumption
- **Recording**: ~18mA @ 16kHz, ~35mA @ 44kHz
- **AI Detection**: Between ~23mA and 70mA depending on the model
- **Sleep Mode**: ~2mA

### File Organization
- **Session folders**: `DEVICENAME_TIMESTAMP`
- **Audio files**: `FILEHEADER_SESSIONID_TIMESTAMP.wav`
- **AI results**: CSV files with detection events
- **Metadata**: Configuration and status JSON files

## Configuration

Device settings via JSON format:
- Audio: sample rate, gain, channels
- Recording: file duration, storage location  
- AI: detection thresholds, inference frequency
- Power: sleep modes, CPU frequency

See [README-nvs.md](eloc610LowPowerPartition/README-nvs.md) for NVS configuration.

## Troubleshooting

**Common Issues**:
1. **Build errors**: Run "Full Clean" before AI builds
2. **SD card**: Format as FAT32, check connections

**Debug**: Enable verbose logging and monitor serial output
```bash
pio device monitor -e esp32dev-ei
```

## API Reference

Full API: [ELOC 3.0 App Interface](https://github.com/LIFsCode/ELOC-3.0/wiki/ELOC-3.0-App-Interface)

## License

MIT License - see [LICENSE](LICENSE) file.

## Credits

- **tbgilson**: Original firmware architecture
- **Wildlife Conservation International**: Project sponsorship
- **Edge Impulse**: AI/ML platform integration

## Support

- 📋 [Create issue](https://github.com/LIFsCode/ELOC-3.0/issues)
- 📖 [Project wiki](https://github.com/LIFsCode/ELOC-3.0/wiki)
- 📱 [Android app](https://play.google.com/store/apps/details?id=de.eloc.eloc_control_panel_2)

---

**🐘 Protecting wildlife through technology**  
*ELOC-3.0 Low Power Wildlife Audio Recorder*
