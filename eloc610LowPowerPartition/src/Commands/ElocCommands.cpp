/*
 * Created on Fri Oct 20 2023
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
#include <stdint.h>
#include <esp_err.h>
#include <esp_log.h>

#include "CmdParser.hpp"
#include "CmdAdvCallback.hpp"

#define ARDUINOJSON_ENABLE_STD_STREAM 1
#include "ArduinoJson.h"
#include "WString.h"

#include "ESP32Time.h"

#include "CmdResponse.hpp"
#include "ElocConfig.hpp"
#include "Battery.hpp"
#include "config.h"


namespace BtCommands {

static const char* TAG = "BtCmds";

/********** BUGME: encapsulate ELOC status and make it threadsafe!!!*/ 
//BUGME: global status
extern bool gRecording;
extern int64_t gTotalUPTimeSinceReboot;  //esp_timer_get_time returns 64-bit time since startup, in microseconds.
extern int64_t gTotalRecordTimeSinceReboot;
extern int64_t gSessionRecordTime;
extern String gSessionIdentifier;
extern float gFreeSpaceGB;
extern ESP32Time timeObject;

//BUGME: global constant from config.h
extern String gFirmwareVersion;

void printStatus(String& buf) {

    StaticJsonDocument<512> doc;
    JsonObject battery = doc.createNestedObject("battery");
    battery["type"]                = Battery::GetInstance().getBatType();
    battery["state"]               = Battery::GetInstance().getState();
    battery["SoC[%]"]              = Battery::GetInstance().getSoC();
    battery["voltage[V]"]          = Battery::GetInstance().getVoltage();

    JsonObject session = doc.createNestedObject("session");
    session["identifier"]                = gSessionIdentifier;
    session["recordingState"]            = String(gRecording);
    session["recordingTime[h]"]          = String((float)gSessionRecordTime / 1000 / 1000 / 60 / 60);
    
    JsonObject device = doc.createNestedObject("device");
    device["firmware"]                   = gFirmwareVersion;
    device["timeStamp"]                  = String((float)esp_timer_get_time() / 1000 / 1000 / 60 / 60);
    device["totalRecordingTime[h]"]      = String(((float)gTotalRecordTimeSinceReboot + gSessionRecordTime) / 1000 / 1000 / 60 / 60);
    device["SdCardFreeSpace[GB]"]        = String(gFreeSpaceGB);

    if (serializeJsonPretty(doc, buf) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
    }
}

void cmd_GetStatus(CmdParser* cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    // write directly to output buffer to avoid reallocation
    String& status = resp.getPayload(); 
    printStatus(status);
    resp.setResultSuccess(status);
}
/****************************************************************************************/


void cmd_SetConfig(CmdParser *cmdParser) {

    CmdResponse& resp = CmdResponse::getInstance();
    const char* cfg = cmdParser->getValueFromKey("cfg");
    if (!cfg) {
        const char* errMsg = "Missing key 'cfg'";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        return;
    }
    ESP_LOGI(TAG, "updating config with %s", cfg);
    esp_err_t err = updateConfig(cfg);
    resp.setResult(err);
    return; 

}


void cmd_GetConfig(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    String& cfg = resp.getPayload(); // write directly to output buffer to avoid reallocation
    if (!printConfig(cfg)) {
        resp.setError(ESP_ERR_NO_MEM, "Failed to serialize JSON config!");
    }
    resp.setResultSuccess(cfg);
    return;
}
void cmd_DelConfig(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    ESP_LOGW(TAG, "Clearing config only deletes the cfg file");
    clearConfig();
    resp.setResultSuccess("cfg deleted");
    return;
}

bool initCommands(CmdAdvCallback<MAX_COMMANDS>& cmdCallback) {
    bool success = true;
    success &= cmdCallback.addCmd("setConfig", &cmd_SetConfig, "Write config key as json, e.g. setConfig#cfg={\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("getConfig", &cmd_GetConfig, "Read config as json, e.g. getConfig --> return{\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("delConfig", &cmd_DelConfig, "Delete the current config file. Current config is not reset to default until next reboot");
    if (!success) {
        ESP_LOGE(TAG, "Failed to add all BT commands!");
    }
    return success;
}

} // namespace BtCommands
