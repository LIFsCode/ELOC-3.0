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

#include "Esp.h"
#include <FS.h>
#include "SPIFFS.h"

#include "utils/ffsutils.h"
#include "config.h"
#include "ElocConfig.hpp"

static const char* TAG = "CONFIG";

static const uint32_t JSON_DOC_SIZE = 1024;

static const char* CFG_FILE = "/spiffs/eloc.config";
static const char* CFG_FILE_SD = "/sdcard/eloctest.txt";


//BUGME: encapsulate these in a struct & implement a getter
static const micInfo_t C_MicInfo_Default {
    .MicType="ns",
    .MicBitShift=11,
    .MicSampleRate = I2S_DEFAULT_SAMPLE_RATE,
    .MicUseAPLL = true,
    .MicUseTimingFix = true,
    .MicGPSCoords="ns",
    .MicPointingDirectionDegrees="ns",
    .MicHeight="ns",
    .MicMountType="ns",
};
micInfo_t gMicInfo = C_MicInfo_Default;

void setMicBitShift(int MicBitShift) {
    gMicInfo.MicBitShift = MicBitShift;
}
void setMicType(String MicType) {
    gMicInfo.MicType = MicType;
}

const micInfo_t& getMicInfo() {
    return gMicInfo;
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
    .testI2SClockInput = false
};
elocConfig_T gElocConfig = C_ElocConfig_Default;
const elocConfig_T& getConfig() {
    return gElocConfig;
}

bool setSampleRate(int MicSampleRate) {
    if (MicSampleRate <= 0) {
        ESP_LOGE(TAG, "Invalid Sample rate %d! Ignoring setting\n", MicSampleRate);
        return false;
    }
    //TODO: should we check for a max. sample rate?
    gMicInfo.MicSampleRate = MicSampleRate;
    return true;
}
bool setSecondsPerFile(int secondsPerFile) {
    if (secondsPerFile <= 0) {
        ESP_LOGE(TAG, "Invalid Seconds per File %d! Ignoring setting\n", secondsPerFile);
        return false;
    }
    gElocConfig.secondsPerFile = secondsPerFile;
    return true;
}
void setBluetoothOnOrOffDuringRecord(bool MicBluetoothOnOrOff) {
    gElocConfig.bluetoothEnableDuringRecord = MicBluetoothOnOrOff;
}

static const elocDeviceInfo_T C_ElocDeviceInfo_Default {
    .location = "not_set",
    .locationCode = "unknown", 
    .locationAccuracy = "99",
    .nodeName = "ELOC_NONAME",
};
elocDeviceInfo_T gElocDeviceInfo = C_ElocDeviceInfo_Default;
const elocDeviceInfo_T& getDeviceInfo() {
    return gElocDeviceInfo;
}

void setLocationName(const String& location) {
    gElocDeviceInfo.location = location;
}
void setLocationSettings(const String& code, const String& accuracy) {
    gElocDeviceInfo.locationCode = code;
    gElocDeviceInfo.locationAccuracy = accuracy;
}

bool setNodeName(const String& nodeName) {
    if (nodeName.length() == 0) {
        ESP_LOGE(TAG, "Invalid Node Name! Node Name must at least hold 1 character!");
        return false;
    }
    gElocDeviceInfo.nodeName = nodeName;
    return true;
}

/**************************************************************************************************/

/*************************** Global settings via config file **************************************/
//BUGME: handle this through file system
extern bool gMountedSDCard;


void loadDevideInfo(const JsonObject& device) {
    gElocDeviceInfo.location         = device["location"]         | C_ElocDeviceInfo_Default.location; 
    gElocDeviceInfo.locationCode     = device["locationCode"]     | C_ElocDeviceInfo_Default.locationCode;
    gElocDeviceInfo.locationAccuracy = device["locationAccuracy"] | C_ElocDeviceInfo_Default.locationAccuracy;
    gElocDeviceInfo.nodeName         = device["nodeName"]         | C_ElocDeviceInfo_Default.nodeName;
}
void loadConfig(const JsonObject& config) {
    gElocConfig.secondsPerFile              = config["secondsPerFile"]              | C_ElocConfig_Default.secondsPerFile;             
    gElocConfig.listenOnly                  = config["listenOnly"]                  | C_ElocConfig_Default.listenOnly;            
    gElocConfig.cpuMaxFrequencyMHZ          = config["cpuMaxFrequencyMHZ"]          | C_ElocConfig_Default.cpuMaxFrequencyMHZ;    
    gElocConfig.cpuMinFrequencyMHZ          = config["cpuMinFrequencyMHZ"]          | C_ElocConfig_Default.cpuMinFrequencyMHZ;    
    gElocConfig.cpuEnableLightSleep         = config["cpuEnableLightSleep"]         | C_ElocConfig_Default.cpuEnableLightSleep;   
    gElocConfig.bluetoothEnableAtStart      = config["bluetoothEnableAtStart"]      | C_ElocConfig_Default.bluetoothEnableAtStart;  
    gElocConfig.bluetoothEnableOnTapping    = config["bluetoothEnableOnTapping"]    | C_ElocConfig_Default.bluetoothEnableOnTapping; 
    gElocConfig.bluetoothEnableDuringRecord = config["bluetoothEnableDuringRecord"] | C_ElocConfig_Default.bluetoothEnableDuringRecord;
    gElocConfig.bluetoothOffTimeoutSeconds  = config["bluetoothOffTimeoutSeconds"]  | C_ElocConfig_Default.bluetoothOffTimeoutSeconds;
}

