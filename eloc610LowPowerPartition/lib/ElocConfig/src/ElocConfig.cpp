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
static const uint32_t JSON_DOC_SIZE = 1024;
static const char* CFG_FILE = "/spiffs/eloc.config";
static const char* CFG_FILE_SD = "/sdcard/eloctest.txt";

extern SDCardSDIO sd_card;

//BUGME: encapsulate these in a struct & implement a getter
static const micInfo_t C_MicInfo_Default {
    .MicType="ns",
    .MicBitShift=11,
    .MicSampleRate = I2S_DEFAULT_SAMPLE_RATE,
    .MicUseAPLL = true,
    .MicUseTimingFix = true,
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
void upateI2sConfig() {
    i2s_mic_Config.sample_rate = gMicInfo.MicSampleRate;
    i2s_mic_Config.use_apll = gMicInfo.MicUseAPLL;
    if (i2s_mic_Config.sample_rate == 0) {
        ESP_LOGI(TAG, "Resetting invalid sample rate to default = %d", I2S_DEFAULT_SAMPLE_RATE);
        i2s_mic_Config.sample_rate = I2S_DEFAULT_SAMPLE_RATE;
    }
    switch (gMicInfo.MicChannel) {
        // TODO: check about potential wrong channel selection in ESP IDF lib
        case MicChannel_t::Left:
            i2s_mic_Config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
            break;
        case MicChannel_t::Right:
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
    .secondsPerFile = 60,
    .listenOnly = false,
    // Power management
    .cpuMaxFrequencyMHZ = 80,    // minimum 80
    .cpuMinFrequencyMHZ = 10,
    .cpuEnableLightSleep = true,
    .bluetoothEnableAtStart = true,
    .bluetoothEnableOnTapping = true,
    .bluetoothEnableDuringRecord = true,
    .bluetoothOffTimeoutSeconds = 60,
    .testI2SClockInput = false,
    .logConfig = {
        .logToSdCard = true,
        .filename = "/sdcard/log/eloc.log",
        .maxFiles = 10,
        .maxFileSize = 5*1024*1024,
    },
    .IntruderConfig = {
        .detectEnable = false,
        .thresholdCnt = INTRUDER_DETECTION_THRSH,
        .detectWindowMS = 2000,
    },
    .batteryConfig = {
        .updateIntervalMs = 10*60*1000, //10 minutes
        .avgSamples = 10,
        .avgIntervalMs = 0,
        .noBatteryMode  = false,
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
    .nodeName = "ELOC_NONAME",
};
elocDeviceInfo_T gElocDeviceInfo = C_ElocDeviceInfo_Default;
const elocDeviceInfo_T& getDeviceInfo() {
    return gElocDeviceInfo;
}

/**************************************************************************************************/

/*************************** Global settings via config file **************************************/

void loadDevideInfo(const JsonObject& device) {
    gElocDeviceInfo.fileHeader       = device["fileHeader"]       | C_ElocDeviceInfo_Default.fileHeader;
    gElocDeviceInfo.locationCode     = device["locationCode"]     | C_ElocDeviceInfo_Default.locationCode;
    gElocDeviceInfo.locationAccuracy = device["locationAccuracy"] | C_ElocDeviceInfo_Default.locationAccuracy;
    gElocDeviceInfo.nodeName         = device["nodeName"]         | C_ElocDeviceInfo_Default.nodeName;
}
void loadConfig(const JsonObject& config) {
    gElocConfig.secondsPerFile                = config["secondsPerFile"]              | C_ElocConfig_Default.secondsPerFile;
    gElocConfig.listenOnly                    = /* TODO: unusedconfig["listenOnly"] |*/ C_ElocConfig_Default.listenOnly;
    gElocConfig.cpuMaxFrequencyMHZ            = config["cpuMaxFrequencyMHZ"]          | C_ElocConfig_Default.cpuMaxFrequencyMHZ;
    gElocConfig.cpuMinFrequencyMHZ            = config["cpuMinFrequencyMHZ"]          | C_ElocConfig_Default.cpuMinFrequencyMHZ;
    gElocConfig.cpuEnableLightSleep           = config["cpuEnableLightSleep"]         | C_ElocConfig_Default.cpuEnableLightSleep;
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
    /** battery config*/
    gElocConfig.batteryConfig.updateIntervalMs = config["battery"]["updateIntervalMs"] | C_ElocConfig_Default.batteryConfig.updateIntervalMs;
    gElocConfig.batteryConfig.avgSamples       = config["battery"]["avgSamples"]       | C_ElocConfig_Default.batteryConfig.avgSamples;
    gElocConfig.batteryConfig.avgIntervalMs    = config["battery"]["avgIntervalMrs"]   | C_ElocConfig_Default.batteryConfig.avgIntervalMs;
    gElocConfig.batteryConfig.noBatteryMode    = config["battery"]["noBatteryMode"]    | C_ElocConfig_Default.batteryConfig.noBatteryMode;
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
    gMicInfo.MicBitShift     = micInfo["MicBitShift"]     | C_MicInfo_Default.MicBitShift;
    gMicInfo.MicSampleRate   = micInfo["MicSampleRate"]   | C_MicInfo_Default.MicSampleRate;
    gMicInfo.MicUseAPLL      = micInfo["MicUseAPLL"]      | C_MicInfo_Default.MicUseAPLL;
    gMicInfo.MicUseTimingFix = micInfo["MicUseTimingFix"] | C_MicInfo_Default.MicUseTimingFix;
    gMicInfo.MicChannel = ParseMicChannel(micInfo["MicChannel"], C_MicInfo_Default.MicChannel);

    upateI2sConfig();
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
        memset(input, 0, fsize+1);
        if (!input) {
            ESP_LOGE(TAG, "Not enough memory for reading %s", filename);
            fclose(f);
            return false;
        }
        fread(input, fsize, 1, f);

        ESP_LOGI(TAG, "Read this Configuration:");
        printf(input);

        StaticJsonDocument<JSON_DOC_SIZE> doc;

        DeserializationError error = deserializeJson(doc, input, fsize);

        if (error) {
            ESP_LOGE(TAG, "Parsing %s failed with %s!", filename, error.c_str());
        }
        JsonObject device = doc["device"];
        loadDevideInfo(device);

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
    if (sd_card.isMounted() && ffsutil::fileExist(CFG_FILE_SD)) {
        ESP_LOGI(TAG, "Using test config from sd-card: %s", CFG_FILE_SD);
        readConfigFile(CFG_FILE_SD);
    }
    else {
        if (ffsutil::fileExist(CFG_FILE)) {
            readConfigFile(CFG_FILE);
        }
        else {
            ESP_LOGW(TAG, "No config file found, creating default config!");
            writeConfig();
        }
    }

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Running with this Configuration:");
    printf(cfg.c_str());

    upateI2sConfig();

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
    device["nodeName"]                    = ElocDeviceInfo.nodeName.c_str();

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
    config["battery"]["updateIntervalMs"] = ElocConfig.batteryConfig.updateIntervalMs;
    config["battery"]["avgSamples"]       = ElocConfig.batteryConfig.avgSamples;
    config["battery"]["avgIntervalMs"]    = ElocConfig.batteryConfig.avgIntervalMs;
    config["battery"]["noBatteryMode"]    = ElocConfig.batteryConfig.noBatteryMode;


    JsonObject micInfo = doc.createNestedObject("mic");
    micInfo["MicType"]                     = MicInfo.MicType.c_str();
    micInfo["MicBitShift"]                 = MicInfo.MicBitShift;
    micInfo["MicSampleRate"]               = MicInfo.MicSampleRate;
    micInfo["MicUseAPLL"]                  = MicInfo.MicUseAPLL;
    micInfo["MicUseTimingFix"]             = MicInfo.MicUseTimingFix;
    micInfo["MicChannel"]                  = toString(MicInfo.MicChannel);
}

bool printConfig(String& buf, CfgType cfgType/* = CfgType::RUNTIME*/) {

    StaticJsonDocument<JSON_DOC_SIZE> doc;
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

esp_err_t updateConfig(const char* buf) {
    static StaticJsonDocument<JSON_DOC_SIZE> newCfg;
    newCfg.clear();

    DeserializationError error = deserializeJson(newCfg, buf);
    if (error) {
        ESP_LOGE(TAG, "Parsing config failed with %s!", error.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    static StaticJsonDocument<JSON_DOC_SIZE> doc;
    buildConfigFile(doc);

    jsonutils::merge(doc, newCfg);

    JsonObject device = doc["device"];
    loadDevideInfo(device);

    JsonObject config = doc["config"];
    loadConfig(config);

    JsonObject mic = doc["mic"];
    loadMicInfo(mic);

    if (!writeConfig()) {
        return ESP_ERR_FLASH_BASE;
    }

    return ESP_OK;
}
