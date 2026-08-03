/*
 * Created on Wed Apr 26 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 * based on the work of @tbgilson (https://github.com/tbgilson)
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
#include <esp_log.h>

#include "ArduinoJson.h"
#include "WString.h"

#include "Esp.h"
#include <FS.h>
#include "SPIFFS.h"

#include "ffsutils.h"
#include "jsonutils.hpp"
#include "config.h"
#include "ElocConfig.hpp"
#include "SDCardSDIO.h"

static const char* TAG = "CONFIG";
static const uint32_t JSON_DOC_SIZE = 3072;
static const char* CFG_FILE = "/spiffs/eloc.config";
static const char* CFG_FILE_SD = "/sdcard/eloctest.txt";
static const char* LEGACY_NODE_NAME = "ELOC_NONAME";

extern SDCardSDIO sd_card;

//BUGME: encapsulate these in a struct & implement a getter
static const micInfo_t C_MicInfo_Default {
    .MicType="ICS-43434",
    .MicVolume2_pwr = I2S_DEFAULT_VOLUME,
    .MicSampleRate = I2S_DEFAULT_SAMPLE_RATE,
    .MicUseAPLL = false,
    .MicChannel =
#ifdef I2S_DEFAULT_CHANNEL_FORMAT_LEFT
        MicChannel_t::Left
#else
        MicChannel_t::Right
#endif
};
micInfo_t gMicInfo = C_MicInfo_Default;

const micInfo_t& getMicInfo() {
    return gMicInfo;
}

/**
 * @brief Update the configuration of the I2S microphone
 * @note Possible configuration sources are:
 *         1. '.config' file on SD card
 *         2. '.config' file on SPIFFS
 *         3. Setting in src/config.h
 * TODO: Confirm the priority of the configuration sources??
 */
void updateI2sConfig() {
    i2s_mic_Config.sample_rate = gMicInfo.MicSampleRate;
    i2s_mic_Config.use_apll = gMicInfo.MicUseAPLL;
    if (i2s_mic_Config.sample_rate == 0) {
        ESP_LOGI(TAG, "Resetting invalid sample rate to default = %d", I2S_DEFAULT_SAMPLE_RATE);
        i2s_mic_Config.sample_rate = I2S_DEFAULT_SAMPLE_RATE;
    }
    switch (gMicInfo.MicChannel) {
        // TODO: check about potential wrong channel selection in ESP IDF lib
        case MicChannel_t::Left:
            ESP_LOGI(TAG, "Mic Channel mode: Left");
            i2s_mic_Config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
            break;
        case MicChannel_t::Right:
            ESP_LOGI(TAG, "Mic Channel mode: Right");
            i2s_mic_Config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
            break;
        default:
            ESP_LOGE(TAG, "Mic Channel mode %s is currently not supported!", toString(gMicInfo.MicChannel));
            #ifdef I2S_DEFAULT_CHANNEL_FORMAT_LEFT
                i2s_mic_Config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
            #else
                i2s_mic_Config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
            #endif
            break;
    }

    ESP_LOGI(TAG, "Sample rate = %d", i2s_mic_Config.sample_rate);
}
/**************************************************************************************************/


/*************************** Global settings via BT Config ****************************************/
//BUGME: encapsulate these in a struct & implement a getter


