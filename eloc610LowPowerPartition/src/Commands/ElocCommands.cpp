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
#include "utils/ffsutils.h"
#include "utils/ScopeGuard.hpp"
#include "ElocProcessFactory.hpp"




/********** BUGME: encapsulate ELOC status and make it threadsafe!!!*/
//BUGME: global status
extern ESP32Time timeObject;
extern SDCardSDIO sd_card;


RecState calcRecordingState() {
    RecState recState = RecState::recInvalid;

    WAVFileWriter::Mode recMode = elocProcessing.getWavWriter().get_mode();
    EdgeImpulse::Status aiMode = elocProcessing.getEdgeImpulse().get_status();
    ESP_LOGI("COMMANDS", "WavWriterMode = %s(%d), AI = %s", 
            elocProcessing.getWavWriter().get_mode_str(), elocProcessing.getWavWriter().get_mode_int(), 
            aiMode == EdgeImpulse::Status::running ? "ON" : "OFF");
    switch (recMode) {
        case WAVFileWriter::Mode::disabled:
            if (aiMode == EdgeImpulse::Status::running) {
                recState = RecState::recordOff_detectOn;
            }
            else {
                recState = RecState::recordOff_detectOff;
            }
            break;
        case WAVFileWriter::Mode::continuous:
            if (aiMode == EdgeImpulse::Status::running) {
                recState = RecState::recordOn_detectOn;
            }
            else {
                recState = RecState::recordOn_detectOff;
            }
            break;
        case WAVFileWriter::Mode::single:
            if (aiMode == EdgeImpulse::Status::running) {
                recState = RecState::recordOnEvent;
            }
            else {
                recState = RecState::recInvalid;
            }
            break;
    }
    return recState;
}

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

    RecState recState = calcRecordingState();
    addEnum(recordingState, recState);


    session["recordingTime[h]"]    = (float)elocProcessing.getWavWriter().get_recording_time_total_sec() / 1000 / 1000 / 60 / 60;
    JsonObject ai = session.createNestedObject("detection");
    ai["state"]                   = elocProcessing.getEdgeImpulse().get_status() == EdgeImpulse::Status::running;
    // first set to defaults in case edge impuse is not included in binary
    ai["detectingTime[h]"]        = 0.0;
    ai["detectedEvents"]          = 0;
    ai["aiModel"]                 = "";
#ifdef EDGE_IMPULSE_ENABLED
    ai["detectingTime[h]"]        = elocProcessing.getEdgeImpulse().get_totalDetectingTime_hr();
    ai["detectedEvents"]          = elocProcessing.getEdgeImpulse().get_detectedEvents();
    ai["aiModel"]                 = EI_CLASSIFIER_PROJECT_NAME;
#endif
    JsonObject device = doc.createNestedObject("device");
    device["firmware"]                   = gFirmwareVersion;
    device["Uptime[h]"]                  = (float)esp_timer_get_time() / 1000 / 1000 / 60 / 60;
    device["totalRecordingTime[h]"]      = ((float)elocProcessing.getWavWriter().get_recording_time_total_sec() +
                                            (float)elocProcessing.getWavWriter().get_recording_time_file_sec()) / 1000 / 1000 / 60 / 60;

    float sdCardSizeGB = 0;
    float sdCardFreeSpaceGB = 0;

    if (sd_card.isMounted()) {
        sdCardSizeGB = sd_card.getCapacityMB()/1024;
        sdCardFreeSpaceGB = sd_card.freeSpaceGB();

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
    // first update the battery to have actual reading
    Battery::GetInstance().updateVoltage(true);

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

    RecState new_mode = RecState::recInvalid;
    auto new_ai_mode = true;
    auto ai_mode_change = false;

    if (!req_mode) {
        ESP_LOGI(TAG, "setRecordMode requested <none>");
        if (elocProcessing.getState() != RecState::recordOff_detectOff) {
            new_mode = RecState::recordOn_detectOff;
        }
        else {
            new_mode = RecState::recordOff_detectOff;
        }

    } else {
        ESP_LOGI(TAG, "setRecordMode requested %s", req_mode);
        ai_mode_change = true;

        if (!strcasecmp(req_mode, "recordOn_DetectOFF")) {
            new_mode = RecState::recordOn_detectOff;
        } else if (!strcasecmp(req_mode, "recordOn_DetectOn")) {
            new_mode = RecState::recordOn_detectOn;
        } else if (!strcasecmp(req_mode, "recordOff_DetectOn")) {
            new_mode = RecState::recordOff_detectOn;
        } else if (!strcasecmp(req_mode, "recordOff_DetectOff")) {
            new_mode = RecState::recordOff_detectOff;
        } else if (!strcasecmp(req_mode, "recordOnEvent")) {
            new_mode = RecState::recordOnEvent;
        } else {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg), "Invalid mode %s", req_mode);
            ESP_LOGE(TAG, "%s", errMsg);
            resp.setError(ESP_ERR_INVALID_ARG, errMsg);
            return;
        }
    }
    elocProcessing.queueNewMode(new_mode);


    StaticJsonDocument<512> doc;
    JsonObject recordingState = doc.createNestedObject("recordingState");

    addEnum(recordingState, new_mode);

    String& status = resp.getPayload();
    if (serializeJsonPretty(doc, status) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
    }

    ESP_LOGI(TAG, "setRecordMode now %s(%d)", toString(new_mode), static_cast<int>(new_mode));
    ESP_LOGI(TAG, "wav_writer mode = %s", elocProcessing.getWavWriter().get_mode_str());
    if (ai_mode_change) {
        ESP_LOGI(TAG, "ai mode = %s", new_ai_mode ? "ON" : "OFF");
    }

    resp.setResultSuccess(status);
}

