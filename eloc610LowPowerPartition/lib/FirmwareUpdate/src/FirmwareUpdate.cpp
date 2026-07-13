/*
 * Created on Fri May 05 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 * Copyright (c) 2023 tbgilson
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
#ifndef GENERIC_HW

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

#include "config.h"

#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "mbedtls/sha256.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "ArduinoJson.h"

#include "ffsutils.h"
#include "SDCardSDIO.h"
#include "ElocSystem.hpp"
#include "ElocStatus.hpp"
#include "FirmwareUpdate.hpp"

static const char *TAG = "UPDATE";

extern SDCardSDIO sd_card;

namespace fwupd {

esp_err_t sha256File(const char* path, char hexOut[65]) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    const size_t BUF_SIZE = 4096;
    uint8_t* buf = static_cast<uint8_t*>(malloc(BUF_SIZE));
    if (buf == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0 /* SHA-256, not SHA-224 */);
    size_t n;
    while ((n = fread(buf, 1, BUF_SIZE, f)) > 0) {
        mbedtls_sha256_update_ret(&ctx, buf, n);
    }
    bool readError = (ferror(f) != 0);
    unsigned char digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    free(buf);
    fclose(f);
    if (readError) {
        return ESP_FAIL;
    }
    for (int i = 0; i < 32; i++) {
        snprintf(&hexOut[i * 2], 3, "%02x", digest[i]);
    }
    hexOut[64] = '\0';
    return ESP_OK;
}

esp_err_t validateImageFile(const char* path, String& newVersion, String& errMsg) {
    long fileSize = ffsutil::getFileSize(path);
    if (fileSize <= 0) {
        errMsg = "update file missing or empty";
        return ESP_ERR_NOT_FOUND;
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        errMsg = "no inactive OTA partition found";
        return ESP_ERR_NOT_FOUND;
    }
    if (static_cast<uint32_t>(fileSize) > target->size) {
        errMsg = "update file larger than OTA slot (";
        errMsg += String(fileSize);
        errMsg += " > ";
        errMsg += String(target->size);
        errMsg += " bytes)";
        return ESP_ERR_INVALID_SIZE;
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        errMsg = "failed to open update file";
        return ESP_ERR_NOT_FOUND;
    }
    /* The app descriptor sits at a fixed offset in every ESP32 app image:
     * esp_image_header_t + esp_image_segment_header_t + esp_app_desc_t. */
    struct {
        esp_image_header_t image;
        esp_image_segment_header_t segment;
        esp_app_desc_t app;
    } hdr;
    size_t got = fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (got != sizeof(hdr)) {
        errMsg = "update file too small for image header";
        return ESP_ERR_INVALID_SIZE;
    }
    if (hdr.image.magic != ESP_IMAGE_HEADER_MAGIC) {
        errMsg = "not an ESP32 app image (bad magic)";
        return ESP_ERR_INVALID_ARG;
    }
    if (hdr.app.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        errMsg = "app descriptor missing (bad magic word)";
        return ESP_ERR_INVALID_ARG;
    }
    // project_version is NUL-terminated by the build system, but don't trust it
    char version[sizeof(hdr.app.version) + 1] = {};
    memcpy(version, hdr.app.version, sizeof(hdr.app.version));
    newVersion = version;

    // Guard against flashing a structurally valid image of a *different*
    // project: rollback only reverts images that crash, so a foreign image
    // that runs stably would leave the device unreachable over BT. Compare
    // against the running image's own descriptor — no hardcoded name, except
    // for "ELOC", allowlisted so a future release can rename the CMake
    // project once the whole fleet has moved past this version.
    char newProject[sizeof(hdr.app.project_name) + 1] = {};
    memcpy(newProject, hdr.app.project_name, sizeof(hdr.app.project_name));
    const esp_app_desc_t* running = esp_ota_get_app_description();
    if (running != NULL && strcmp(newProject, running->project_name) != 0) {
        if (strcmp(newProject, "ELOC") == 0) {
            ESP_LOGI(TAG, "%s: project '%s' differs from running '%s', but is a known future "
                          "project name, accepting", path, newProject, running->project_name);
        } else {
            errMsg = "image belongs to project '";
            errMsg += newProject;
            errMsg += "', this device runs '";
            errMsg += running->project_name;
            errMsg += "'";
            return ESP_ERR_INVALID_ARG;
        }
    }

    ESP_LOGI(TAG, "%s: valid app image, version '%s', project '%s', size %ld",
             path, version, newProject, fileSize);
    return ESP_OK;
}