void loadMicInfo(const JsonObject& micInfo) {
    gMicInfo.MicType                     = micInfo["MicType"]                     | C_MicInfo_Default.MicType; 
    gMicInfo.MicBitShift                 = micInfo["MicBitShift"]                 | C_MicInfo_Default.MicBitShift;             
    gMicInfo.MicSampleRate               = micInfo["MicSampleRate"]               | C_MicInfo_Default.MicSampleRate;   
    gMicInfo.MicUseAPLL                  = micInfo["MicUseAPLL"]                  | C_MicInfo_Default.MicUseAPLL;             
    gMicInfo.MicUseTimingFix             = micInfo["MicUseTimingFix"]             | C_MicInfo_Default.MicUseTimingFix;
    gMicInfo.MicGPSCoords                = micInfo["MicGPSCoords"]                | C_MicInfo_Default.MicGPSCoords;           
    gMicInfo.MicPointingDirectionDegrees = micInfo["MicPointingDirectionDegrees"] | C_MicInfo_Default.MicPointingDirectionDegrees;
    gMicInfo.MicHeight                   = micInfo["MicHeight"]                   | C_MicInfo_Default.MicHeight;
    gMicInfo.MicMountType                = micInfo["MicMountType"]                | C_MicInfo_Default.MicMountType;
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
    if (gMountedSDCard && ffsutil::fileExist(CFG_FILE_SD)) {
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
    
    i2s_mic_Config.sample_rate = gMicInfo.MicSampleRate;
    if (gMicInfo.MicSampleRate <= 32000) { // my wav files sound wierd if apll clock raate is > 32kh. So force non-apll clock if >32khz
        i2s_mic_Config.use_apll = gMicInfo.MicUseAPLL;
        ESP_LOGI(TAG, "Sample Rate %u is < 32khz USE APLL Clock %d", gMicInfo.MicSampleRate, gMicInfo.MicUseAPLL);
    } else {
        i2s_mic_Config.use_apll = false;
        ESP_LOGI(TAG, "Sample Rate is %u > 32khz Forcing NO_APLL ", gMicInfo.MicSampleRate);
    }
}

void buildConfigFile(JsonDocument& doc) {
    doc.clear();
    JsonObject device = doc.createNestedObject("device");
    device["location"]                    = gElocDeviceInfo.location.c_str();
    device["locationCode"]                = gElocDeviceInfo.locationCode.c_str();
    device["locationAccuracy"]            = gElocDeviceInfo.locationAccuracy.c_str();
    device["nodeName"]                    = gElocDeviceInfo.nodeName.c_str();

    JsonObject config = doc.createNestedObject("config");
    config["secondsPerFile"]              = gElocConfig.secondsPerFile;
    config["listenOnly"]                  = gElocConfig.listenOnly;
    config["cpuMaxFrequencyMHZ"]          = gElocConfig.cpuMaxFrequencyMHZ;
    config["cpuMinFrequencyMHZ"]          = gElocConfig.cpuMinFrequencyMHZ;
    config["cpuEnableLightSleep"]         = gElocConfig.cpuEnableLightSleep;
    config["bluetoothEnableAtStart"]      = gElocConfig.bluetoothEnableAtStart;
    config["bluetoothEnableOnTapping"]    = gElocConfig.bluetoothEnableOnTapping;
    config["bluetoothEnableDuringRecord"] = gElocConfig.bluetoothEnableDuringRecord;
    config["bluetoothOffTimeoutSeconds"]  = gElocConfig.bluetoothOffTimeoutSeconds;
    
    JsonObject micInfo = doc.createNestedObject("mic");
    micInfo["MicType"]                     = gMicInfo.MicType.c_str();
    micInfo["MicBitShift"]                 = gMicInfo.MicBitShift;
    micInfo["MicSampleRate"]               = gMicInfo.MicSampleRate;
    micInfo["MicUseAPLL"]                  = gMicInfo.MicUseAPLL;
    micInfo["MicUseTimingFix"]             = gMicInfo.MicUseTimingFix;
    micInfo["MicGPSCoords"]                = gMicInfo.MicGPSCoords.c_str();
    micInfo["MicPointingDirectionDegrees"] = gMicInfo.MicPointingDirectionDegrees.c_str();
    micInfo["MicHeight"]                   = gMicInfo.MicHeight.c_str();
    micInfo["MicMountType"]                = gMicInfo.MicMountType.c_str();
}

void printConfig(String& buf) {

    StaticJsonDocument<JSON_DOC_SIZE> doc;
    buildConfigFile(doc);
    if (serializeJsonPretty(doc, buf) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
    }
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

    if (gMountedSDCard) {
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


static void merge(JsonVariant dst, JsonVariantConst src) {
    if (src.is<JsonObjectConst>()) {
        for (JsonPairConst kvp : src.as<JsonObjectConst>()) {
            if (dst[kvp.key()])
                merge(dst[kvp.key()], kvp.value());
            else
                dst[kvp.key()] = kvp.value();
        }
    } else {
        dst.set(src);
    }
}

esp_err_t updateConfig(const String& buf) {
    static StaticJsonDocument<JSON_DOC_SIZE> newCfg;
    newCfg.clear();

    DeserializationError error = deserializeJson(newCfg, buf);
    if (error) {
        ESP_LOGE(TAG, "Parsing config failed with %s!", error.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    static StaticJsonDocument<JSON_DOC_SIZE> doc;
    buildConfigFile(doc);

    merge(doc, newCfg);

    JsonObject device = doc["device"];
    loadDevideInfo(device);
    
    JsonObject config = doc["config"];
    loadConfig(config);

    JsonObject mic = doc["mic"];
    loadMicInfo(mic);

    writeConfig();

    return ESP_OK;
}
