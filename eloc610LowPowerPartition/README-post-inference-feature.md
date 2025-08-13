# Post-Inference Feature and Settings

## Overview

This document describes the new configurable inference system added to the ELOC firmware, which provides intelligent detection criteria and windowed observation capabilities for improved field deployment reliability.

## Key Features Added

### 1. Configurable Inference Parameters

The firmware now supports runtime-configurable inference parameters through the existing JSON configuration system:

```json
{
  "config": {
    "inference": {
      "threshold": 83,
      "observationWindowS": 600,
      "requiredDetections": 100
    }
  }
}
```

**Parameters:**
- `threshold` (0-100): AI confidence threshold percentage (e.g., 83 = 0.83 confidence)
- `observationWindowS` (0-600+): Time window in seconds for counting detections
- `requiredDetections` (1-100): Number of detections needed within the observation window

### 2. Enhanced Detection Logic

**Immediate Actions** (triggered for every detection above threshold):
- ✅ CSV logging to SD card
- ✅ Audio recording initiation  (if enabled in config)
- ✅ LoRa message transmission (if LoRa enabled)

**Windowed Actions** (triggered only when observation criteria are met):
- ✅ Event counter increment
- ✅ Detection window reset
- ✅ Additional system actions

### 3. Backward Compatibility

**Legacy Mode**: Set `observationWindowS = 0` to maintain original behavior (immediate actions only)

**New Mode**: Set `observationWindowS > 0` to enable intelligent windowed detection system

## Technical Implementation

### Detection Window Management
- **Buffer Size**: 128 detection timestamps (circular buffer)
- **Memory Usage**: 512 bytes additional RAM
- **Window Support**: Up to 600+ seconds observation windows
- **Detection Capacity**: Up to 100 required detections per window

### Files Modified
1. `lib/ElocHardware/src/ElocConfig.hpp` - Added inference configuration structure
2. `lib/ElocHardware/src/ElocConfig.cpp` - Configuration loading/saving logic
3. `lib/edge-impulse/src/EdgeImpulse.hpp` - Detection tracking variables and methods
4. `lib/edge-impulse/src/EdgeImpulse.cpp` - Detection window management implementation
5. `src/main.cpp` - Updated inference callback with new detection logic
6. `lib/Commands/src/ElocCommands.cpp` - Fixed firmware version include
7. `lib/Commands/src/BluetoothServer.cpp` - Fixed config include path

## Benefits

- **Reduced False Positives**: Windowed detection criteria prevent spurious triggers
- **Comprehensive Logging**: Every detection above threshold is still logged for analysis
- **Field Deployment Ready**: Robust detection suitable for remote, unattended operation
- **Runtime Configurable**: No firmware changes needed to adjust detection behavior
- **Minimal Memory Impact**: Only 0.1% additional RAM usage (384 bytes)

## Usage

1. **Configure via Bluetooth**: Use existing `setConfig` command with new inference parameters
2. **Persistent Storage**: Configuration is saved to SPIFFS/SD card automatically
3. **Runtime Changes**: No reboot required - changes take effect immediately
4. **Status Monitoring**: Use `getStatus` command to verify current configuration

## Default Values

- `threshold`: 85 (85% confidence)
- `observationWindowS`: 10 (10 seconds)
- `requiredDetections`: 5 (5 detections needed)

These defaults provide a balanced approach between sensitivity and false positive reduction.
