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
#include <esp_system.h>

#include "ArduinoJson.h"
#include "WString.h"
#include "CmdParser.hpp"
#include "CmdAdvCallback.hpp"

#include "ESP32Time.h"

#include "SDCardSDIO.h"
#include "CmdResponse.hpp"
#include "ElocConfig.hpp"
#include "ElocStatus.hpp"
#include "ElocSystem.hpp"
#include "Battery.hpp"
#include "BluetoothServer.hpp"
#include "FwUpdateTransfer.hpp"
#include "esp_ota_ops.h"
#include "../../ElocHardware/src/config.h"
#include "../../ElocHardware/src/ElocLora.hpp"
#include "macros.hpp"
#include "logging.hpp"
#include "ffsutils.h"
#include "ScopeGuard.hpp"
#include "esp_timer.h"

#include "../../../include/project_config.h"
#ifdef USE_GPS
    #include "../../gps/src/ElocGPS.hpp"
#endif



/********** BUGME: encapsulate ELOC status and make it threadsafe!!!*/
//BUGME: global status
extern ESP32Time timeObject;
extern SDCardSDIO sd_card;

// Defined in main.cpp - needed for deferred AI start & session creation
extern bool session_folder_created;
extern bool createSessionFolder();

#define ENUM_MACRO(name, v0, v1, v2, v3, v4, v5)\
    enum class name { v0, v1, v2, v3, v4, v5};\
    constexpr const char *name##Strings[] = {  #v0, #v1, #v2, #v3, #v4, #v5}; \
    constexpr const char *toString(name value) {  return name##Strings[static_cast<int>(value)]; }

ENUM_MACRO (RecState, recInvalid, recordOff_detectOff, recordOn_detectOff, recordOn_detectOn, recordOff_detectOn, recordOnEvent);


RecState calcRecordingState() {
    RecState recState = RecState::recInvalid;

    WAVFileWriter::Mode recMode = wav_writer.get_mode();
    ESP_LOGI("COMMANDS", "WavWriterMode = %s(%d), AI = %s", wav_writer.get_mode_str(), wav_writer.get_mode_int(), ai_run_enable ? "ON" : "OFF");
    switch (recMode) {
        case WAVFileWriter::Mode::disabled:
            if (ai_run_enable) {
                recState = RecState::recordOff_detectOn;
            }
            else {
                recState = RecState::recordOff_detectOff;
            }
            break;
        case WAVFileWriter::Mode::continuous:
            if (ai_run_enable) {
                recState = RecState::recordOn_detectOn;
            }
            else {
                recState = RecState::recordOn_detectOff;
            }
            break;
        case WAVFileWriter::Mode::single:
            if (ai_run_enable) {
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

    StaticJsonDocument<2048> doc;
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

    session["recordingTime[h]"]    = round((wav_writer.get_recordingTimeSinceLastStarted_sec() / 60.f / 60.f), 3);
    JsonObject ai = session.createNestedObject("detection");
    ai["state"]                   = ai_run_enable;
    // first set to defaults in case edge impulse is not included in binary
    ai["detectingTime[h]"]        = 0.0;
    ai["detectedEvents"]          = 0;
    ai["aiModel"]                 = "";
#ifdef EDGE_IMPULSE_ENABLED
    ai["detectingTime[h]"]        = round((edgeImpulse.get_totalDetectingTime_secs() / 60.f / 60.f), 3);
    ai["detectedEvents"]          = edgeImpulse.get_detectedEvents();
    ai["aiModel"]                 = EI_CLASSIFIER_PROJECT_NAME;
#endif
    JsonObject device = doc.createNestedObject("device");
    device["firmware"]                   = gFirmwareVersion;
    // Firmware-update capability advertisement: the app gates its update UI on
    // fwUpdateProto and picks the matching release binary via buildVariant.
    // Old apps ignore unknown keys.
    device["fwUpdateProto"]              = 1;
#ifdef EDGE_IMPULSE_ENABLED
    device["buildVariant"]               = "ei";
#else
    device["buildVariant"]               = "no-ai";
#endif
    const esp_partition_t* otaSlot = esp_ota_get_next_update_partition(NULL);
    device["otaSlotSize"]                = otaSlot ? otaSlot->size : 0;
    // Uptime: prefer the true deployment wall-clock — seconds since this deployment first got a
    // valid time (firstBootEpochS), which survives duty-cycle deep sleep so a field unit shows how
    // long it has actually been out. Fall back to time-since-this-boot (esp_timer, resets on wake)
    // when the clock is not set yet or the RTC state is fresh/invalid.
    int64_t uptimeSecs;
    if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC && rtc_duty_cycle.firstBootEpochS > 0 &&
        timeObject.getEpoch() >= rtc_duty_cycle.firstBootEpochS) {
        uptimeSecs = timeObject.getEpoch() - rtc_duty_cycle.firstBootEpochS;
    } else {
        uptimeSecs = static_cast<int64_t>(timeObject.getUpTimeSecs());
    }
    device["Uptime[h]"]                  = round(uptimeSecs / 3600.0, 3);
    device["totalRecordingTime[h]"]      = round((wav_writer.get_recording_time_total_sec() / 60.f / 60.f), 3);
    // Current device clock: a human-readable local-time string for display plus the raw UTC epoch
    // (seconds) so the app can detect drift against its own clock if it wants to.
    device["time"]                       = timeObject.getDateTime(false);
    device["epoch"]                      = static_cast<long>(timeObject.getEpoch());
    // Clock-source markers so the app can label the ELOC Time row: who last set the wall clock, and
    // where the active timezone came from. Both survive deep sleep via rtc_duty_cycle (guard on magic).
    if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
        const char* clkSrc = rtc_duty_cycle.clockSource == CLOCK_SRC_GPS ? "gps"
                           : rtc_duty_cycle.clockSource == CLOCK_SRC_APP ? "app" : "build";
        device["timeSource"]             = clkSrc;
        device["tzSource"]               = rtc_duty_cycle.timezoneOffsetValid ? "app"
                                         : rtc_duty_cycle.gpsTimezoneValid    ? "gps" : "default";
    } else {
        device["timeSource"]             = "build";
        device["tzSource"]               = "default";
    }

    float sdCardSizeGB = 0;
    float sdCardFreeSpaceGB = 0;

    if (sd_card.isMounted()) {
        sdCardSizeGB = sd_card.getCapacityMB()/1024;
        sdCardFreeSpaceGB = sd_card.freeSpaceGB();
    }
    device["SdCardSize[GB]"]             = round(sdCardSizeGB, 2);
    device["SdCardFreeSpace[GB]"]        = round(sdCardFreeSpaceGB, 2);
    device["SdCardFreeSpace[%]"]         = round(sdCardFreeSpaceGB/sdCardSizeGB*100.0, 2);

    // LoRa signal quality section — allows Android app to display signal strength during deployment
    JsonObject lora = doc.createNestedObject("lora");
    ElocLora& loraInst = ElocLora::GetInstance();
    lora["enabled"]                      = getConfig().loraConfig.loraEnable;
    lora["joined"]                       = loraInst.isJoined();
    lora["hasSignalInfo"]                = loraInst.hasSignalInfo();
    if (loraInst.hasSignalInfo()) {
        lora["RSSI[dBm]"]               = round(loraInst.getLastRSSI(), 1);
        lora["SNR[dB]"]                  = round(loraInst.getLastSNR(), 1);
    }

    // GPS section — lets the Android app show whether the on-board GNSS is active and has a fix, and
    // whether the device clock has been corrected from GPS this power session. "present" reflects
    // whether GPS is compiled into this build at all; "powered" whether the module is running this
    // boot (a fresh-clock duty-cycle wake skips powering it — see main.cpp).
    JsonObject gps = doc.createNestedObject("gps");
#ifdef USE_GPS
    ElocGPS& gpsInst = ElocGPS::GetInstance();
    gps["present"]                       = true;
    gps["powered"]                       = gpsInst.isInitialized();
    gps["hasFix"]                        = gpsInst.hasLiveFix();       // live-gated (latch-bug fix)
    gps["satellites"]                    = gpsInst.getSatellites();
    gps["timeSynced"]                    = gpsInst.lastUtcEpoch() != 0;
    gps["fixAge[s]"]                     = gpsInst.hasFix() ? static_cast<long>(gpsInst.getFixAgeMs() / 1000) : -1;
    gps["hdop"]                          = round(gpsInst.getHdop(), 2);  // 0.0 = no live solution
    // lat/lon stay gated on the LATCHED hasFix() (last-KNOWN position) — they may be present while the
    // live "hasFix" above is false (e.g. GPS just powered down, or the fix was lost indoors).
    if (gpsInst.hasFix()) {
        gps["lat"]                       = round(gpsInst.getLat(), 6);
        gps["lon"]                       = round(gpsInst.getLng(), 6);
    }
#else
    gps["present"]                       = false;
#endif

    // Intruder section - lets the app show whether the knock alarm is armed and whether it is
    // firing right now. "armed" is the effective state: detection is a 24/7-only feature, so a
    // device in duty-cycle mode reports armed=false even with intruderCfg.enable set (see
    // ElocSystem::notifyStatusRefresh). "sirenActive" goes false 5 min after the trigger while
    // "alarmActive" stays true - the LoRa alarm uplinks and GPS tracking keep running.
    JsonObject intruder = doc.createNestedObject("intruder");
    const intruderConfig_t& intruderCfg = getConfig().IntruderConfig;
    ElocSystem& elocSys = ElocSystem::GetInstance();
    intruder["enabled"]                  = intruderCfg.detectEnable;
    intruder["armed"]                    = intruderCfg.detectEnable && !getDutyCycleConfig().enable;
    intruder["alarmActive"]              = elocSys.isIntruderDetected();
    intruder["sirenActive"]              = elocSys.isSirenActive();
    intruder["alarmAge[s]"]              = static_cast<long>(elocSys.getIntruderAlarmAgeS());
    intruder["alarmInterval[s]"]         = static_cast<long>(intruderCfg.alarmIntervalS);
    intruder["idleInterval[s]"]          = static_cast<long>(intruderCfg.idleIntervalS);
    // Movement state behind the reporting cadence. Only meaningful while alarmActive is true.
    intruder["moving"]                   = elocSys.isDeviceMoving();

    // The status document grows with every section added to it; a silent overflow would ship a
    // truncated status to the app, so say so in the log instead.
    if (doc.overflowed()) {
        ESP_LOGE(TAG, "Status JSON exceeded its %u byte document - fields are missing!", doc.capacity());
    }
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

    // Snapshot the CPU frequency config so a rejected change can be rolled back, keeping the
    // stored/displayed config consistent with what the hardware actually applies.
    const int  prevCpuMax        = getConfig().cpuMaxFrequencyMHZ;
    const int  prevCpuMin        = getConfig().cpuMinFrequencyMHZ;
    const bool prevCpuLightSleep = getConfig().cpuEnableLightSleep;

    configChangeFlags_t changeFlags = {};
    esp_err_t err = updateConfig(cfg, &changeFlags);

    // Re-apply settings whose subsystems otherwise only read the config at boot
    // (see README-Config-Restart-Semantics.md).
    if (err == ESP_OK) {
        if (changeFlags.cpu) {
            const int newCpuMax = getConfig().cpuMaxFrequencyMHZ;
            const int newCpuMin = getConfig().cpuMinFrequencyMHZ;
            const char* cpuErrMsg = nullptr;

            // Validate each field individually for a clear message. Not cross-checking min<=max
            // here avoids a lock-up where a bad stored value in one field would block fixing the
            // other; pm_configure() at boot handles the combination (and forces min=max while
            // LoRa is enabled).
            if (!isValidCpuMaxFrequency(newCpuMax)) {
                cpuErrMsg = "Invalid CPU max frequency: must be 80, 160 or 240 MHz";
            } else if (!isValidCpuMinFrequency(newCpuMin)) {
                cpuErrMsg = "Invalid CPU min frequency: must be 240, 160, 80, 40, 20 or 10 MHz";
            }

            if (cpuErrMsg != nullptr) {
                ESP_LOGE(TAG, "%s -- reverting CPU config to max=%d min=%d lightSleep=%d",
                    cpuErrMsg, prevCpuMax, prevCpuMin, prevCpuLightSleep);
                setCpuFrequencyConfig(prevCpuMax, prevCpuMin, prevCpuLightSleep);
                resp.setError(ESP_ERR_INVALID_ARG, cpuErrMsg);
                return;
            }
            // Deliberately NOT applied live: switching between 240 MHz (480 MHz PLL) and
            // 80/160 MHz (320 MHz PLL) relocks the BBPLL, which the Bluetooth radio is
            // clocked from — and Bluetooth is by definition active while this command is
            // being handled. The new value is applied by pm_configure() early on the next
            // boot; the app prompts the user to restart the device.
        }
        if (changeFlags.logConfig) {
            StaticJsonDocument<128> logCfg;
            logCfg["logToSdCard"] = getConfig().logConfig.logToSdCard;
            logCfg["filename"]    = getConfig().logConfig.filename;
            logCfg["maxFiles"]    = getConfig().logConfig.maxFiles;
            logCfg["maxFileSize"] = getConfig().logConfig.maxFileSize;
            String logCfgStr;
            serializeJson(logCfg, logCfgStr);
            if (Logging::updateConfig(logCfgStr) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to re-apply logging config");
            }
        }
    }
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

//TODO: use ESP32Time::getSystemTimeMS() instead (depends on the results of #77 time drift evaluation)
int64_t getTimeFromTimeObjectMS() {
    return(static_cast<int64_t>(timeObject.getEpoch())*1000LL+static_cast<int64_t>(timeObject.getMillis()));

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

    /**
     * Format:
     * setTime#time={"seconds":1701665155, "ms" : 42, "timezone" : 7}
     * where:
     *  - seconds: epoch time in seconds
     *  - ms: milliseconds
     *  - timezone: offset in hours (+ve implies ahead of UTC, -ve implies behind UTC)
     */

    long seconds = timeCfg["seconds"];
    long milliseconds = timeCfg["ms"];
    const char* type = timeCfg["type"] | "?";
    long timeZone_offset = timeCfg["timezone"] | TIMEZONE_OFFSET;  // can -ve & +ve
    ESP_LOGI(TAG, "timestamp in from type %s Time Zone: %ld sec: %ld millisec: %ld", type, timeZone_offset, seconds, milliseconds);
    // TODO: why is this needed and what should it be used for?
    // const char* minutesSinceSync = "";//serialIN.substring(11, serialIN.indexOf("___"));

    // Some sanity checks
    //BUGME: Why limit the max. time change since build to 10 years?! should be unlimited
    if ((seconds < timeObject.getBuildTimeSecs()) || (seconds > (timeObject.getBuildTimeSecs() + 60*60*24*365*10))) {
        resp.setError(ESP_ERR_INVALID_ARG, "Invalid epoch time!");
        return;
    }

    if (milliseconds < 0 || milliseconds > 999) {
        resp.setError(ESP_ERR_INVALID_ARG, "Invalid milliseconds!");
        return;
    }

    if (timeZone_offset < -12 || timeZone_offset > 14) {
        resp.setError(ESP_ERR_INVALID_ARG, "Invalid timezone offset!");
        return;
    }

    // Safe to cast as boundary checked
    timeObject.setTime(seconds, static_cast<int>(milliseconds) * 1000);
    timeObject.setTimeZone(static_cast<int32_t>(timeZone_offset));

    // Persist TZ so it survives duty-cycle deep sleep. Without this, the wake
    // path in main.cpp falls back to the compile-time TIMEZONE_OFFSET and CSV
    // timestamps drift to the wrong zone after the first timer-wake.
    rtc_duty_cycle.timezoneOffset      = static_cast<int8_t>(timeZone_offset);
    rtc_duty_cycle.timezoneOffsetValid = true;
    if (rtc_duty_cycle.magic != DUTY_CYCLE_RTC_MAGIC) {
        rtc_duty_cycle.magic = DUTY_CYCLE_RTC_MAGIC;
    }
    rtc_duty_cycle.clockSource = CLOCK_SRC_APP;  // the app just set the wall clock (getStatus timeSource)
    // Anchor the deployment-uptime clock the first time this deployment gets a real time (from the
    // app here, or from GPS in main.cpp). Persists across duty-cycle sleep; only re-armed on a full
    // power loss / magic bump. See the Uptime[h] computation in printStatus().
    if (rtc_duty_cycle.firstBootEpochS == 0) {
        rtc_duty_cycle.firstBootEpochS = timeObject.getEpoch();
    }

    // timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
    //  timestamps coming in from android are always GMT (minus 7 hrs)
    //  if I not add timezone then timeobject is off
    //  so timeobject does not seem to be adding timezone to system time.
    //  timestamps are in gmt+0, so timestamp convrters

    // struct timeval tv_now;
    // gettimeofday(&tv_now, NULL);
    // int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    // time_us = time_us / 1000;

    // ESP_LOGI(TAG, "atol(minutesSinceSync.c_str()) *60L*1000L "+String(atol(minutesSinceSync.c_str()) *60L*1000L));
    // gLastSystemTimeUpdate = getTimeFromTimeObjectMS() - (atol(minutesSinceSync.c_str()) * 60L * 1000L);
    // ESP_LOGI(TAG, "timestamp in from android GMT "+everything    +"  sec: "+seconds + "   millisec: "+milliseconds);
    // ESP_LOGI(TAG, "new timestamp from new sys time (local time) %lld", time_us  ); //this is 7 hours too slow!
    // ESP_LOGI(TAG,"new timestamp from timeobJect (local time) %lld",getTimeFromTimeObjectMS());
    // ESP_LOGI(TAG,"new time set to (local time) %s",timeObject.getDateTime().c_str());

    String& response = resp.getPayload();
    response = "{\"Time[ms]\" : ";
    response += String(getTimeFromTimeObjectMS());
    response += "}";
    resp.setResultSuccess(response);
    return;
}

void cmd_SetRecordMode(CmdParser* cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();

    // Defense in depth: a firmware transfer requires recording/AI off and the
    // command parser is bypassed while it is receiving, but never allow a mode
    // change to slip in around a transfer.
    if (FwUpdateTransfer::GetInstance().isBinaryMode()) {
        resp.setError(ESP_ERR_INVALID_STATE, "firmware transfer in progress");
        return;
    }

    const char* req_mode = cmdParser->getValueFromKey("mode");
    // rec_req_t rec_req;

    RecState new_mode = RecState::recInvalid;
    auto new_ai_mode = true;
    auto ai_mode_change = false;
    // Resolved first, applied only after the SD card check below - a rejected request must
    // leave the recorder exactly as it was.
    auto new_wav_mode = WAVFileWriter::Mode::disabled;

    if (!req_mode) {
        ESP_LOGI(TAG, "setRecordMode requested <none>");

        auto wav_write_mode = wav_writer.get_mode();
        /**
         * If no explicit mode is set, recording mode is toggled, no change to AI mode
         * @warning This is debug feature, shouldn't be generally used
         */
        if (wav_write_mode == WAVFileWriter::Mode::disabled) {
            new_mode = ai_run_enable ? RecState::recordOn_detectOn : RecState::recordOn_detectOff;
            new_wav_mode = WAVFileWriter::Mode::continuous;
        } else {
            new_mode = ai_run_enable ? RecState::recordOff_detectOn : RecState::recordOff_detectOff;
            new_wav_mode = WAVFileWriter::Mode::disabled;
        }
    } else {
        ESP_LOGI(TAG, "setRecordMode requested %s", req_mode);
        ai_mode_change = true;

        // Comparison is case-insensitive, but keep the literals spelled exactly like the
        // RecState enum (and getHelp) so the accepted values match what getStatus reports back.
        if (!strcasecmp(req_mode, "recordOn_detectOff")) {
            new_mode = RecState::recordOn_detectOff;
            new_ai_mode = false;
            new_wav_mode = WAVFileWriter::Mode::continuous;
        } else if (!strcasecmp(req_mode, "recordOn_detectOn")) {
            new_mode = RecState::recordOn_detectOn;
            new_ai_mode = true;
            new_wav_mode = WAVFileWriter::Mode::continuous;
        } else if (!strcasecmp(req_mode, "recordOff_detectOn")) {
            new_mode = RecState::recordOff_detectOn;
            new_ai_mode = true;
            new_wav_mode = WAVFileWriter::Mode::disabled;
        } else if (!strcasecmp(req_mode, "recordOff_detectOff")) {
            new_mode = RecState::recordOff_detectOff;
            new_ai_mode = false;
            new_wav_mode = WAVFileWriter::Mode::disabled;
        } else if (!strcasecmp(req_mode, "recordOnEvent")) {
            new_mode = RecState::recordOnEvent;
            new_ai_mode = true;
            new_wav_mode = WAVFileWriter::Mode::single;
        } else {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg), "Invalid mode %s", req_mode);
            ESP_LOGE(TAG, "%s", errMsg);
            resp.setError(ESP_ERR_INVALID_ARG, errMsg);
            // Must not fall through: the success result at the end would overwrite this error,
            // and the AI block below would enable detection for a mode that was never accepted.
            return;
        }
    }

    // No card, no recording. Without this the device happily reports "recording" while the wav
    // writer silently refuses every file, so the app shows a running session that writes nothing.
    // Detection-only modes stay allowed: they still raise LoRa alerts without a card.
    if ((new_wav_mode != WAVFileWriter::Mode::disabled) && !sd_card.isMounted()) {
        const char* errMsg = "No SD card - cannot start recording. Insert a card and try again.";
        ESP_LOGE(TAG, "%s", errMsg);
        resp.setError(ESP_ERR_NOT_FOUND, errMsg);
        return;
    }

    wav_writer.set_mode(new_wav_mode);

    if (ai_mode_change) {
        // Set ai_run_enable IMMEDIATELY so that calcRecordingState() and getStatus
        // return the correct state right away, without waiting for the main loop
        // to dequeue from rec_ai_evt_queue.
        ai_run_enable = new_ai_mode;

        if (new_ai_mode) {
            // ENABLING AI: Create session folder now so getStatus returns correct session ID.
            // The actual AI thread start is DEFERRED to allow BT to serve follow-up
            // commands (getStatus/getConfig) before the AI thread consumes CPU/memory.
            if (!session_folder_created) {
                ESP_LOGI(TAG, "Creating session folder early for correct session ID in getStatus");
                createSessionFolder();
            }
            g_ai_deferred_start_time = esp_timer_get_time() + AI_DEFERRED_START_DELAY_US;
            g_ai_start_pending = true;
            ESP_LOGI(TAG, "AI start deferred by %lld ms to allow BT commands to complete",
                     AI_DEFERRED_START_DELAY_US / 1000LL);
        } else {
            // DISABLING AI: Cancel any pending deferred start and stop immediately
            g_ai_start_pending = false;
            xQueueSend(rec_ai_evt_queue, &new_ai_mode, (TickType_t)0);
        }

        // Duty cycle applies to the AI-only patrol mode (recordOff_detectOn) and to both
        // record-ON modes (recordOn_detectOn, recordOn_detectOff). recordOnEvent and the
        // off/toggle modes stay non-duty-cycled. Persist the wav mode + AI state in RTC so
        // they can be restored after each timer wake (see handleWakeUpCause / boot path).
        bool dutyCycleMode = (new_mode == RecState::recordOff_detectOn ||
                              new_mode == RecState::recordOn_detectOn  ||
                              new_mode == RecState::recordOn_detectOff);
        if (dutyCycleMode && getDutyCycleConfig().enable) {
            rtc_duty_cycle.recordMode = static_cast<uint8_t>(wav_writer.get_mode());
            rtc_duty_cycle.aiEnabled  = new_ai_mode;
            gDutyCycleActivationTimeUS = esp_timer_get_time();
            gSleepCycleState = SLEEP_CYCLE_INFERENCE_ACTIVE;
            ESP_LOGI(TAG, "Duty cycle activated for mode %s", toString(new_mode));
        } else {
            gSleepCycleState = SLEEP_CYCLE_DISABLED;
        }
    }

    StaticJsonDocument<512> doc;
    JsonObject recordingState = doc.createNestedObject("recordingState");

    addEnum(recordingState, new_mode);

    String& status = resp.getPayload();
    if (serializeJsonPretty(doc, status) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
    }

    ESP_LOGI(TAG, "setRecordMode now %s(%d)", toString(new_mode), static_cast<int>(new_mode));
    ESP_LOGI(TAG, "wav_writer mode = %s", wav_writer.get_mode_str());
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

static void rebootTimerCallback(void* /*arg*/) {
    ESP_LOGW(TAG, "Restarting device now (scheduled restart)");
    esp_restart();
}

/// Restart shortly after returning so the command response gets flushed over Bluetooth first.
static esp_err_t scheduleRestart(uint64_t delayUs) {
    static esp_timer_handle_t rebootTimer = nullptr;
    if (rebootTimer == nullptr) {
        const esp_timer_create_args_t timerArgs = {
            .callback = &rebootTimerCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "reboot"
        };
        esp_err_t err = esp_timer_create(&timerArgs, &rebootTimer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create reboot timer with %s", esp_err_to_name(err));
            return err;
        }
    }
    esp_err_t err = esp_timer_start_once(rebootTimer, delayUs);
    if (err == ESP_ERR_INVALID_STATE) {  // restart already pending
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reboot timer with %s", esp_err_to_name(err));
    }
    return err;
}

void cmd_Reboot(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    if (esp_err_t err = scheduleRestart(1000 * 1000)) {
        resp.setError(err, "Failed to schedule reboot");
        return;
    }
    resp.setResultSuccess("rebooting");
}

/****************************************************************************************
 * Firmware update over Bluetooth (see README-FirmwareUpdate-From-App-Plan.md, Phase 1)
 ****************************************************************************************/

void cmd_SetFwUpdateBegin(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* meta = cmdParser->getValueFromKey("meta");
    if (!meta) {
        resp.setError(ESP_ERR_INVALID_ARG, "Missing key 'meta'");
        return;
    }
    String errMsg;
    uint32_t resumeOffset = 0;
    uint16_t chunkSize = 0;
    esp_err_t err = FwUpdateTransfer::GetInstance().begin(meta, errMsg, resumeOffset, chunkSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setFwUpdateBegin refused: %s", errMsg.c_str());
        resp.setError(err, errMsg.c_str());
        return;
    }
    // begin() skips binary mode when the staged file is already complete
    // (resume after the final ack was lost) — only hook the raw data sink
    // when there is actually something to receive. Hooking happens before
    // the response goes out: the first binary frame can never race past the
    // response and hit the command parser.
    bool receiving = FwUpdateTransfer::GetInstance().isBinaryMode();
    if (receiving) {
        btEnterFwBinaryMode();
    }

    StaticJsonDocument<128> doc;
    doc["resumeOffset"] = resumeOffset;
    doc["chunkSize"] = chunkSize;
    // "staged" tells the app to skip streaming and apply directly
    doc["state"] = receiving ? "receiving" : "staged";
    String& payload = resp.getPayload();
    serializeJson(doc, payload);
    resp.setResultSuccess(payload);
}

void cmd_GetFwUpdateStatus(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    String& payload = resp.getPayload();
    FwUpdateTransfer::GetInstance().statusJson(payload);
    resp.setResultSuccess(payload);
}

void cmd_SetFwUpdateAbort(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    const char* discardStr = cmdParser->getValueFromKey("discard");
    bool discard = (discardStr != NULL) && !strcasecmp(discardStr, "true");
    String msg;
    FwUpdateTransfer::GetInstance().abortTransfer(discard, msg);
    String& payload = resp.getPayload();
    payload = "\"";
    payload += msg;
    payload += "\"";
    resp.setResultSuccess(payload);
}

void cmd_SetFwUpdateApply(CmdParser *cmdParser) {
    CmdResponse& resp = CmdResponse::getInstance();
    String errMsg;
    esp_err_t err = FwUpdateTransfer::GetInstance().apply(errMsg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "setFwUpdateApply refused: %s", errMsg.c_str());
        resp.setError(err, errMsg.c_str());
        return;
    }
    if ((err = scheduleRestart(1500 * 1000))) {
        resp.setError(err, "update staged but restart failed - reboot manually");
        return;
    }
    resp.setResultSuccess("\"applying - device will restart and flash the update\"");
}

