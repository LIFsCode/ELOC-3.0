/*
 * Created on Wed Apr 26 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef ELOCCONFIG_HPP_
#define ELOCCONFIG_HPP_

#include "WString.h"

 #define ENUM_MACRO(name, v0, v1, v2)\
    enum class name { v0, v1, v2};\
    constexpr const char *name##Strings[] = {  #v0, #v1, #v2}; \
    constexpr const char *toString(name value) {  return name##Strings[static_cast<int>(value)]; }

/// @brief Definition of channel selection. NOTE: Changes affect the JSON schema of the configuration!
ENUM_MACRO(MicChannel_t, Left, Right, Stereo);
#undef ENUM_MACRO

/// @brief Holds all the Microphone & recording spedific settings
typedef struct {
    String   MicType;
    int      MicVolume2_pwr; // 2^x
    uint32_t MicSampleRate; // TODO: this should finally be moved to Mic Info for consistency
    bool     MicUseAPLL;
    MicChannel_t MicChannel;
}micInfo_t;

const micInfo_t& getMicInfo();

typedef struct {
    bool logToSdCard;
    String filename;
    uint32_t maxFiles;
    uint32_t maxFileSize;
}logConfig_t;

typedef struct {
    bool detectEnable;
    uint32_t thresholdCnt;
    uint32_t detectWindowMS;
}intruderConfig_t;

typedef struct {
    uint32_t updateIntervalMs; // time between battery voltage readings in ms
    uint32_t avgSamples;       // number of voltage samples to read
    uint32_t avgIntervalMs;    // interval between voltage readings (0 == ignored)
    bool noBatteryMode;        // disables battery readings to allow the device to be powered from USB only
}batteryConfig_t;

typedef struct {
    bool loraEnable;          // enable/disable Lora communication
    uint32_t upLinkIntervalS; // time between lora uplink messages in seconds
    String loraRegion;        // Lora Region, e.g. EU868
}loraConfig_T;

typedef struct {
    uint32_t threshold;           // AI confidence threshold (0-100, e.g. 83 = 0.83)
    uint32_t observationWindowS;  // observation window in seconds (0 = legacy immediate mode)
    uint32_t requiredDetections;  // number of detections required within window
}inferenceConfig_t;

/// @brief holds all the device specific configuration settings
typedef struct {
    int  secondsPerFile;
    int  cpuMaxFrequencyMHZ;    // SPI this fails for anything below 80   //
    int  cpuMinFrequencyMHZ;
    bool cpuEnableLightSleep;   //only for AUTOMATIC light sleep.
    bool bluetoothEnableAtStart;
    bool bluetoothEnableOnTapping;
    bool bluetoothEnableDuringRecord;
    int bluetoothOffTimeoutSeconds;
    bool testI2SClockInput;
    logConfig_t logConfig;
    intruderConfig_t IntruderConfig;
    batteryConfig_t batteryConfig;
    loraConfig_T loraConfig;
    inferenceConfig_t inferenceConfig;
}elocConfig_T;

const elocConfig_T& getConfig();

/// @brief Holds all Device Meta data, such as Name, location, etc.
typedef struct {
    String fileHeader;
    String locationCode;
    int locationAccuracy;
    String nodeName;
}elocDeviceInfo_T;
const elocDeviceInfo_T& getDeviceInfo();

const loraConfig_T& getLoraConfig();

const inferenceConfig_t& getInferenceConfig();

/**
 * @brief Load configuration (.config)
 * @note Searched in priority order:
 *          1. SD card (CFG_FILE_SD)
 *          2. SPIFFS (i.e. onboard flash) (CFG_FILE)
 *          3. Default values (constructed in code)
 */
void readConfig();

/// @brief write current configuration to file system
/// @return True: success
///         False: failure
bool writeConfig();

void clearConfig();

enum class CfgType {RUNTIME, DEFAULT_CFG};

bool printConfig(String& buf, CfgType cfgType = CfgType::RUNTIME);

esp_err_t updateConfig(const char* buf) ;

#endif // ELOCCONFIG_HPP_
