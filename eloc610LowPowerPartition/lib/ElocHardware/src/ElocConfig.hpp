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
    uint32_t thresholdCnt;     // knocks within detectWindowMS needed to open a candidate ("more than")
    uint32_t detectWindowMS;
    uint32_t alarmIntervalS;   // interval between intruder alarm LoRa uplinks while the device is moving
    uint32_t idleIntervalS;    // DEPRECATED (V1.73): a stopped device now transmits nothing at all, so
                               // there is no idle cadence left to configure. Still parsed and reported
                               // so older apps and stored configs keep working; the value is ignored.
    uint32_t settleMs;         // after a knock burst, ignore the accelerometer this long - the knocks
                               // themselves register as movement and would confirm every candidate
    uint32_t confirmWindowS;   // how long a candidate waits for real movement before expiring to IDLE
    uint32_t quietS;           // stillness before a moving device counts as stopped
    uint32_t alarmTimeoutH;    // auto-clear a confirmed alarm after this long with no movement
                               // (0 = never; the alarm then latches until detection is disabled)
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
    uint32_t alarmUpLinkIntervalS; // heartbeat interval while a CONFIRMED intruder alarm is active.
                              // A stolen device that has been put down transmits no alarm messages at
                              // all (they only go out while it is moving), so this accelerated
                              // heartbeat is the only thing saying it is still alive and where it was.
    String loraRegion;        // Lora Region, e.g. EU868
    uint32_t eventCooldownS;  // Min seconds between event LoRa msgs (0 = legacy mode, send every detection)
    uint32_t eventEndTimeoutS;// Seconds without detection before event is considered ended
}loraConfig_T;

typedef struct {
    uint32_t threshold;           // AI confidence threshold (0-100, e.g. 83 = 0.83)
    uint32_t observationWindowS;  // observation window in seconds (0 = legacy immediate mode)
    uint32_t requiredDetections;  // number of detections required within window
}inferenceConfig_t;

/// @brief Duty-cycle deep sleep configuration
typedef struct {
    bool     enable;           // Enable duty-cycle mode (default: false)
    uint32_t sleepDurationS;   // Deep sleep duration in seconds (default: 300 = 5 min)
    uint32_t awakeDurationS;   // Active inference duration in seconds (default: 30)
}dutyCycleConfig_t;

/// @brief LoRa coverage survey ("signal strength mapper") configuration.
///        While enabled the survey owns the LoRa loop: heartbeat, event and intruder uplinks are
///        suspended, so nothing else competes for the airtime budget.
typedef struct {
    bool     enable;             // master switch; persists across reboot via the config cascade
    uint32_t minIntervalS;       // time floor between survey uplinks (raised per SF, see C_SF_MIN_INTERVAL_S)
    uint32_t minDistanceM;       // send once the device has moved this far since the last sample
    uint32_t startSF;            // spreading factor the survey starts on (7..12)
    bool     adaptiveSF;         // step 7 -> 9 -> 10 -> 12 when link checks stop being answered
    uint32_t linkCheckEveryN;    // request a downlink every Nth sample (0 = only on demand)
    bool     audio;              // buzzer readout after every link check
    bool     buttonUplink;       // GPIO0 forces an immediate uplink (no downlink requested)
    uint32_t sessionTimeoutMin;  // hard stop, so a forgotten session cannot run the battery flat
    uint32_t maxUplinksPerDay;   // hard budgets, counted per local day
    uint32_t maxDownlinksPerDay;
}surveyConfig_t;

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
    dutyCycleConfig_t dutyCycleConfig;
    surveyConfig_t surveyConfig;
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

/// @brief Build the factory-default Bluetooth/config name from an NVS serial number.
///        Uses the last five serial digits and pads them with leading zeroes when needed.
String formatDefaultNodeName(uint32_t serialNumber);

/// @brief Supply the factory NVS serial used when creating or migrating a default node name.
void setFactorySerialNumber(uint32_t serialNumber);

const loraConfig_T& getLoraConfig();

const inferenceConfig_t& getInferenceConfig();

const dutyCycleConfig_t& getDutyCycleConfig();

const surveyConfig_t& getSurveyConfig();

/// @brief Flip surveyConfig.enable in the running config and persist it, so survey mode
///        survives a reboot (see setSurveyMode). Does not start/stop the session itself.
/// @return ESP_OK on success, ESP_ERR_FLASH_BASE if persisting failed.
esp_err_t setSurveyEnabled(bool enable);

/// @brief Validate and clamp duty cycle config values to safe ranges
void validateDutyCycleConfig();

/// @brief True if the value is a selectable ESP32 CPU max clock (PLL-derived: 80/160/240 MHz).
bool isValidCpuMaxFrequency(int mhz);

/// @brief True if the value is a selectable ESP32 CPU min clock: the PLL frequencies (80/160/240)
///        or the crystal-derived low frequencies (40/20/10 MHz). See esp_pm_configure().
bool isValidCpuMinFrequency(int mhz);

/// @brief Overwrite the CPU frequency / light-sleep fields in the running config and persist them.
///        Used to roll back a setConfig change the hardware rejected, so the stored/displayed config
///        stays consistent with what is actually applied.
/// @return ESP_OK on success, ESP_ERR_FLASH_BASE if persisting failed.
esp_err_t setCpuFrequencyConfig(int maxFrequencyMHZ, int minFrequencyMHZ, bool enableLightSleep);

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

/// @brief Reports which config groups were present in a setConfig JSON delta,
///        so the caller can re-apply them to running subsystems (live apply).
typedef struct {
    bool cpu;        // any of cpuMaxFrequencyMHZ / cpuMinFrequencyMHZ / cpuEnableLightSleep
    bool logConfig;  // any logConfig.* field
} configChangeFlags_t;

esp_err_t updateConfig(const char* buf, configChangeFlags_t* changeFlags = nullptr);

#endif // ELOCCONFIG_HPP_
