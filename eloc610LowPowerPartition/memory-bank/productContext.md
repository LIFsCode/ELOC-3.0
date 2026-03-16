# Product Context

## Why This Project Exists

The ELOC (Electronic Listening and Observation Console) was created for the **International Elephant Project** by **Wildlife Conservation International (WCI)**. Elephant populations in Southeast Asia and Africa face threats from poaching, habitat encroachment, and human-wildlife conflict. Conservation teams need a way to monitor vast, remote areas of jungle and savanna for elephant presence — but cannot have humans physically present 24/7.

ELOC provides an automated, low-cost acoustic monitoring solution that can be deployed in harsh field environments for weeks at a time, recording environmental sounds and optionally detecting target animal vocalizations using on-device AI.

## Problems It Solves

1. **Remote Area Monitoring:** Traditional monitoring requires human presence or expensive satellite-linked equipment. ELOC uses battery-powered, weatherproof devices with LoRaWAN for long-range, low-power alerts.

2. **Real-Time Detection:** Rather than collecting days of audio and analyzing it later, ELOC can run Edge Impulse AI models on-device to detect specific sounds (e.g., elephant calls, chainsaw activity) and send immediate LoRaWAN alerts.

3. **Data Volume Management:** Continuous audio recording generates large volumes of data. ELOC's configurable recording modes (continuous, AI-triggered, disabled) let conservation teams balance data volume against detection coverage.

4. **Scalable Deployment:** The low per-unit cost and simple configuration (via Android app) enables deploying networks of ELOC devices across large conservation areas.

5. **Field Serviceability:** Devices deployed in remote locations need to be serviced quickly — swap SD cards, update firmware, change configuration — all without specialized equipment.

## How It Should Work

### Typical Field Workflow

1. **Pre-deployment:** Technician configures the ELOC via the Android **ELOC Control Panel** app over Bluetooth — sets device name, location code, microphone settings, recording mode, AI model parameters, and LoRa settings.

2. **Deployment:** Device is mounted at the monitoring site (typically strapped to a tree). Recording begins automatically or is started via the app. The device enters low-power mode.

3. **Active Monitoring:** The device continuously records audio to SD card as WAV files. If AI inference is enabled, it simultaneously analyzes audio for target sounds. On detection, it:
   - Logs the event to a CSV file on SD card
   - Triggers a LoRaWAN uplink event message
   - Can switch to continuous recording mode if in AI-triggered (single) mode

4. **Status Reporting:** Periodic LoRaWAN status messages report battery level, recording status, and detection counts to a TTN (The Things Network) dashboard.

5. **Data Retrieval:** Technician visits the site, connects via Bluetooth to check status, stops recording, swaps the SD card, and optionally updates firmware by placing a binary on the new SD card.

### Operating Modes

| Mode | Audio Recording | AI Inference | Power Usage | Use Case |
|------|----------------|-------------|-------------|----------|
| Continuous Recording | ✅ Always | Optional | Higher | Full acoustic survey |
| AI-Triggered (Single) | On detection only | ✅ Always | Medium | Event-focused monitoring |
| AI Only (No Recording) | ❌ | ✅ Always | Lower | Detection counting / alerting |
| Disabled | ❌ | ❌ | Minimal | Transit / storage |

## User Experience Goals

- **Deploy and Forget:** Once configured and placed, the device should operate autonomously for the battery's lifetime without intervention
- **Simple Configuration:** All settings adjustable via a user-friendly Android app over Bluetooth
- **Reliable Data Capture:** No audio data loss — proper WAV file finalization, robust SD card handling
- **Actionable Alerts:** LoRa event messages should provide meaningful, timely alerts when target sounds are detected
- **Easy Maintenance:** SD card swap for data retrieval, firmware updates via SD card, no soldering or reprogramming needed

## Target Users

- **Conservation field technicians** — deploy, configure, and service devices
- **Wildlife researchers** — analyze collected audio data and detection logs
- **Conservation program managers** — monitor detection alerts via LoRaWAN dashboards
- **Firmware developers** — extend and improve device capabilities