/// Persist the outcome of an update attempt so the app can read it after reconnect.
static void writeResultFile(const char* fromVersion, const char* toVersion, const char* outcome) {
    FILE* f = fopen(RESULT_FILE, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "Could not write %s", RESULT_FILE);
        return;
    }
    StaticJsonDocument<384> doc;
    doc["fromVersion"] = fromVersion;
    doc["toVersion"] = toVersion;
    doc["outcome"] = outcome;
    doc["timestamp"] = static_cast<long>(time(NULL));
    String out;
    serializeJson(doc, out);
    fputs(out.c_str(), f);
    fclose(f);
}

}  // namespace fwupd

using namespace fwupd;

#define FLASH_COPY_BUF_SIZE 4096

/// Stream the staged file into the inactive OTA slot and activate it.
static esp_err_t flashStagedImage(const esp_partition_t* update_partition, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    ESP_LOGI(TAG, "Flashing %ld bytes from %s to partition '%s'", size, filename, update_partition->label);

    uint8_t* buf = static_cast<uint8_t*>(malloc(FLASH_COPY_BUF_SIZE));
    if (buf == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, size, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        free(buf);
        fclose(file);
        return err;
    }

    long remaining = size;
    while (remaining > 0) {
        ElocSystem::GetInstance().notifyFwUpdate();
        size_t chunk = (remaining < FLASH_COPY_BUF_SIZE) ? remaining : FLASH_COPY_BUF_SIZE;
        if (fread(buf, 1, chunk, file) != chunk) {
            ESP_LOGE(TAG, "Read error on %s with %ld bytes remaining", filename, remaining);
            err = ESP_FAIL;
            break;
        }
        err = esp_ota_write(update_handle, buf, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed (%s) with %ld bytes remaining", esp_err_to_name(err), remaining);
            break;
        }
        remaining -= chunk;
    }
    free(buf);
    fclose(file);

    if (err != ESP_OK) {
        esp_ota_abort(update_handle);
        return err;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        }
        ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        return err;
    }
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "update success.");
    return ESP_OK;
}

