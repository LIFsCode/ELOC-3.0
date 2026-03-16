# DMM-4026-B-I2S-R Microphone Support

This document describes the implementation of support for the PUI Audio DMM-4026-B-I2S-R microphone in addition to the existing TDK InvenSense ICS-43434 microphone.

## Overview

The DMM-4026-B-I2S-R microphone has different timing and data format requirements compared to the ICS-43434:

### Key Differences

| Feature | ICS-43434 | DMM-4026-B-I2S-R |
|---------|-----------|-------------------|
| Data Format | 24-bit full precision | 24-bit with 18-bit precision (6 null bits) |
| Word Size | 24-bit | 32-bit word size required |
| **I2S Format** | **Standard I2S (Philips)** | **MSB-aligned (Left-justified)** |
| BCLK Range | Calculated from sample rate | 3.072-12.288 MHz |
| Input Clock | Calculated | 2.048-4.096 MHz |
| Sensitivity | -26 dB FS ±1 dB | -26 dB FS ±1 dB |
| Sample Rate | 23-51.6 kHz (HP), 6.25-18.75 kHz (LP) | 8-48 kHz (based on clock constraints) |

## Configuration

### Switching Between Microphones

To switch between microphones, edit `include/project_config.h`:

**For ICS-43434 (default):**
```c
#define I2S_TDK_INVENSENSE_ICS_43434
// #define I2S_PUI_DMM_4026_B_I2S_R
```

**For DMM-4026-B-I2S-R:**
```c
// #define I2S_TDK_INVENSENSE_ICS_43434
#define I2S_PUI_DMM_4026_B_I2S_R
```

### Configuration Parameters

The following parameters are automatically configured based on the selected microphone:

#### DMM-4026-B-I2S-R Specific:
- `I2S_BITS_PER_SAMPLE`: 24 (same as ICS-43434)
- `I2S_DEFAULT_VOLUME`: -3 (adjusted to match ICS-43434 behavior)
- `I2S_DMM_4026_PRECISION_BITS`: 18 (actual data precision)
- `I2S_DMM_4026_NULL_BITS`: 6 (number of null bits)
- `I2S_SAMPLE_RATE_MIN`: 8000 Hz
- `I2S_SAMPLE_RATE_MAX`: 48000 Hz
- `I2S_DMM_4026_ENABLE_DC_FILTER`: Enable DC offset removal filter
- `I2S_DMM_4026_DC_FILTER_ALPHA`: Filter coefficient (0.001 = slow response)

## Implementation Details

### Files Modified

1. **`include/project_config.h`**
   - Added DMM-4026-B-I2S-R configuration block
   - Added microphone-specific parameters

2. **`lib/ElocHardware/src/config.cpp`**
   - Updated I2S configuration with conditional settings
   - Adjusted MCLK multiplier for DMM-4026-B-I2S-R requirements

3. **`lib/audio_input/src/I2SMEMSSampler.cpp`**
   - Modified bit shifting logic to handle 18-bit precision
   - Added microphone identification logging
   - Adjusted volume calculations for reduced precision

### Bit Processing Logic

The key difference in processing is handling the reduced precision:

**ICS-43434:**
```c
overall_bit_shift = (32 - I2S_BITS_PER_SAMPLE) - volume2_pwr;
// = (32 - 24) - volume2_pwr = 8 - volume2_pwr
```

**DMM-4026-B-I2S-R:**
```c
overall_bit_shift = (32 - I2S_BITS_PER_SAMPLE + I2S_DMM_4026_NULL_BITS) - volume2_pwr;
// = (32 - 24 + 6) - volume2_pwr = 14 - volume2_pwr
```

This accounts for the 6 null bits in the DMM-4026-B-I2S-R data format.

### DC Offset Removal Filter

The DMM-4026-B-I2S-R microphone may not have internal AC coupling, which can cause DC offset issues in the audio signal. To address this, a software DC offset removal filter has been implemented:

**Filter Algorithm:**
```c
// Exponential moving average to track DC component
dc_filter_state = (sample * alpha) + (dc_filter_state * (1 - alpha));

// Remove DC offset from sample
filtered_sample = sample - dc_filter_state;
```

**Configuration:**
- `I2S_DMM_4026_ENABLE_DC_FILTER`: Enables the filter (defined by default)
- `I2S_DMM_4026_DC_FILTER_ALPHA`: Filter coefficient (0.001 = slow response, good for DC removal)

**How it works:**
- The filter tracks the average (DC) level of the audio signal
- It subtracts this DC component from each sample
- This centers the waveform around zero, removing DC offset
- The small alpha value (0.001) ensures the filter responds slowly to actual audio content but effectively removes static DC shifts