static const elocConfig_T C_ElocConfig_Default {
    .secondsPerFile = 3600,
    // Power management
    .cpuMaxFrequencyMHZ = 80,    // minimum 80
    // min = max, i.e. DFS OFF by default. Dynamic frequency scaling on this chip corrupts both
    // software time bases — esp_timer (TG0 LACT, divided from APB) ran ~41 % fast and the FreeRTOS
    // tick behind every ESP_LOGx timestamp ~23 % fast, measured on a 10/80 MHz profile. Audio is
    // unaffected (I2S runs off the APLL), but anything scheduled off esp_timer fires early: the
    // duty-cycle GPS acquisition window is the one that hurts, since CONFIG_DEFAULT is the profile
    // in force during the timer-wake GPS wait. It also garbles the console despite REF_TICK.
    // The idle dip from 80 down to 10 MHz is the only thing given up.
    .cpuMinFrequencyMHZ = 80,
    // Automatic light sleep is DISABLED by default on purpose — do not flip this back to true
    // as an "optimization". The device hangs inside the light-sleep entry/exit sequence and is
    // reset by the safety-net RTC watchdog esp_light_sleep_start() arms (reset reason 0x10,
    // RTCWDT_RTC_RESET, no panic and no backtrace). Worse than a plain reboot: an RTC WDT reset
    // is not a deep-sleep wake, so the duty-cycle RTC block is memset and the unit comes back
    // NOT recording and NOT duty cycling — a silently dead node in the field.
    // It can only engage when nothing holds a PM lock, i.e. idle after the Bluetooth timeout and
    // during the duty-cycle GPS wait; recording is safe because I2S holds a lock throughout.
    // The underlying hang is unexplained (VDD_SDIO powers both PSRAM and the SD card on this
    // board and light sleep power-cycles that rail — prime suspect, not investigated).
    .cpuEnableLightSleep = false,
    .bluetoothEnableAtStart = true,
    .bluetoothEnableOnTapping = true,
    .bluetoothEnableDuringRecord = true,
    .bluetoothOffTimeoutSeconds = 360,
    .testI2SClockInput = false,
    .logConfig = {
        .logToSdCard = false,
        .filename = "/sdcard/log/eloc.log",
        .maxFiles = 10,
        .maxFileSize = 5*1024*1024,
    },
    .IntruderConfig = {
        .detectEnable = false,
        .thresholdCnt = INTRUDER_DETECTION_THRSH,
        .detectWindowMS = 2000,
        .alarmIntervalS = 600,      // 10 minutes between intruder alarm LoRa msgs
    },
    .batteryConfig = {
        .updateIntervalMs = 10*60*1000, //10 minutes
        .avgSamples = 10,
        .avgIntervalMs = 0,
        .noBatteryMode  = false,
    },
    .loraConfig = {
        .loraEnable = true,
        .upLinkIntervalS = 86400,
        .loraRegion = "AS923_2",
        .eventCooldownS = 900,      // 15 minutes between event LoRa msgs
        .eventEndTimeoutS = 300,    // 5 minutes without detection = event ended
    },
    .inferenceConfig = {
        .threshold = 85,           // Default to 85 (0.85 confidence)
        .observationWindowS = 10,   // Default to legacy immediate mode
        .requiredDetections = 5,   // Default to single detection
    },
    .dutyCycleConfig = {
        .enable = true,           // Disabled by default (normal continuous operation)
        .sleepDurationS = 120,     // 5 minutes deep sleep
        .awakeDurationS = 30,      // 30 seconds active inference
    },
};
elocConfig_T gElocConfig = C_ElocConfig_Default;
const elocConfig_T& getConfig() {
    return gElocConfig;
}

static const elocDeviceInfo_T C_ElocDeviceInfo_Default {
    .fileHeader = "not_set",
    .locationCode = "unknown",
    .locationAccuracy = 99,
    // Filled from the factory NVS serial after ElocSystem has initialized.
    .nodeName = "",
};
elocDeviceInfo_T gElocDeviceInfo = C_ElocDeviceInfo_Default;
static bool gNodeNameMigrationPending = false;
static uint32_t gFactorySerialNumber = 0;

String formatDefaultNodeName(uint32_t serialNumber) {
    char name[24];
    snprintf(name, sizeof(name), "ELOC_%05lu",
             static_cast<unsigned long>(serialNumber % 100000UL));
    return String(name);
}

void setFactorySerialNumber(uint32_t serialNumber) {
    gFactorySerialNumber = serialNumber;
}

static String factoryDefaultNodeName() {
    return formatDefaultNodeName(gFactorySerialNumber);
}

static bool isV163FullSerialNodeName(const String& nodeName) {
    if (gFactorySerialNumber < 100000UL) {
        return false;
    }

    char legacyName[24];
    snprintf(legacyName, sizeof(legacyName), "ELOC_%lu",
             static_cast<unsigned long>(gFactorySerialNumber));
    return nodeName == legacyName;
}

