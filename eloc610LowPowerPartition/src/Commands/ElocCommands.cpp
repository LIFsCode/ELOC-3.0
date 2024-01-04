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

#include "ArduinoJson.h"
#include "WString.h"
#include "CmdParser.hpp"
#include "CmdAdvCallback.hpp"

#include "ESP32Time.h"

#include "SDCardSDIO.h" 
#include "CmdResponse.hpp"
#include "ElocConfig.hpp"
#include "ElocStatus.hpp"
#include "Battery.hpp"
#include "config.h"
#include "utils/macros.hpp"
#include "utils/logging.hpp"




/********** BUGME: encapsulate ELOC status and make it threadsafe!!!*/ 
//BUGME: global status
extern ESP32Time timeObject;

namespace BtCommands {
static const char* TAG = "BtCmds";

//TODO: this can be moved to a shared header if it is to be used in other places for enum to JSON conversion
template <typename T>
void addEnum(JsonObject& object, T val) {
    object["val"] = static_cast<int>(val);
    object["state"] = toString(val);
}

void printStatus(String& buf) {

    StaticJsonDocument<512> doc;
    JsonObject battery = doc.createNestedObject("battery");
    battery["type"]                = Battery::GetInstance().getBatType();
    battery["state"]               = Battery::GetInstance().getState();
    battery["SoC[%]"]              = round(Battery::GetInstance().getSoC(), 1);
    battery["voltage[V]"]          = round(Battery::GetInstance().getVoltage(), 2);

    JsonObject session = doc.createNestedObject("session");
    session["identifier"]          = gSessionIdentifier;
    JsonObject recordingState = session.createNestedObject("recordingState");
    
    if (wav_writer != nullptr){
        recordingState["val"], wav_writer->get_mode_int();
        recordingState["state"] = wav_writer->get_mode_str();
    }
    else {
        recordingState["val"] = 99;
        recordingState["state"] = "unknown";
    }
    session["recordingTime[h]"]    = (float)gSessionRecordTime / 1000 / 1000 / 60 / 60;
    JsonObject ai = session.createNestedObject("ai");
    ai["state"]                   = ai_run_enable;
    JsonObject device = doc.createNestedObject("device");
    device["firmware"]                   = gFirmwareVersion;
    device["Uptime[h]"]                  = (float)esp_timer_get_time() / 1000 / 1000 / 60 / 60;
    device["totalRecordingTime[h]"]      = ((float)gTotalRecordTimeSinceReboot + gSessionRecordTime) / 1000 / 1000 / 60 / 60;

    float sdCardSizeGB = 0;
    float sdCardFreeSpaceGB = 0;
  
    if (ElocSystem::GetInstance().isSDCardMounted()) {
        sdCardSizeGB = ElocSystem::GetInstance().getSdCard().getCapacityMB()/1024;
        sdCardFreeSpaceGB = ElocSystem::GetInstance().getSdCard().freeSpaceGB();

    }
    device["SdCardSize[GB]"]             = round(sdCardSizeGB,2);
    device["SdCardFreeSpace[GB]"]        = round(sdCardFreeSpaceGB,2);
    device["SdCardFreeSpace[%]"]         = round(sdCardFreeSpaceGB/sdCardSizeGB*100.0,2);

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
    const char* typ = cmdParser->getValueFromKey("cfgType");
    CfgType cfgType = CfgType::RUNTIME;
    if ((typ != NULL) && !strcasecmp(typ, "DEFAULT")) {
        ESP_LOGI(TAG, "reading default config");
        cfgType = CfgType::DEFAULT_CFG;
    }
    String& cfg = resp.getPayload(); // write directly to output buffer to avoid reallocation
    if (!printConfig(cfg, cfgType)) {
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
void cmd_SetLogPersistent(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* cfg = cmdParser->getValueFromKey("cfg");
    if (!cfg) {
        const char* errMsg = "Missing key 'cfg'";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        return;
    }
    ESP_LOGI(TAG, "updating log cfg with %s", cfg);
    esp_err_t err = Logging::updateConfig(cfg);
    if (err != ESP_OK) {
        resp.setError(ESP_ERR_INVALID_ARG, "Failed to update logging config!");
        return;
    }
    String& newCfg = resp.getPayload(); // write directly to output buffer to avoid reallocation
    Logging::printLogConfig(newCfg);
    resp.setResultSuccess(newCfg);
    return;
}


long getTimeFromTimeObjectMS() {
    return(timeObject.getEpoch()*1000L+timeObject.getMillis());

}

void cmd_SetTime(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* time = cmdParser->getValueFromKey("time");
    if (!time) {
        const char* errMsg = "Missing key 'time'";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        return;
    }
    ESP_LOGI(TAG, "updating time with %s", time);
    StaticJsonDocument<256> timeCfg;

    DeserializationError error = deserializeJson(timeCfg, time);
    if (error) {
        ESP_LOGE(TAG, "Parsing time config failed with %s!", error.c_str());
        resp.setError(ESP_ERR_INVALID_ARG, error.c_str());
        return;
    }
    if (!timeCfg.containsKey("seconds")) {
        resp.setError(ESP_ERR_INVALID_ARG, "Missing JSON key 'seconds'");
        return;
    }
    long seconds = timeCfg["seconds"];
    long milliseconds = timeCfg["ms"];
    const char* type = timeCfg["type"] | "?";
    long timeZone_offset = timeCfg["timezone"] | TIMEZONE_OFFSET;
    ESP_LOGI(TAG, "timestamp in from type %s Time Zone: %ld sec: %ld millisec: %ld", type, timeZone_offset, seconds, milliseconds);
    // TODO: why is this needed and what should it be used for?
    // const char* minutesSinceSync = "";//serialIN.substring(11, serialIN.indexOf("___"));
    
    timeObject.setTime(seconds + (timeZone_offset * 60L * 60L), milliseconds * 1000);
    // timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
    //  timestamps coming in from android are always GMT (minus 7 hrs)
    //  if I not add timezone then timeobject is off
    //  so timeobject does not seem to be adding timezone to system time.
    //  timestamps are in gmt+0, so timestamp convrters

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    time_us = time_us / 1000;

    // ESP_LOGI(TAG, "atol(minutesSinceSync.c_str()) *60L*1000L "+String(atol(minutesSinceSync.c_str()) *60L*1000L));
    // gLastSystemTimeUpdate = getTimeFromTimeObjectMS() - (atol(minutesSinceSync.c_str()) * 60L * 1000L);
    // ESP_LOGI(TAG, "timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
    ESP_LOGI(TAG, "new timestamp from new sys time (local time) %lld", time_us  ); //this is 7 hours too slow!
    ESP_LOGI(TAG,"new timestamp from timeobJect (local time) %ld",getTimeFromTimeObjectMS());
    ESP_LOGI(TAG,"new time set to (local time) %s",timeObject.getDateTime().c_str());

    String& response = resp.getPayload();
    response = "{\"Time[ms]\" : ";
    response += getTimeFromTimeObjectMS();
    response += "}";
    resp.setResultSuccess(response);
    return;
}

void cmd_SetRecordMode(CmdParser* cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* req_mode = cmdParser->getValueFromKey("mode");
    // rec_req_t rec_req;

    const char* new_mode = "";
    auto new_ai_mode = true;
    auto ai_mode_change = false;

    if (!req_mode) {
        ESP_LOGI(TAG, "setRecordMode requested <none>");
        
        auto wav_write_mode = wav_writer->get_mode();
        /**
         * If no explicit mode is set, recording mode is toggled, no change to AI mode
         * @warning This is debug feature, shouldn't be generally used
         */
        if(wav_write_mode == WAVFileWriter::Mode::disabled){
            new_mode = "recordOn(AI ?)";
            wav_writer->set_mode(WAVFileWriter::Mode::continuous);
        } else {
            new_mode = "recordOff(AI ?)";
            wav_writer->set_mode(WAVFileWriter::Mode::disabled);
        }
    }
    else {
        // Change this string to 'continuous' ?
        ESP_LOGI(TAG, "setRecordMode requested %s", req_mode);

        if (!strcasecmp(req_mode, "recordOn")) {
            // No change to AI mode
            new_mode = "recordOn";
            wav_writer->set_mode(WAVFileWriter::Mode::continuous);
        }
        // Change this string to 'disabled' ?
        else if (!strcasecmp(req_mode, "recordOff")) {
            // No change to AI mode
            new_mode = "recordOff";
            wav_writer->set_mode(WAVFileWriter::Mode::disabled);
        }
        else if (!strcasecmp(req_mode, "recordOnEvent")) {
            new_mode = "recordOnEvent";     
            new_ai_mode = true;    //<-- Must be on for recording to be triggered
            ai_mode_change = true;
            wav_writer->set_mode(WAVFileWriter::Mode::single);
        }
        else if (!strcasecmp(req_mode, "recordOn_DetectOff")) {
            new_mode = "recordOn_DetectOff";
            new_ai_mode = false;
            ai_mode_change = true;
            wav_writer->set_mode(WAVFileWriter::Mode::continuous);
        }
        else if (!strcasecmp(req_mode, "recordOff_DetectOn")) {
            // Set wav_recording mode to disabled
            // Change to AI mode to ON
            new_mode = "recordOff_DetectOn";
            new_ai_mode = true;
            ai_mode_change = true;
            wav_writer->set_mode(WAVFileWriter::Mode::disabled);
        }
        else if (!strcasecmp(req_mode, "recordOff_DetectOff")) {
            // Set wav_recording mode to disabled
            // Change to AI mode to Off
            new_mode = "recordOff_DetectOff";
            new_ai_mode = true;
            ai_mode_change = false;
            wav_writer->set_mode(WAVFileWriter::Mode::disabled);
        }
        else {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg), "Invalid mode %s", req_mode);
            ESP_LOGE(TAG, "%s", errMsg);
            resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        }
    }

    if (ai_mode_change){
        xQueueSend(rec_ai_evt_queue, &new_ai_mode, (TickType_t)0);
    }

    String& status = resp.getPayload(); 
    status += "{\"recordingState\" : ";
    status += new_mode;
    status += "}";

    ESP_LOGI(TAG, "setRecordMode now %s", new_mode);
    ESP_LOGI(TAG, "wav_writer mode = %s", wav_writer->get_mode_str());
    if (ai_mode_change){
        ESP_LOGI(TAG, "ai mode = %s", new_ai_mode ? "ON" : "OFF");
    }

    resp.setResultSuccess(status);
}

bool initCommands(CmdAdvCallback<MAX_COMMANDS>& cmdCallback) {
    bool success = true;
    success &= cmdCallback.addCmd("setConfig", &cmd_SetConfig, "Write config key as json, e.g. setConfig#cfg={\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("getConfig", &cmd_GetConfig, "Read config as jso. Optional argument 'cfgType' can be set to ('DEFAULT' or 'RUNTIME') to read default config or currently set config. Without 'cfgType' current set config is returned, e.g. getConfig --> return{\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("delConfig", &cmd_DelConfig, "Delete the current config file. Current config is not reset to default until next reboot");
    success &= cmdCallback.addCmd("getStatus", &cmd_GetStatus, "Returns the current status in JSON format");
    success &= cmdCallback.addCmd("setTime", &cmd_SetTime, "Set the current Time. Time format is given as JSON, e.g. setTime#time={\"seconds\":1351824120,\"ms\":42,\"timezone\":6,\"type\":\"G\"}");
    success &= cmdCallback.addCmd("setRecordMode", &cmd_SetRecordMode, "Enable/disable recording. If used without arguments, current mode is toggled(on/off). Otherwise set recording to specified mode, e.g. setRecordingmode#mode=recordOff_DetectOn");
    success &= cmdCallback.addCmd("setLogPersistent", &cmd_SetLogPersistent, "Configure the logging messages to be stored on a rotating log file on SD carde.g. setLogPersitent#cfg={\"logToSdCard\":\"true\",\"filename\":\"/sdcard/log/eloc.log\",\"maxFiles\":6,\"maxFileSize\":1024}");
    if (!success) {
        ESP_LOGE(TAG, "Failed to add all BT commands!");
    }
    return success;
}

} // namespace BtCommands
