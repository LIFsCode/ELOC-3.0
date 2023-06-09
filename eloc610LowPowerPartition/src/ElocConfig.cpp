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

#include "config.h"
#include "ElocConfig.hpp"

static const char* TAG = "CONFIG";

static const char* CFG_FILE = "/spiffs/eloc.config";
static const char* CFG_FILE_SD = "/sdcard/eloctest.txt";


//BUGME: encapsulate these in a struct & implement a getter
micInfo_t gMicInfo {
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
String gSyncPhoneOrGoogle; //will be either G or P (google or phone).
long gLastSystemTimeUpdate; // local system time of last time update PLUS minutes since last phone update 


elocConfig_T gElocConfig {
    .secondsPerFile = 60,
    .listenOnly = false,
    // Power management
    .cpuMaxFrequencyMHZ = 80,    // minimum 80
    .cpuMinFrequencyMHZ = 10,
    .cpuEnableLightSleep = true,
    .bluetoothEnableAtStart = false,
    .bluetoothEnableOnTapping = true,
    .bluetoothEnableDuringRecord = false,
    .testI2SClockInput = false
};
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

elocDeviceInfo_T gElocDeviceInfo {
    .location = "not_set",
    .locationCode = "unknown", 
    .locationAccuracy = "99",
    .nodeName = "ELOC_NONAME",
};
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
    gElocDeviceInfo.location         = device["location"].as<String>(); 
    gElocDeviceInfo.locationCode     = device["locationCode"].as<String>();
    gElocDeviceInfo.locationAccuracy = device["locationAccuracy"].as<String>();
    gElocDeviceInfo.nodeName         = device["nodeName"].as<String>();
}
void loadConfig(const JsonObject& config) {
    gElocConfig.secondsPerFile              = config["secondsPerFile"];             
    gElocConfig.listenOnly                  = config["listenOnly"];            
    gElocConfig.cpuMaxFrequencyMHZ          = config["cpuMaxFrequencyMHZ"];    
    gElocConfig.cpuMinFrequencyMHZ          = config["cpuMinFrequencyMHZ"];    
    gElocConfig.cpuEnableLightSleep         = config["cpuEnableLightSleep"];   
    gElocConfig.bluetoothEnableAtStart      = config["bluetoothEnableAtStart"];  
    gElocConfig.bluetoothEnableOnTapping    = config["bluetoothEnableOnTapping"]; 
    gElocConfig.bluetoothEnableDuringRecord = config["bluetoothEnableDuringRecord"];
}

void loadMicInfo(const JsonObject& micInfo) {
    gMicInfo.MicType                     = micInfo["MicType"].as<String>(); 
    gMicInfo.MicBitShift                 = micInfo["MicBitShift"];             
    gMicInfo.MicSampleRate               = micInfo["MicSampleRate"];   
    gMicInfo.MicUseAPLL                  = micInfo["MicUseAPLL"];             
    gMicInfo.MicUseTimingFix             = micInfo["MicUseTimingFix"];
    gMicInfo.MicGPSCoords                = micInfo["MicGPSCoords"].as<String>();           
    gMicInfo.MicPointingDirectionDegrees = micInfo["MicPointingDirectionDegrees"].as<String>();
    gMicInfo.MicHeight                   = micInfo["MicHeight"].as<String>();
    gMicInfo.MicMountType                = micInfo["MicMountType"].as<String>();
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
        fclose(f);

        static StaticJsonDocument<1024> doc;

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
    }
    return true;
}

void readConfig() {

    if (gMountedSDCard && (access(CFG_FILE_SD, F_OK) == 0)) {
        ESP_LOGI(TAG, "Using test config from sd-card: %s", CFG_FILE_SD);
        readConfigFile(CFG_FILE_SD);
    }
    else {
        if (access(CFG_FILE, F_OK)==0) {
            readConfigFile(CFG_FILE);
        }
        else {
            ESP_LOGW(TAG, "No config file found, creating default config!");
            bool writeConfig();
        }
    }
    i2s_mic_Config.sample_rate = gMicInfo.MicSampleRate;
    if (gMicInfo.MicSampleRate <= 32000) { // my wav files sound wierd if apll clock raate is > 32kh. So force non-apll clock if >32khz
        i2s_mic_Config.use_apll = gMicInfo.MicUseAPLL;
        ESP_LOGI(TAG, "Sample Rate is < 32khz USE APLL Clock %d", gMicInfo.MicUseAPLL);
    } else {
        i2s_mic_Config.use_apll = false;
        ESP_LOGI(TAG, "Sample Rate is > 32khz Forcing NO_APLL ");
    }
}


bool writeConfigFile(const char* filename) {

    FILE *f = fopen("/sdcard/elocConfig.config.bak", "w+");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open config file on sd card!");
        return false;
        // return;
    } 
    StaticJsonDocument<1024> doc;

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

    // Serialize JSON to file
    const size_t BUF_SIZE = 2048;
    char * charBuffer = reinterpret_cast<char*>(malloc(BUF_SIZE));
    if (!charBuffer) {
        ESP_LOGE(TAG, "Not enough heap memory for writing %s", filename);
        fclose(f);
        return false;
    }
    if (serializeJsonPretty(doc, charBuffer, BUF_SIZE) == 0) {
        ESP_LOGE(TAG, "Failed to write to file");
    }
    fprintf(f, "%s", charBuffer);
    // BUGME: add scopeguard here to make sure file is closed
    free(charBuffer);
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