static bool isValidUtf8NodeName(const String& nodeName) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(nodeName.c_str());
    const size_t length = nodeName.length();
    size_t i = 0;

    while (i < length) {
        const uint8_t first = bytes[i++];
        if (first < 0x20 || first == 0x7F) {
            return false;
        }
        if (first < 0x80) {
            continue;
        }

        size_t continuationCount = 0;
        uint8_t secondMin = 0x80;
        uint8_t secondMax = 0xBF;
        if (first >= 0xC2 && first <= 0xDF) {
            continuationCount = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuationCount = 2;
            if (first == 0xE0) secondMin = 0xA0;  // reject overlong encoding
            if (first == 0xED) secondMax = 0x9F;  // reject UTF-16 surrogates
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuationCount = 3;
            if (first == 0xF0) secondMin = 0x90;  // reject overlong encoding
            if (first == 0xF4) secondMax = 0x8F;  // maximum U+10FFFF
        } else {
            return false;
        }

        if (i + continuationCount > length || bytes[i] < secondMin || bytes[i] > secondMax) {
            return false;
        }
        ++i;
        for (size_t remaining = 1; remaining < continuationCount; ++remaining, ++i) {
            if (bytes[i] < 0x80 || bytes[i] > 0xBF) {
                return false;
            }
        }
    }
    return true;
}

static bool isUnprovisionedNodeName(const String& nodeName) {
    return nodeName.length() == 0 || nodeName == LEGACY_NODE_NAME
            || isV163FullSerialNodeName(nodeName) || !isValidUtf8NodeName(nodeName);
}
const elocDeviceInfo_T& getDeviceInfo() {
    return gElocDeviceInfo;
}

const inferenceConfig_t& getInferenceConfig() {
    return gElocConfig.inferenceConfig;
}

const dutyCycleConfig_t& getDutyCycleConfig() {
    return gElocConfig.dutyCycleConfig;
}

/// Clamp helper
static uint32_t clampU32(uint32_t val, uint32_t minVal, uint32_t maxVal, const char* name) {
    if (val < minVal) {
        ESP_LOGW(TAG, "Clamping %s from %u to min %u", name, val, minVal);
        return minVal;
    }
    if (val > maxVal) {
        ESP_LOGW(TAG, "Clamping %s from %u to max %u", name, val, maxVal);
        return maxVal;
    }
    return val;
}

void validateDutyCycleConfig() {
    static const uint32_t AWAKE_MIN = 20;
    static const uint32_t AWAKE_MAX = 120;
    static const uint32_t SLEEP_MIN = 60;
    static const uint32_t SLEEP_MAX = 900;
    static const uint32_t STARTUP_OVERHEAD_S = 5; // seconds for boot + mic settle

    if (!gElocConfig.dutyCycleConfig.enable) {
        return; // No validation needed if duty cycle is disabled
    }

    gElocConfig.dutyCycleConfig.awakeDurationS = clampU32(
        gElocConfig.dutyCycleConfig.awakeDurationS, AWAKE_MIN, AWAKE_MAX, "awakeDurationS");

    gElocConfig.dutyCycleConfig.sleepDurationS = clampU32(
        gElocConfig.dutyCycleConfig.sleepDurationS, SLEEP_MIN, SLEEP_MAX, "sleepDurationS");

    // Validate inference observation window fits within awake duration
    uint32_t maxObsWindow = gElocConfig.dutyCycleConfig.awakeDurationS - STARTUP_OVERHEAD_S;
    if (gElocConfig.inferenceConfig.observationWindowS > maxObsWindow) {
        ESP_LOGW(TAG, "Clamping observationWindowS from %u to %u (awakeDurationS=%u - overhead=%u)",
            gElocConfig.inferenceConfig.observationWindowS, maxObsWindow,
            gElocConfig.dutyCycleConfig.awakeDurationS, STARTUP_OVERHEAD_S);
        gElocConfig.inferenceConfig.observationWindowS = maxObsWindow;
    }

    // Validate requiredDetections fits within observation window (~1 inference/sec)
    if (gElocConfig.inferenceConfig.requiredDetections > gElocConfig.inferenceConfig.observationWindowS) {
        ESP_LOGW(TAG, "Clamping requiredDetections from %u to %u (observationWindowS)",
            gElocConfig.inferenceConfig.requiredDetections,
            gElocConfig.inferenceConfig.observationWindowS);
        gElocConfig.inferenceConfig.requiredDetections = gElocConfig.inferenceConfig.observationWindowS;
    }

    ESP_LOGI(TAG, "Duty cycle config validated: enable=%d, sleep=%us, awake=%us",
        gElocConfig.dutyCycleConfig.enable,
        gElocConfig.dutyCycleConfig.sleepDurationS,
        gElocConfig.dutyCycleConfig.awakeDurationS);
}