bool initCommands(CmdAdvCallback<MAX_COMMANDS>& cmdCallback) {
    bool success = true;
    success &= cmdCallback.addCmd("setConfig", &cmd_SetConfig, "Write config key as json, e.g. setConfig#cfg={\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("reboot", &cmd_Reboot, "Restart the ELOC device. The response is sent first, the device restarts ~1 second later, e.g. reboot");
    success &= cmdCallback.addCmd("getConfig", &cmd_GetConfig, "Read config as jso. Optional argument 'cfgType' can be set to ('DEFAULT' or 'RUNTIME') to read default config or currently set config. Without 'cfgType' current set config is returned, e.g. getConfig --> return{\"device\":{\"location\":\"not_set\"}}");
    success &= cmdCallback.addCmd("delConfig", &cmd_DelConfig, "Delete the current config file. Current config is not reset to default until next reboot");
    success &= cmdCallback.addCmd("getStatus", &cmd_GetStatus, "Returns the current status in JSON format");
    success &= cmdCallback.addCmd("setTime", &cmd_SetTime, "Set the current Time. Time format is given as JSON, e.g. setTime#time={\"seconds\":1351824120,\"ms\":42,\"timezone\":6,\"type\":\"G\"}");
    success &= cmdCallback.addCmd("setRecordMode", &cmd_SetRecordMode, "Enable/disable recording. If used without arguments, current mode is toggled(on/off). Otherwise set recording to specified mode. Accepted modes (matched case-insensitively, reported back by getStatus in this exact spelling): recordOff_detectOff, recordOn_detectOff, recordOn_detectOn, recordOff_detectOn, recordOnEvent, e.g. setRecordMode#mode=recordOff_detectOn");
    success &= cmdCallback.addCmd("setLogPersistent", &cmd_SetLogPersistent, "Configure the logging messages to be stored on a rotating log file on SD carde.g. setLogPersitent#cfg={\"logToSdCard\":\"true\",\"filename\":\"/sdcard/log/eloc.log\",\"maxFiles\":6,\"maxFileSize\":1024}");
    success &= cmdCallback.addCmd("setBattery", &cmd_SetBattery, "Set battery calibration values. Mode otions: \"clear\", \"add\", cal in the format {\"<esp meas voltage>\" : <real voltage>} e.g. setBattery#mode=add#cal={\"3.0\":3.1}");
    success &= cmdCallback.addCmd("getBattery", &cmd_GetBattery, "read the battery calibration or the raw (uncalibrated voltage). Mode options: \"raw\", \"cal\"");
success &= cmdCallback.addCmd("getSdSpeedTest", &cmd_GetSdCardSpeedTest, "write and read a blocks (1k - 64k) of data to/from the sd card and check the speed. Additinoal option \"size\", size of overall file (default 512 kByte), -1 means file size = block size, e.g. getSdSpeedTest#size=524288");
    success &= cmdCallback.addCmd("setFwUpdateBegin", &cmd_SetFwUpdateBegin, "Start a firmware transfer over BT. meta JSON: size, sha256, version, variant, chunkSize (default 4096, max 8192). Returns {resumeOffset, chunkSize} and switches the link to binary frame mode, e.g. setFwUpdateBegin#meta={\"size\":123456,\"sha256\":\"<64 hex>\",\"version\":\"1.23\",\"variant\":\"ei\",\"chunkSize\":4096}");
    success &= cmdCallback.addCmd("getFwUpdateStatus", &cmd_GetFwUpdateStatus, "Status of the staged firmware transfer: {state: idle/receiving/staged/error, bytesReceived, expectedSize, sha256}. Drives resume and app UI");
    success &= cmdCallback.addCmd("setFwUpdateAbort", &cmd_SetFwUpdateAbort, "Abort the current firmware transfer. Optional discard=true also deletes the partial file (otherwise it is kept for resume), e.g. setFwUpdateAbort#discard=true");
    success &= cmdCallback.addCmd("setFwUpdateApply", &cmd_SetFwUpdateApply, "Verify the staged firmware (SHA-256 + image header) and restart to flash it via the SD updater");

    if (!success) {
        ESP_LOGE(TAG, "Failed to add all BT commands!");
    }
    return success;
}

}  // namespace BtCommands
