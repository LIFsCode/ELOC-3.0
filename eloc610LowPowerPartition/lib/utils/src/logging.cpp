/*
 * Created on Fri Aug 11 2023
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
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"

#include "ffsutils.h"
#include "RotateFile.hpp"

#include "ArduinoJson.h"

#include "ESP32Time.h"


namespace Logging {

static const char *TAG = "Logging";

static const uint32_t LOG_NUM_FILES = 10;
static const uint32_t LOG_FILE_SIZE = 5*1024;

static const char* LOG_NAME = "/sdcard/log/eloc.log";

static RotateFile logFile(LOG_NAME, LOG_NUM_FILES, LOG_FILE_SIZE);
static bool log_to_scard_enabled = false;
// This function will be called by the ESP log library every time ESP_LOG needs to be performed.
//      @important Do NOT use the ESP_LOG* macro's in this function ELSE recursive loop and stack overflow! So use printf() instead for debug messages.
static bool static_fatal_error = false;
static int _log_vprintf(const char *fmt, va_list args) {
    if (static_fatal_error == false) {
        if (!logFile.vprintf(fmt, args)) {
            printf("%s() ABORT. failed vfprintf() -> disable future vfprintf \n", __FUNCTION__);
            // MARK FATAL
            static_fatal_error = true;
        }
    }
    // #3 ALWAYS Write to stdout!
    return vprintf(fmt, args);
}

esp_err_t esp_log_to_scard(bool enable) {

    if (enable == log_to_scard_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (enable) {
        ESP_LOGI(TAG, "***Redirecting log output to SD card log file (also keep sending logs to UART0)");

        if (!logFile.open()) {
            ESP_LOGE(TAG, "Failed to open %s for logging", LOG_NAME);
            return ESP_ERR_NOT_FOUND;
        }
        esp_log_set_vprintf(&_log_vprintf);
        ESP_LOGI(TAG, "%s: start logging to sd card", ESP32Time().getTimeDate(false).c_str());
    }
    else {
        ESP_LOGI(TAG, "  ***Redirecting log output BACK to only UART0 (not to the SPIFFS log file anymore)");
        logFile.close();
        // reset permanent error if disabled, allows to be rechecked if reenabled
        static_fatal_error = false;
        esp_log_set_vprintf(&vprintf);
        ESP_LOGI(TAG, "this should not be on sd card");
    }
    log_to_scard_enabled = enable;
    return ESP_OK;

}

esp_err_t init(bool logToSdCard, const String& filename, uint32_t maxFiles, uint32_t maxFileSize) {
    if (!logFile.setFilename(filename.c_str())) {
        return ESP_FAIL;
    }
    if (!logFile.setMaxFiles(maxFiles)) {
        return ESP_FAIL;
    }
    if (!logFile.setMaxFileSize(maxFileSize)) {
        return ESP_FAIL;
    }
    if (!logFile.setMaxFileSize(maxFileSize)) {
        return ESP_FAIL;
    }
    return esp_log_to_scard(logToSdCard);
}

static const uint32_t JSON_DOC_SIZE = 128;

esp_err_t printLogConfig(String& buf) {

    StaticJsonDocument<JSON_DOC_SIZE> doc;
    doc["logToSdCard"]    = log_to_scard_enabled;
    doc["filename"]       = logFile.getFilename();
    doc["maxFiles"]       = logFile.getMaxFiles();
    doc["maxFileSize"]    = logFile.getMaxFileSize();
    if (serializeJsonPretty(doc, buf) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t updateConfig(const String& buf) {
    bool enable;
    bool changeState=false;
    { // limit scope of StaticJsonDocument on stack to reduce memory usage
        static StaticJsonDocument<JSON_DOC_SIZE> newCfg;
        newCfg.clear();
        DeserializationError error = deserializeJson(newCfg, buf);
        if (error) {
            ESP_LOGE(TAG, "Parsing config failed with %s!", error.c_str());
            return ESP_ERR_INVALID_ARG;
        }
        if (newCfg.containsKey("filename")) {
            if (!logFile.setFilename(newCfg["filename"])) {
                return ESP_FAIL;
            }
        }
        if (newCfg.containsKey("maxFiles")) {
            if (!logFile.setMaxFiles(newCfg["maxFiles"])) {
                return ESP_FAIL;
            }
        }
        if (newCfg.containsKey("maxFileSize")) {
            if (!logFile.setMaxFileSize(newCfg["maxFileSize"])) {
                return ESP_FAIL;
            }
        }
        if (newCfg.containsKey("logToSdCard")) {
            enable = newCfg["logToSdCard"];
            changeState = true;
        }
    }
    if (changeState) {
        // disabling log to sdcard may use quiet a lot of stack memory due to file syncs & renames
        // make sure this is not within the same scope as StaticJsonDocument
        return esp_log_to_scard(enable);
    }
    return ESP_OK;
}

} // namespace Logging