/**************************************************************************************************/

/*************************** Global settings via config file **************************************/

void loadDeviceInfo(const JsonObject& device) {
    gElocDeviceInfo.fileHeader       = device["fileHeader"]       | C_ElocDeviceInfo_Default.fileHeader;
    gElocDeviceInfo.locationCode     = device["locationCode"]     | C_ElocDeviceInfo_Default.locationCode;
    gElocDeviceInfo.locationAccuracy = device["locationAccuracy"] | C_ElocDeviceInfo_Default.locationAccuracy;
    gElocDeviceInfo.nodeName         = device["nodeName"]         | C_ElocDeviceInfo_Default.nodeName;

    // Preserve names deliberately assigned through the app. Only a missing/empty name, the
    // historical placeholder, or the exact full-serial name produced by V1.63 is replaced.
    if (isUnprovisionedNodeName(gElocDeviceInfo.nodeName)) {
        gElocDeviceInfo.nodeName = factoryDefaultNodeName();
        gNodeNameMigrationPending = true;
        ESP_LOGI(TAG, "Using factory-derived node name: %s", gElocDeviceInfo.nodeName.c_str());
    }
}
/**
 * @brief Gate the cpuEnableLightSleep config field on ALLOW_AUTOMATIC_LIGHT_SLEEP.
 *
 * Changing the compiled default to false is not enough on its own: the config cascade is
 * SD -> SPIFFS -> compile-time default, and a key that is PRESENT always wins over the default.
 * Every unit provisioned before the default changed still carries "cpuEnableLightSleep": true
 * in /sdcard/elocConfig.config, so an upgraded device would keep enabling light sleep and keep
 * the RTC-watchdog reset risk with it. Clamping here neutralises those stored values.
 *
 * Call from EVERY path that writes gElocConfig.cpuEnableLightSleep. Today that is loadConfig()
 * — which the app's setConfig also funnels through, see updateConfig() — and
 * setCpuFrequencyConfig(), which the app calls directly and which would otherwise bypass it.
 *
 * No eager migration write is needed: the clamped value is what gets serialised by
 * buildConfigFile(), so the first writeConfig() from any config change rewrites the stale
 * true as false on its own. Until then the file value is simply inert, because nothing reads
 * it without going through here.
 */
static bool clampLightSleep(bool requested) {
#if ALLOW_AUTOMATIC_LIGHT_SLEEP
    return requested;
#else
    if (requested) {
        ESP_LOGW(TAG, "cpuEnableLightSleep=true from stored config is IGNORED: automatic light "
                      "sleep hangs this board and is reset by the RTC watchdog "
                      "(see ALLOW_AUTOMATIC_LIGHT_SLEEP). Forcing false.");
    }
    return false;
#endif
}