void cmd_SetBattery(CmdParser *cmdParser) {

    CmdResponse& resp = CmdResponse::getInstance();
    const char* mode = cmdParser->getValueFromKey("mode");
    if (!mode) {
        const char* errMsg = "Missing key 'mode'";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        return;
    }
    if (!strcasecmp(mode, "clear")) {
        //TODO clear battery calibration
        esp_err_t err = Battery::GetInstance().clearCal();
        resp.setResult(err);
        return;
    }
    else if (!strcasecmp(mode, "add")) {
        const char* cal = cmdParser->getValueFromKey("cal");
        if (!cal) {
            const char* errMsg = "Missing key 'cal'";
            ESP_LOGE(TAG, "%s", errMsg);
            resp.setError(ESP_ERR_INVALID_ARG, errMsg);
            return;
        }
        ESP_LOGI(TAG, "updating calibration with %s", cal);
        esp_err_t err = Battery::GetInstance().updateCal(cal);
        if (err != ESP_OK) {
            resp.setError(err, "failed to update calibration data");
        }
        else {
            String& payload = resp.getPayload(); // write directly to output buffer to avoid reallocation
            err = Battery::GetInstance().printCal(payload);
            resp.setResult(err);
        }
        return;
    }
    char errMsg[128];
    snprintf(errMsg, sizeof(errMsg), "Invalid mode '%s'", mode);
    ESP_LOGE(TAG, "%s", errMsg);
    resp.setError(ESP_ERR_INVALID_ARG, errMsg);
    return;
}
void cmd_GetBattery(CmdParser *cmdParser) {

    CmdResponse& resp = CmdResponse::getInstance();
    const char* mode = cmdParser->getValueFromKey("mode");
    if (!mode) {
        const char* errMsg = "Missing key 'mode'";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
        return;
    }
    String& payload = resp.getPayload(); // write directly to output buffer to avoid reallocation
    if (!strcasecmp(mode, "cal")) {
        //TODO clear battery calibration
        esp_err_t err = Battery::GetInstance().printCal(payload);
        resp.setResult(err);
    }
    else if (!strcasecmp(mode, "raw")) {
        //TODO: read raw voltage (uncalibrated here)
        float voltage = 0;
        esp_err_t err = Battery::GetInstance().getRawVoltage(voltage);
        payload = String(voltage);
        resp.setResult(err);
    }
    else {
        char errMsg[128];
        snprintf(errMsg, sizeof(errMsg), "Invalid mode '%s'", mode);
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_INVALID_ARG, errMsg);
    }
    return;
}