bool updateFirmware() {

    if (!ffsutil::fileExist(STAGED_BIN)) {
        ESP_LOGI(TAG, "No update file found at %s", STAGED_BIN);
        return false;
    }

    const char* fromVersion = gFirmwareVersion.c_str();

    // 1) Image sanity: magic, app descriptor, size vs. the inactive slot.
    //    No date/version comparison — downgrades are legitimate; the device
    //    only guards integrity.
    String newVersion;
    String errMsg;
    if (validateImageFile(STAGED_BIN, newVersion, errMsg) != ESP_OK) {
        ESP_LOGE(TAG, "Refusing update: %s", errMsg.c_str());
        writeResultFile(fromVersion, "", "refused: invalid image");
        ElocSystem::GetInstance().notifyFwUpdateError();
        return false;
    }

    // 2) SHA-256 verification against the metadata file, when present. The
    //    BT path (setFwUpdateApply) always writes it; the manual SD-swap path
    //    may omit it — warn but proceed for backward compatibility.
    if (ffsutil::fileExist(STAGED_META)) {
        String expectedSha;
        {
            FILE* f = fopen(STAGED_META, "r");
            if (f != NULL) {
                char buf[512] = {};
                fread(buf, 1, sizeof(buf) - 1, f);
                fclose(f);
                StaticJsonDocument<512> doc;
                if (deserializeJson(doc, buf) == DeserializationError::Ok) {
                    expectedSha = doc["sha256"] | "";
                }
            }
        }
        if (expectedSha.length() == 64) {
            char actualSha[65];
            if (sha256File(STAGED_BIN, actualSha) != ESP_OK ||
                !expectedSha.equalsIgnoreCase(actualSha)) {
                ESP_LOGE(TAG, "SHA-256 mismatch: expected %s", expectedSha.c_str());
                writeResultFile(fromVersion, newVersion.c_str(), "refused: sha256 mismatch");
                ElocSystem::GetInstance().notifyFwUpdateError();
                return false;
            }
            ESP_LOGI(TAG, "SHA-256 verified: %s", actualSha);
        } else {
            ESP_LOGW(TAG, "%s has no usable sha256 field, skipping hash check", STAGED_META);
        }
    } else {
        ESP_LOGW(TAG, "No %s metadata — manual SD-swap update without hash check", STAGED_META);
    }

    ESP_LOGI(TAG, "Updating ELOC firmware %s -> %s", fromVersion, newVersion.c_str());
    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Update partition: '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32,
             updatePartition->label, updatePartition->address, updatePartition->size);

    esp_err_t err = flashStagedImage(updatePartition, STAGED_BIN);

    if (err == ESP_OK) {
        writeResultFile(fromVersion, newVersion.c_str(), "flashed");
        // Clean up the staged files so the next boot doesn't re-flash and a
        // fresh transfer never resumes into a stale image.
        remove(STAGED_BIN);
        remove(STAGED_META);
        remove(TRANSFER_META);

        // Status LED is on the PCA9557 IO expander (GPIO4 is now the GPS UART TX).
        if (ElocSystem::GetInstance().hasIoExpander()) {
            ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_STATUS, true);
            vTaskDelay(pdMS_TO_TICKS(3000));
            ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_STATUS, false);
        }

        ESP_LOGI(TAG, "Prepare to restart system!");
        esp_restart();
        return true;  // not reached
    }

    ESP_LOGE(TAG, "Update failed (%s), keeping current firmware", esp_err_to_name(err));
    writeResultFile(fromVersion, newVersion.c_str(), "failed: flash error");
    ElocSystem::GetInstance().notifyFwUpdateError();
    return false;
}


void checkForFirmwareUpdateFile() {

    if (ffsutil::fileExist(TRIGGER_FILE)) {
        ESP_LOGI(TAG, "%s exists: Doing Firmware update now...", TRIGGER_FILE);
        if (remove(TRIGGER_FILE) == 0) {
            ESP_LOGI(TAG, "%s deleted successfully... doing update", TRIGGER_FILE);
        } else {
            ESP_LOGI(TAG, "unable to delete file %s", TRIGGER_FILE);
        }
        updateFirmware();
    }
    return;
}

void markRunningFirmwareValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "First boot after update: marked firmware %s as valid (rollback cancelled)",
                     gFirmwareVersion.c_str());
            // Promote the boot-time "flashed" record to a confirmed success,
            // keeping the fromVersion recorded by the updater.
            if (sd_card.isMounted()) {
                String fromVersion;
                FILE* f = fopen(RESULT_FILE, "r");
                if (f != NULL) {
                    char buf[512] = {};
                    fread(buf, 1, sizeof(buf) - 1, f);
                    fclose(f);
                    StaticJsonDocument<384> doc;
                    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
                        fromVersion = doc["fromVersion"] | "";
                    }
                }
                writeResultFile(fromVersion.c_str(), gFirmwareVersion.c_str(), "success");
            }
        } else {
            ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback failed (%s)", esp_err_to_name(err));
        }
    }
}

#endif  // GENERIC_HW