void loadConfig(const JsonObject& config) {
    gElocConfig.secondsPerFile                = config["secondsPerFile"]              | C_ElocConfig_Default.secondsPerFile;
    gElocConfig.cpuMaxFrequencyMHZ            = config["cpuMaxFrequencyMHZ"]          | C_ElocConfig_Default.cpuMaxFrequencyMHZ;
    gElocConfig.cpuMinFrequencyMHZ            = config["cpuMinFrequencyMHZ"]          | C_ElocConfig_Default.cpuMinFrequencyMHZ;
    gElocConfig.cpuEnableLightSleep           = clampLightSleep(config["cpuEnableLightSleep"] | C_ElocConfig_Default.cpuEnableLightSleep);
    gElocConfig.bluetoothEnableAtStart        = config["bluetoothEnableAtStart"]      | C_ElocConfig_Default.bluetoothEnableAtStart;
    gElocConfig.bluetoothEnableOnTapping      = config["bluetoothEnableOnTapping"]    | C_ElocConfig_Default.bluetoothEnableOnTapping;
    gElocConfig.bluetoothEnableDuringRecord   = config["bluetoothEnableDuringRecord"] | C_ElocConfig_Default.bluetoothEnableDuringRecord;
    gElocConfig.bluetoothOffTimeoutSeconds    = config["bluetoothOffTimeoutSeconds"]  | C_ElocConfig_Default.bluetoothOffTimeoutSeconds;

    /** persistant log config */
    gElocConfig.logConfig.logToSdCard         = config["logConfig"]["logToSdCard"]    | C_ElocConfig_Default.logConfig.logToSdCard;
    gElocConfig.logConfig.filename            = config["logConfig"]["filename"]       | C_ElocConfig_Default.logConfig.filename;
    gElocConfig.logConfig.maxFiles            = config["logConfig"]["maxFiles"]       | C_ElocConfig_Default.logConfig.maxFiles;
    gElocConfig.logConfig.maxFileSize         = config["logConfig"]["maxFileSize"]    | C_ElocConfig_Default.logConfig.maxFileSize;

    /** Intruder config*/
    gElocConfig.IntruderConfig.detectEnable   = config["intruderCfg"]["enable"]       | C_ElocConfig_Default.IntruderConfig.detectEnable;
    gElocConfig.IntruderConfig.thresholdCnt   = config["intruderCfg"]["threshold"]    | C_ElocConfig_Default.IntruderConfig.thresholdCnt;
    gElocConfig.IntruderConfig.detectWindowMS = config["intruderCfg"]["windowsMs"]    | C_ElocConfig_Default.IntruderConfig.detectWindowMS;
    gElocConfig.IntruderConfig.alarmIntervalS = config["intruderCfg"]["alarmIntervalS"] | C_ElocConfig_Default.IntruderConfig.alarmIntervalS;
    /** battery config*/
    gElocConfig.batteryConfig.updateIntervalMs = config["battery"]["updateIntervalMs"] | C_ElocConfig_Default.batteryConfig.updateIntervalMs;
    gElocConfig.batteryConfig.avgSamples       = config["battery"]["avgSamples"]       | C_ElocConfig_Default.batteryConfig.avgSamples;
    gElocConfig.batteryConfig.avgIntervalMs    = config["battery"]["avgIntervalMs"]    | C_ElocConfig_Default.batteryConfig.avgIntervalMs;
    gElocConfig.batteryConfig.noBatteryMode    = config["battery"]["noBatteryMode"]    | C_ElocConfig_Default.batteryConfig.noBatteryMode;
    /** lora config*/
    gElocConfig.loraConfig.loraEnable          = config["lorawan"]["loraEnable"]       | C_ElocConfig_Default.loraConfig.loraEnable;
    gElocConfig.loraConfig.upLinkIntervalS     = config["lorawan"]["upLinkIntervalS"]  | C_ElocConfig_Default.loraConfig.upLinkIntervalS;
    gElocConfig.loraConfig.loraRegion          = config["lorawan"]["loraRegion"]       | C_ElocConfig_Default.loraConfig.loraRegion;
    gElocConfig.loraConfig.eventCooldownS      = config["lorawan"]["eventCooldownS"]   | C_ElocConfig_Default.loraConfig.eventCooldownS;
    gElocConfig.loraConfig.eventEndTimeoutS    = config["lorawan"]["eventEndTimeoutS"] | C_ElocConfig_Default.loraConfig.eventEndTimeoutS;
    /** inference config*/
    gElocConfig.inferenceConfig.threshold           = config["inference"]["threshold"]           | C_ElocConfig_Default.inferenceConfig.threshold;
    gElocConfig.inferenceConfig.observationWindowS = config["inference"]["observationWindowS"] | C_ElocConfig_Default.inferenceConfig.observationWindowS;
    gElocConfig.inferenceConfig.requiredDetections = config["inference"]["requiredDetections"] | C_ElocConfig_Default.inferenceConfig.requiredDetections;
    /** duty cycle config*/
    gElocConfig.dutyCycleConfig.enable         = config["dutyCycle"]["enable"]         | C_ElocConfig_Default.dutyCycleConfig.enable;
    gElocConfig.dutyCycleConfig.sleepDurationS = config["dutyCycle"]["sleepDurationS"] | C_ElocConfig_Default.dutyCycleConfig.sleepDurationS;
    gElocConfig.dutyCycleConfig.awakeDurationS = config["dutyCycle"]["awakeDurationS"] | C_ElocConfig_Default.dutyCycleConfig.awakeDurationS;
}