## Testing

### Verification Steps

1. **Build and Flash:**
   ```bash
   pio run -t upload
   ```

2. **Monitor Serial Output:**
   ```bash
   pio device monitor
   ```

3. **Check Microphone Detection:**
   Look for log messages indicating which microphone configuration is active:
   ```
   I2SMEMSSampler: Microphone: PUI DMM-4026-B-I2S-R (18-bit precision in 24-bit word)
   ```
   or
   ```
   I2SMEMSSampler: Microphone: TDK InvenSense ICS-43434 (24-bit full precision)
   ```

4. **Audio Quality Test:**
   - Test audio recording functionality
   - Verify Edge Impulse inference works correctly
   - Check for audio clipping or distortion

### Expected Behavior

- **DMM-4026-B-I2S-R**: Should show proper audio levels with the adjusted bit shifting
- **Volume Levels**: May need fine-tuning of `I2S_DEFAULT_VOLUME` based on actual hardware testing
- **Sample Rates**: Should work within the 8-48 kHz range for DMM-4026-B-I2S-R

## Troubleshooting

### I2S Timing and Communication Format

**IMPORTANT**: The DMM-4026-B-I2S-R uses a different I2S communication format than the ICS-43434.

Based on the timing diagram in the datasheet:
- **TSWCLK** (Setup time): min 20 nS - WCLK must be stable before BCLK falling edge
- **TDV** (Data Valid): max 18 nS - Data becomes valid after BCLK falling edge
- **THWCLK** (Hold time): 32 (1/BCLK) for two mic mode

The key observation is that in the DMM-4026-B-I2S-R timing diagram for "Two microphone Mode":
- WCLK transitions → BCLK falls → Data appears immediately (within TDV)

This corresponds to **MSB-aligned (Left-justified) format**, NOT standard I2S Philips format.

| Format | WS Transition | First Data Bit |
|--------|---------------|----------------|
| **Standard I2S (Philips)** | BCLK edge [N] | BCLK edge [N+1] (one cycle later) |
| **MSB-aligned (Left-justified)** | BCLK edge [N] | BCLK edge [N] (same cycle) |

**The fix**: Changed from `I2S_COMM_FORMAT_STAND_I2S` to `I2S_COMM_FORMAT_STAND_MSB` for the DMM-4026-B-I2S-R microphone in `lib/ElocHardware/src/config.cpp`.

**Symptoms of wrong format**: Using standard I2S format with the DMM-4026-B-I2S-R causes noise spikes at sample boundaries because each sample is off by one bit position.

### Common Issues

1. **No Audio Signal:**
   - Verify correct microphone type is selected in config
   - Check I2S pin connections
   - Verify power supply voltage (1.5-3.6V for DMM-4026-B-I2S-R)

2. **Audio Too Quiet/Loud:**
   - Adjust `I2S_DEFAULT_VOLUME` in the microphone configuration
   - Check bit shifting calculations in debug logs

3. **Clock Issues:**
   - DMM-4026-B-I2S-R requires specific BCLK ranges
   - Verify MCLK multiplier settings
   - Check sample rate compatibility

### Debug Information

Enable verbose logging to see detailed bit shift calculations:
```c
// In platformio.ini or build flags
-DCORE_DEBUG_LEVEL=5
```

## Hardware Considerations

### DMM-4026-B-I2S-R Specific:
- **Power Supply**: 1.5-3.6 VDC (vs 1.65-3.63V for ICS-43434)
- **Current Consumption**: 820-1000 μA normal mode (vs 490 μA for ICS-43434)
- **Package**: Different physical dimensions
- **Soldering**: Reflow solder only (same as ICS-43434)

### Pin Compatibility
Both microphones use the same I2S pin configuration:
- BCLK (Bit Clock)
- WCLK/WS (Word Select/Left-Right Clock)  
- DOUT/SD (Serial Data Output)
- VDD (Power)
- GND (Ground)

## Future Enhancements

1. **Runtime Detection**: Implement automatic microphone type detection
2. **Dynamic Configuration**: Allow switching without recompilation
3. **Calibration**: Add microphone-specific calibration routines
4. **Performance Optimization**: Fine-tune settings for each microphone type

## References

- [DMM-4026-B-I2S-R Datasheet](https://www.puiaudio.com/media/SpecSheet/DMM-4026-B-I2S-R.pdf)
- [ICS-43434 Datasheet](https://invensense.tdk.com/download-pdf/ics-43434-datasheet)
- [ESP32 I2S Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