void cmd_GetSdCardSpeedTest(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* size = cmdParser->getValueFromKey("size");
    int TEST_FILE_SIZE = (512 * 1024); // 512 kByte
    if (size) {
        TEST_FILE_SIZE = atoi(size);
    }
    uint8_t *buf = (uint8_t*) malloc(64 * 1024);   /* malloc will not reset all bytes to zero, so it is a random data */
    if (!buf) {
        const char* errMsg = "Failed to allocate buffer!";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_NO_MEM, errMsg);
      return;
    }
    if (TEST_FILE_SIZE < 0) {
      ESP_LOGI("SD_TEST", "Total file size same as block size");
    }
    else {
      ESP_LOGI("SD_TEST", "Total file size %d",  TEST_FILE_SIZE);
    }

    // set current task to highest priority to minimize sideeffects on performance measurement
    vTaskPrioritySet( NULL, configMAX_PRIORITIES - 1);
    static const int NUM_TESTS = 7;
    ffsutil::sdTestSpeed_t results[NUM_TESTS];
    results[0] = ffsutil::TestSDFile("/sdcard/test_1k.bin", buf, 1024, TEST_FILE_SIZE);
    results[1] = ffsutil::TestSDFile("/sdcard/test_2k.bin", buf, 2 * 1024, TEST_FILE_SIZE);
    results[2] = ffsutil::TestSDFile("/sdcard/test_4k.bin", buf, 4 * 1024, TEST_FILE_SIZE);
    results[3] = ffsutil::TestSDFile("/sdcard/test_8k.bin", buf, 8 * 1024, TEST_FILE_SIZE);
    results[4] = ffsutil::TestSDFile("/sdcard/test_16k.bin", buf, 16 * 1024, TEST_FILE_SIZE);
    results[5] = ffsutil::TestSDFile("/sdcard/test_32k.bin", buf, 32 * 1024, TEST_FILE_SIZE);
    results[6] = ffsutil::TestSDFile("/sdcard/test_64k.bin", buf, 64 * 1024, TEST_FILE_SIZE);

    // reset the task priority
    vTaskPrioritySet( NULL, TASK_PRIO_CMD);
    free(buf);
    ESP_LOGI("SD_TEST", "Done.");

    StaticJsonDocument<1024> doc;
    JsonArray resultList = doc.createNestedArray("results");
    for (int i = 0; i < NUM_TESTS; i++ ) {
        JsonObject res = resultList.createNestedObject();
        res["blkSize"]     = results[i].blockSize;
        res["write[kB/s]"] = results[i].writeSpeedKBs;
        res["read[kB/s]"]  = results[i].readSpeedKBs;
    }

    String& payload = resp.getPayload();
    if (serializeJsonPretty(doc, payload) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
    }
    resp.setResultSuccess(payload);
    return;
}

bool initCommands(CmdAdvCallback<MAX_COMMANDS>& cmdCallback) {
    bool success = true;
    success &= cmdCallback.addCmd("setConfig", &cmd_SetConfig, "Write config key as json, e.g. setConfig#cfg={\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("getConfig", &cmd_GetConfig, "Read config as jso. Optional argument 'cfgType' can be set to ('DEFAULT' or 'RUNTIME') to read default config or currently set config. Without 'cfgType' current set config is returned, e.g. getConfig --> return{\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("delConfig", &cmd_DelConfig, "Delete the current config file. Current config is not reset to default until next reboot");
    success &= cmdCallback.addCmd("getStatus", &cmd_GetStatus, "Returns the current status in JSON format");
    success &= cmdCallback.addCmd("setTime", &cmd_SetTime, "Set the current Time. Time format is given as JSON, e.g. setTime#time={\"seconds\":1351824120,\"ms\":42,\"timezone\":6,\"type\":\"G\"}");
    success &= cmdCallback.addCmd("setRecordMode", &cmd_SetRecordMode, "Enable/disable recording. If used without arguments, current mode is toggled(on/off). Otherwise set recording to specified mode, e.g. setRecordMode#mode=recordOff_DetectOn");
    success &= cmdCallback.addCmd("setLogPersistent", &cmd_SetLogPersistent, "Configure the logging messages to be stored on a rotating log file on SD carde.g. setLogPersitent#cfg={\"logToSdCard\":\"true\",\"filename\":\"/sdcard/log/eloc.log\",\"maxFiles\":6,\"maxFileSize\":1024}");
    success &= cmdCallback.addCmd("setBattery", &cmd_SetBattery, "Set battery calibration values. Mode otions: \"clear\", \"add\", cal in the format {\"<esp meas voltage>\" : <real voltage>} e.g. setBattery#mode=add#cal={\"3.0\":3.1}");
    success &= cmdCallback.addCmd("getBattery", &cmd_GetBattery, "read the battery calibration or the raw (uncalibrated voltage). Mode options: \"raw\", \"cal\"");
success &= cmdCallback.addCmd("getSdSpeedTest", &cmd_GetSdCardSpeedTest, "write and read a blocks (1k - 64k) of data to/from the sd card and check the speed. Additinoal option \"size\", size of overall file (default 512 kByte), -1 means file size = block size, e.g. getSdSpeedTest#size=524288");

    if (!success) {
        ESP_LOGE(TAG, "Failed to add all BT commands!");
    }
    return success;
}

}  // namespace BtCommands