MicChannel_t ParseMicChannel(const char* str, MicChannel_t default_value) {
    if (!str) {
        return default_value;
    }
    for (int i=0; i<sizeof(MicChannel_tStrings); i++) {
        if (!strcmp(str, MicChannel_tStrings[i])) {
            return static_cast<MicChannel_t>(i);
        }
    }
    ESP_LOGW(TAG, "Unsupported Mic Channel %s", str);
    return default_value;
}

void loadMicInfo(const JsonObject& micInfo) {
    gMicInfo.MicType         = micInfo["MicType"]         | C_MicInfo_Default.MicType;
    gMicInfo.MicVolume2_pwr  = micInfo["MicVolume2_pwr"]  | C_MicInfo_Default.MicVolume2_pwr;
    gMicInfo.MicSampleRate   = micInfo["MicSampleRate"]   | C_MicInfo_Default.MicSampleRate;
    gMicInfo.MicUseAPLL      = micInfo["MicUseAPLL"]      | C_MicInfo_Default.MicUseAPLL;
    gMicInfo.MicChannel = ParseMicChannel(micInfo["MicChannel"], C_MicInfo_Default.MicChannel);

    updateI2sConfig();
}

bool readConfigFile(const char* filename) {

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "file not present: %s", filename);
        return false;
    } else {

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

        char *input = reinterpret_cast<char*>(malloc(fsize + 1));
        // Check successful before clearing memory
        if (!input) {
            ESP_LOGE(TAG, "Not enough memory for reading %s", filename);
            fclose(f);
            return false;
        }
        memset(input, 0, fsize+1);
        fread(input, fsize, 1, f);

        ESP_LOGI(TAG, "Read this Configuration:");
        printf("%s\n", input);

        DynamicJsonDocument doc(JSON_DOC_SIZE);

        DeserializationError error = deserializeJson(doc, input, fsize);

        if (error) {
            ESP_LOGE(TAG, "Parsing %s failed with %s!", filename, error.c_str());
        }
        JsonObject device = doc["device"];
        loadDeviceInfo(device);

        JsonObject config = doc["config"];
        loadConfig(config);

        JsonObject mic = doc["mic"];
        loadMicInfo(mic);

        free(input);
        fclose(f);
    }
    return true;
}

void readConfig() {
    // setup() supplies the serial loaded from factory NVS before reading config. A fresh SPIFFS
    // config is therefore correct on first boot without coupling this module to ElocSystem.
    gElocDeviceInfo.nodeName = factoryDefaultNodeName();
    gNodeNameMigrationPending = false;

    if (sd_card.isMounted() && ffsutil::fileExist(CFG_FILE_SD)) {
        ESP_LOGI(TAG, "Using test config from sd-card: %s", CFG_FILE_SD);
        readConfigFile(CFG_FILE_SD);
    } else {
        if (ffsutil::fileExist(CFG_FILE)) {
            ESP_LOGI(TAG, "Using config from SPIFFS: %s", CFG_FILE);
            readConfigFile(CFG_FILE);
            if (gNodeNameMigrationPending) {
                ESP_LOGI(TAG, "Persisting factory-derived node name to SPIFFS");
                if (writeConfig()) {
                    gNodeNameMigrationPending = false;
                }
            }
        } else {
            ESP_LOGW(TAG, "No config file found, creating default config!");
            writeConfig();
        }
    }

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Running with this Configuration:");
    printf("%s", cfg.c_str());

    updateI2sConfig();
}

void buildConfigFile(JsonDocument& doc, CfgType cfgType = CfgType::RUNTIME) {
    const elocDeviceInfo_T& ElocDeviceInfo = \
            cfgType == CfgType::DEFAULT_CFG ? C_ElocDeviceInfo_Default : gElocDeviceInfo;
    const elocConfig_T& ElocConfig = \
            cfgType == CfgType::DEFAULT_CFG ? C_ElocConfig_Default : gElocConfig;
    const micInfo_t& MicInfo = \
            cfgType == CfgType::DEFAULT_CFG ? C_MicInfo_Default : gMicInfo;
    doc.clear();
    JsonObject device = doc.createNestedObject("device");
    device["fileHeader"]                  = ElocDeviceInfo.fileHeader.c_str();
    device["locationCode"]                = ElocDeviceInfo.locationCode.c_str();
    device["locationAccuracy"]            = ElocDeviceInfo.locationAccuracy;
    const String nodeName = isUnprovisionedNodeName(ElocDeviceInfo.nodeName)
            ? factoryDefaultNodeName()
            : ElocDeviceInfo.nodeName;
    // Assign the String itself so ArduinoJson copies it into the document. Passing c_str() here
    // would retain a pointer to this local String and corrupt the serialized value after return.
    device["nodeName"]                    = nodeName;

    JsonObject config = doc.createNestedObject("config");
    config["secondsPerFile"]              = ElocConfig.secondsPerFile;
    config["cpuMaxFrequencyMHZ"]          = ElocConfig.cpuMaxFrequencyMHZ;
    config["cpuMinFrequencyMHZ"]          = ElocConfig.cpuMinFrequencyMHZ;
    config["cpuEnableLightSleep"]         = ElocConfig.cpuEnableLightSleep;
    config["bluetoothEnableAtStart"]      = ElocConfig.bluetoothEnableAtStart;
    config["bluetoothEnableOnTapping"]    = ElocConfig.bluetoothEnableOnTapping;
    config["bluetoothEnableDuringRecord"] = ElocConfig.bluetoothEnableDuringRecord;
    config["bluetoothOffTimeoutSeconds"]  = ElocConfig.bluetoothOffTimeoutSeconds;
    config["logConfig"]["logToSdCard"]    = ElocConfig.logConfig.logToSdCard;
    config["logConfig"]["filename"]       = ElocConfig.logConfig.filename;
    config["logConfig"]["maxFiles"]       = ElocConfig.logConfig.maxFiles;
    config["logConfig"]["maxFileSize"]    = ElocConfig.logConfig.maxFileSize;
    config["intruderCfg"]["enable"]       = ElocConfig.IntruderConfig.detectEnable;
    config["intruderCfg"]["threshold"]    = ElocConfig.IntruderConfig.thresholdCnt;
    config["intruderCfg"]["windowsMs"]    = ElocConfig.IntruderConfig.detectWindowMS;
    config["intruderCfg"]["alarmIntervalS"] = ElocConfig.IntruderConfig.alarmIntervalS;
    config["battery"]["updateIntervalMs"] = ElocConfig.batteryConfig.updateIntervalMs;
    config["battery"]["avgSamples"]       = ElocConfig.batteryConfig.avgSamples;
    config["battery"]["avgIntervalMs"]    = ElocConfig.batteryConfig.avgIntervalMs;
    config["battery"]["noBatteryMode"]    = ElocConfig.batteryConfig.noBatteryMode;
    config["lorawan"]["loraEnable"]       = ElocConfig.loraConfig.loraEnable;
    config["lorawan"]["upLinkIntervalS"]  = ElocConfig.loraConfig.upLinkIntervalS;
    config["lorawan"]["loraRegion"]       = ElocConfig.loraConfig.loraRegion;
    config["lorawan"]["eventCooldownS"]   = ElocConfig.loraConfig.eventCooldownS;
    config["lorawan"]["eventEndTimeoutS"] = ElocConfig.loraConfig.eventEndTimeoutS;
    config["inference"]["threshold"]           = ElocConfig.inferenceConfig.threshold;
    config["inference"]["observationWindowS"] = ElocConfig.inferenceConfig.observationWindowS;
    config["inference"]["requiredDetections"] = ElocConfig.inferenceConfig.requiredDetections;
    config["dutyCycle"]["enable"]         = ElocConfig.dutyCycleConfig.enable;
    config["dutyCycle"]["sleepDurationS"] = ElocConfig.dutyCycleConfig.sleepDurationS;
    config["dutyCycle"]["awakeDurationS"] = ElocConfig.dutyCycleConfig.awakeDurationS;

    JsonObject micInfo = doc.createNestedObject("mic");
    micInfo["MicType"]                     = MicInfo.MicType.c_str();
    micInfo["MicVolume2_pwr"]              = MicInfo.MicVolume2_pwr;
    micInfo["MicSampleRate"]               = MicInfo.MicSampleRate;
    micInfo["MicUseAPLL"]                  = MicInfo.MicUseAPLL;
    micInfo["MicChannel"]                  = toString(MicInfo.MicChannel);
}

bool printConfig(String& buf, CfgType cfgType/* = CfgType::RUNTIME*/) {

    DynamicJsonDocument doc(JSON_DOC_SIZE);
    buildConfigFile(doc, cfgType);
    if (serializeJsonPretty(doc, buf) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
        return false;
    }
    return true;
}

bool writeConfigFile(const char* filename) {

    FILE *f = fopen(filename, "w+");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open config file %s!", filename);
        return false;
        // return;
    }

    String buffer;
    printConfig(buffer);
    fprintf(f, "%s", buffer.c_str());
    // BUGME: add scopeguard here to make sure file is closed
    fclose(f);
    return true;
}

bool writeConfig() {
    if (sd_card.isMounted()) {
        if (!writeConfigFile("/sdcard/elocConfig.config.bak")) {
            ESP_LOGE(TAG, "Failed to write config backup to sdcard!");
        }
    }
    if (!writeConfigFile(CFG_FILE)) {
        ESP_LOGE(TAG, "Failed to write config to SPIFFS!");
        return false;
    }
    return true;
}

void clearConfig() {
    //TODO: set config to default
    remove(CFG_FILE);
}

esp_err_t updateConfig(const char* buf, configChangeFlags_t* changeFlags) {
    static StaticJsonDocument<JSON_DOC_SIZE> newCfg;
    newCfg.clear();

    DeserializationError error = deserializeJson(newCfg, buf);
    if (error) {
        ESP_LOGE(TAG, "Parsing config failed with %s!", error.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    if (changeFlags != nullptr) {
        JsonObjectConst newConfig = newCfg["config"];
        changeFlags->cpu = newConfig.containsKey("cpuMaxFrequencyMHZ") ||
                           newConfig.containsKey("cpuMinFrequencyMHZ") ||
                           newConfig.containsKey("cpuEnableLightSleep");
        changeFlags->logConfig = newConfig.containsKey("logConfig");
    }
    static StaticJsonDocument<JSON_DOC_SIZE> doc;
    buildConfigFile(doc);

    jsonutils::merge(doc, newCfg);

    JsonObject device = doc["device"];
    loadDeviceInfo(device);

    JsonObject config = doc["config"];
    loadConfig(config);

    JsonObject mic = doc["mic"];
    loadMicInfo(mic);

    if (!writeConfig()) {
        return ESP_ERR_FLASH_BASE;
    }

    gNodeNameMigrationPending = false;

    return ESP_OK;
}

bool isValidCpuMaxFrequency(int mhz) {
    // ESP32 CPU max clock is PLL-derived; only these values are selectable.
    return (mhz == 80) || (mhz == 160) || (mhz == 240);
}

bool isValidCpuMinFrequency(int mhz) {
    // min clock may also use the crystal-derived low frequencies (40 MHz xtal / N).
    return isValidCpuMaxFrequency(mhz) || (mhz == 40) || (mhz == 20) || (mhz == 10);
}

esp_err_t setCpuFrequencyConfig(int maxFrequencyMHZ, int minFrequencyMHZ, bool enableLightSleep) {
    gElocConfig.cpuMaxFrequencyMHZ  = maxFrequencyMHZ;
    gElocConfig.cpuMinFrequencyMHZ  = minFrequencyMHZ;
    gElocConfig.cpuEnableLightSleep = clampLightSleep(enableLightSleep);
    if (!writeConfig()) {
        return ESP_ERR_FLASH_BASE;
    }
    return ESP_OK;
}
