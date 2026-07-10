/*
 * Created on Sun Jul 06 2026
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2026 EDsteve
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

#include <sys/stat.h>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"

#include "ArduinoJson.h"

#include "ffsutils.h"
#include "SDCardSDIO.h"
#include "Battery.hpp"
#include "ElocStatus.hpp"
#include "FirmwareUpdate.hpp"
#include "FwUpdateTransfer.hpp"

static const char* TAG = "FwUpdateTransfer";

extern SDCardSDIO sd_card;

using namespace fwupd;

/// Free space margin required on top of the remaining transfer size.
static const uint64_t FREE_SPACE_MARGIN_BYTES = 1024 * 1024;

FwUpdateTransfer::FwUpdateTransfer() {
    mStreamMutex = xSemaphoreCreateMutex();
}

bool FwUpdateTransfer::loadPersistedMeta(String& sha256, uint32_t& size) {
    FILE* f = fopen(TRANSFER_META, "r");
    if (f == NULL) {
        return false;
    }
    char buf[512] = {};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        return false;
    }
    const char* sha = doc["sha256"] | "";
    sha256 = sha;
    size = doc["size"] | 0;
    return sha256.length() == 64 && size > 0;
}

esp_err_t FwUpdateTransfer::begin(const char* metaJson, String& errMsg, uint32_t& resumeOffset, uint16_t& chunkSize) {
    resumeOffset = 0;

    if (mBinaryMode || mState == State::RECEIVING) {
        errMsg = "transfer already in progress";
        return ESP_ERR_INVALID_STATE;
    }

    // --- parse meta ---------------------------------------------------------
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, metaJson) != DeserializationError::Ok) {
        errMsg = "invalid meta JSON";
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t size = doc["size"] | 0;
    String sha256 = doc["sha256"] | "";
    String version = doc["version"] | "";
    String variant = doc["variant"] | "";
    uint32_t reqChunk = doc["chunkSize"] | FW_FRAME_DEFAULT_CHUNK;

    if (size == 0) {
        errMsg = "meta is missing 'size'";
        return ESP_ERR_INVALID_ARG;
    }
    if (sha256.length() != 64) {
        errMsg = "meta 'sha256' must be 64 hex chars";
        return ESP_ERR_INVALID_ARG;
    }
    sha256.toLowerCase();
    if (reqChunk < 512) reqChunk = 512;
    if (reqChunk > FW_FRAME_MAX_PAYLOAD) reqChunk = FW_FRAME_MAX_PAYLOAD;

    // --- preflight (distinct, app-displayable reasons) ----------------------
    if (wav_writer.get_mode() != WAVFileWriter::Mode::disabled || ai_run_enable || g_ai_start_pending) {
        errMsg = "recording or AI detection is active - stop it first";
        return ESP_ERR_INVALID_STATE;
    }
    if (!sd_card.isMounted()) {
        errMsg = "SD card not mounted";
        return ESP_ERR_INVALID_STATE;
    }

    Battery& bat = Battery::GetInstance();
    bat.updateVoltage(true);
    const char* batType = bat.getBatType();
    float minVoltage = 0.0f;
    if (strcmp(batType, "LiFePo") == 0) {
        minVoltage = MIN_VOLTAGE_LIFEPO;
    } else if (strcmp(batType, "LiPo") == 0) {
        minVoltage = MIN_VOLTAGE_LIPO;
    }  // BAT_NONE (bench/USB powered): no voltage gate
    if (minVoltage > 0.0f && bat.getVoltage() < minVoltage) {
        errMsg = "battery too low for update (";
        errMsg += String(bat.getVoltage(), 2);
        errMsg += " V < ";
        errMsg += String(minVoltage, 1);
        errMsg += " V ";
        errMsg += batType;
        errMsg += ")";
        return ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        errMsg = "no inactive OTA partition";
        return ESP_ERR_NOT_FOUND;
    }
    if (size > target->size) {
        errMsg = "image larger than OTA slot";
        return ESP_ERR_INVALID_SIZE;
    }

    // --- resume detection ----------------------------------------------------
    bool resuming = false;
    long partialSize = ffsutil::getFileSize(STAGED_BIN);
    if (partialSize > 0) {
        String prevSha;
        uint32_t prevSize = 0;
        if (loadPersistedMeta(prevSha, prevSize) &&
            prevSha.equalsIgnoreCase(sha256) && prevSize == size &&
            static_cast<uint32_t>(partialSize) <= size) {
            resuming = true;
            resumeOffset = partialSize;
            ESP_LOGI(TAG, "Resuming previous transfer at offset %lu", static_cast<unsigned long>(resumeOffset));
        }
    }

    // Fully staged already (e.g. the final ack of a previous session was lost
    // in a disconnect): nothing left to receive. Don't enter binary mode —
    // report it staged so the app proceeds straight to setFwUpdateApply.
    if (resuming && resumeOffset == size) {
        mSha256 = sha256;
        mVersion = version;
        mVariant = variant;
        mExpectedSize = size;
        mChunkSize = static_cast<uint16_t>(reqChunk);
        chunkSize = mChunkSize;
        mBytesReceived = size;
        mLastError = "";
        mState = State::STAGED;
        ESP_LOGI(TAG, "Transfer already fully staged (%lu bytes), skipping binary mode",
                 static_cast<unsigned long>(size));
        return ESP_OK;
    }

    // --- free space (only what still needs to be received + margin) ---------
    sd_card.update();
    uint64_t needed = static_cast<uint64_t>(size - resumeOffset) + FREE_SPACE_MARGIN_BYTES;
    if (sd_card.getFreeBytes() < needed) {
        errMsg = "not enough free space on SD card";
        return ESP_ERR_NO_MEM;
    }

    // --- stage file + persist meta ------------------------------------------
    if (!ffsutil::folderExists(UPDATE_DIR)) {
        if (mkdir(UPDATE_DIR, 0777) != 0) {
            errMsg = "failed to create update folder";
            return ESP_FAIL;
        }
    }
    {
        FILE* f = fopen(TRANSFER_META, "w");
        if (f == NULL) {
            errMsg = "failed to write transfer metadata";
            return ESP_FAIL;
        }
        StaticJsonDocument<512> meta;
        meta["sha256"] = sha256;
        meta["size"] = size;
        meta["version"] = version;
        meta["variant"] = variant;
        String out;
        serializeJson(meta, out);
        fputs(out.c_str(), f);
        fclose(f);
    }

    mFile = fopen(STAGED_BIN, resuming ? "ab" : "wb");
    if (mFile == NULL) {
        errMsg = "failed to open staging file";
        return ESP_FAIL;
    }

    // --- binary receive resources -------------------------------------------
    releaseBinaryResources();  // in case a previous session leaked
    // payload buffer prefers PSRAM to spare the tight internal heap
    mPayloadBuf = static_cast<uint8_t*>(heap_caps_malloc(reqChunk, MALLOC_CAP_SPIRAM));
    if (mPayloadBuf == nullptr) {
        mPayloadBuf = static_cast<uint8_t*>(malloc(reqChunk));
    }
    // stream buffer must hold one full frame + slack (stop-and-wait: only one
    // frame is ever in flight, so overflow is impossible by construction)
    StreamBufferHandle_t stream = xStreamBufferCreate(
        reqChunk + FW_FRAME_HEADER_SIZE + FW_FRAME_CRC_SIZE + 1024, 1);
    if (mPayloadBuf == nullptr || stream == nullptr) {
        if (stream != nullptr) vStreamBufferDelete(stream);
        releaseBinaryResources();
        fclose(mFile);
        mFile = nullptr;
        errMsg = "out of memory for transfer buffers";
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(mStreamMutex, portMAX_DELAY) == pdTRUE) {
        mStream = stream;
        xSemaphoreGive(mStreamMutex);
    }

    mSha256 = sha256;
    mVersion = version;
    mVariant = variant;
    mExpectedSize = size;
    mChunkSize = static_cast<uint16_t>(reqChunk);
    chunkSize = mChunkSize;
    mBytesReceived = resumeOffset;
    mExpectSeq = 0;
    mParser.setBuffer(mPayloadBuf, mChunkSize);
    mLastRxUs = esp_timer_get_time();
    mLastError = "";
    mState = State::RECEIVING;
    mBinaryMode = true;

    ESP_LOGI(TAG, "Transfer started: %lu bytes, chunk %u, resume offset %lu, version '%s' (%s)",
             static_cast<unsigned long>(size), mChunkSize,
             static_cast<unsigned long>(resumeOffset), version.c_str(), variant.c_str());
    return ESP_OK;
}

void FwUpdateTransfer::feed(const uint8_t* data, size_t len) {
    if (!mBinaryMode) {
        return;
    }
    if (xSemaphoreTake(mStreamMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (mStream != nullptr) {
        size_t sent = xStreamBufferSend(mStream, data, len, 0);
        if (sent != len) {
            // Can only happen if the app violates stop-and-wait; the resulting
            // short/corrupt frame is caught by the parser and NAKed.
            ESP_LOGW(TAG, "RX stream overflow, dropped %u bytes", static_cast<unsigned>(len - sent));
        }
    }
    xSemaphoreGive(mStreamMutex);
}

void FwUpdateTransfer::sendAck(Stream& out, uint16_t seq, int ecode, const char* state) {
    // Same EOT-terminated JSON envelope as bt_sendResponse so the app's
    // normal read loop can route it (cmd == "fwFrame").
    StaticJsonDocument<256> doc;
    doc["id"] = -1;
    doc["cmd"] = "fwFrame";
    doc["ecode"] = ecode;
    JsonObject payload = doc.createNestedObject("payload");
    payload["seq"] = seq;
    payload["received"] = mBytesReceived;
    payload["state"] = state;
    String json;
    serializeJson(doc, json);
    out.println(json);
    out.print(static_cast<char>(0x04));
}

void FwUpdateTransfer::endBinaryMode(State newState, const char* reason) {
    if (mFile != nullptr) {
        fclose(mFile);
        mFile = nullptr;
    }
    mState = newState;
    if (newState == State::ERROR) {
        mLastError = reason;
    }
    mBinaryMode = false;
    ESP_LOGI(TAG, "Binary mode ended (%s), %lu/%lu bytes staged", reason,
             static_cast<unsigned long>(mBytesReceived), static_cast<unsigned long>(mExpectedSize));
}

bool FwUpdateTransfer::handleFrame(Stream& out) {
    const uint16_t seq = mParser.frameSeq();
    const uint16_t len = mParser.frameLen();

    mLastRxUs = esp_timer_get_time();

    if (len == 0) {
        // In-band end-of-stream sentinel: the app pauses/aborts cleanly.
        // Keep the partial file for resume.
        sendAck(out, seq, 0, (mBytesReceived >= mExpectedSize) ? "staged" : "idle");
        endBinaryMode((mBytesReceived >= mExpectedSize) ? State::STAGED : State::IDLE, "end-of-stream");
        return false;
    }

    if (seq == static_cast<uint16_t>(mExpectSeq - 1)) {
        // Duplicate of the last acked frame (ack lost) — re-ack, don't write.
        sendAck(out, seq, 0, "receiving");
        return true;
    }
    if (seq != mExpectSeq) {
        ESP_LOGE(TAG, "Frame seq mismatch: got %u, expected %u", seq, mExpectSeq);
        sendAck(out, seq, ESP_ERR_INVALID_STATE, "error");
        endBinaryMode(State::ERROR, "sequence mismatch");
        return false;
    }
    if (mBytesReceived + len > mExpectedSize) {
        sendAck(out, seq, ESP_ERR_INVALID_SIZE, "error");
        endBinaryMode(State::ERROR, "more data than announced size");
        return false;
    }

    if (fwrite(mPayloadBuf, 1, len, mFile) != len) {
        ESP_LOGE(TAG, "SD write failed at offset %lu", static_cast<unsigned long>(mBytesReceived));
        sendAck(out, seq, ESP_FAIL, "error");
        endBinaryMode(State::ERROR, "SD write failed");
        return false;
    }
    mBytesReceived += len;
    mExpectSeq++;

    if (mBytesReceived >= mExpectedSize) {
        fflush(mFile);
        sendAck(out, seq, 0, "staged");
        endBinaryMode(State::STAGED, "complete");
        return false;
    }
    sendAck(out, seq, 0, "receiving");
    return true;
}

bool FwUpdateTransfer::service(Stream& out, bool connected) {
    if (!mBinaryMode) {
        return false;
    }
    if (!connected) {
        endBinaryMode(State::IDLE, "BT disconnect");
        return false;
    }

    // Drain and parse for up to ~200 ms per call so the surrounding BT task
    // loop (LED/status handling) keeps running while acks stay prompt.
    const int64_t budgetUs = 200 * 1000;
    const int64_t startUs = esp_timer_get_time();
    uint8_t rx[256];
    while ((esp_timer_get_time() - startUs) < budgetUs) {
        size_t n = 0;
        if (mStream != nullptr) {
            n = xStreamBufferReceive(mStream, rx, sizeof(rx), pdMS_TO_TICKS(50));
        }
        if (n == 0) {
            break;  // nothing pending right now
        }
        for (size_t i = 0; i < n; i++) {
            fwupd::FwFrameParser::Status st = mParser.feed(rx[i]);
            switch (st) {
                case fwupd::FwFrameParser::Status::NEED_MORE:
                    break;
                case fwupd::FwFrameParser::Status::FRAME:
                    if (!handleFrame(out)) {
                        return false;
                    }
                    break;
                case fwupd::FwFrameParser::Status::BAD_CRC:
                    ESP_LOGE(TAG, "Frame CRC error at seq %u", mParser.frameSeq());
                    sendAck(out, mParser.frameSeq(), ESP_ERR_INVALID_CRC, "error");
                    endBinaryMode(State::ERROR, "CRC error");
                    return false;
                case fwupd::FwFrameParser::Status::BAD_LEN:
                    ESP_LOGE(TAG, "Frame length exceeds chunk size");
                    sendAck(out, 0, ESP_ERR_INVALID_SIZE, "error");
                    endBinaryMode(State::ERROR, "frame too large");
                    return false;
            }
        }
    }

    if ((esp_timer_get_time() - mLastRxUs) > RX_TIMEOUT_US) {
        ESP_LOGW(TAG, "No frame for %lld s - leaving binary mode (partial kept for resume)",
                 RX_TIMEOUT_US / 1000000);
        endBinaryMode(State::IDLE, "RX timeout");
        return false;
    }
    return true;
}

void FwUpdateTransfer::releaseBinaryResources() {
    if (xSemaphoreTake(mStreamMutex, portMAX_DELAY) == pdTRUE) {
        if (mStream != nullptr) {
            vStreamBufferDelete(mStream);
            mStream = nullptr;
        }
        xSemaphoreGive(mStreamMutex);
    }
    if (mPayloadBuf != nullptr) {
        free(mPayloadBuf);
        mPayloadBuf = nullptr;
        mParser.setBuffer(nullptr, 0);
    }
}

void FwUpdateTransfer::abortTransfer(bool discard, String& msg) {
    if (mBinaryMode) {  // defensive - commands cannot arrive while in binary mode
        endBinaryMode(State::IDLE, "abort command");
    }
    if (mFile != nullptr) {
        fclose(mFile);
        mFile = nullptr;
    }
    if (discard) {
        remove(STAGED_BIN);
        remove(TRANSFER_META);
        mState = State::IDLE;
        mBytesReceived = 0;
        mExpectedSize = 0;
        msg = "transfer aborted, partial file discarded";
    } else {
        mState = State::IDLE;
        msg = "transfer aborted, partial file kept for resume";
    }
    ESP_LOGI(TAG, "%s", msg.c_str());
}

void FwUpdateTransfer::statusJson(String& payload) {
    // Reflect on-disk reality so status is correct even right after a reboot
    // or reconnect (resume flow starts with this command).
    long partialSize = ffsutil::getFileSize(STAGED_BIN);
    String sha = mSha256;
    uint32_t expected = mExpectedSize;
    if (sha.length() != 64 || expected == 0) {
        uint32_t persistedSize = 0;
        String persistedSha;
        if (loadPersistedMeta(persistedSha, persistedSize)) {
            sha = persistedSha;
            expected = persistedSize;
        }
    }

    const char* state = "idle";
    if (mBinaryMode) {
        state = "receiving";
    } else if (mState == State::ERROR) {
        state = "error";
    } else if (partialSize > 0 && expected > 0 && static_cast<uint32_t>(partialSize) == expected) {
        state = "staged";
    }

    StaticJsonDocument<512> doc;
    doc["state"] = state;
    doc["bytesReceived"] = (partialSize > 0) ? partialSize : 0;
    doc["expectedSize"] = expected;
    doc["sha256"] = sha;
    if (mLastError.length() > 0) {
        doc["error"] = mLastError;
    }
    serializeJson(doc, payload);
}

esp_err_t FwUpdateTransfer::apply(String& errMsg) {
    if (mBinaryMode) {
        errMsg = "transfer still in progress";
        return ESP_ERR_INVALID_STATE;
    }

    // Trust only the on-disk state: works also when apply is issued in a
    // fresh session (staged earlier, applied after reconnect/reboot).
    String sha = mSha256;
    uint32_t expected = mExpectedSize;
    if (sha.length() != 64 || expected == 0) {
        if (!loadPersistedMeta(sha, expected)) {
            errMsg = "no staged transfer metadata found";
            return ESP_ERR_NOT_FOUND;
        }
    }
    long stagedSize = ffsutil::getFileSize(STAGED_BIN);
    if (stagedSize <= 0 || static_cast<uint32_t>(stagedSize) != expected) {
        errMsg = "staged file incomplete (";
        errMsg += String(stagedSize);
        errMsg += " of ";
        errMsg += String(expected);
        errMsg += " bytes)";
        return ESP_ERR_INVALID_SIZE;
    }

    char actualSha[65];
    if (sha256File(STAGED_BIN, actualSha) != ESP_OK) {
        errMsg = "failed to hash staged file";
        return ESP_FAIL;
    }
    if (!sha.equalsIgnoreCase(actualSha)) {
        errMsg = "SHA-256 mismatch on staged file";
        mState = State::ERROR;
        mLastError = errMsg;
        return ESP_ERR_INVALID_CRC;
    }

    String newVersion;
    esp_err_t err = validateImageFile(STAGED_BIN, newVersion, errMsg);
    if (err != ESP_OK) {
        mState = State::ERROR;
        mLastError = errMsg;
        return err;
    }

    // Boot-time metadata for the SD updater's own hash check
    {
        FILE* f = fopen(STAGED_META, "w");
        if (f == NULL) {
            errMsg = "failed to write update metadata";
            return ESP_FAIL;
        }
        StaticJsonDocument<512> meta;
        meta["sha256"] = sha;
        meta["size"] = expected;
        meta["version"] = newVersion;
        String out;
        serializeJson(meta, out);
        fputs(out.c_str(), f);
        fclose(f);
    }
    {
        FILE* f = fopen(TRIGGER_FILE, "w");
        if (f == NULL) {
            errMsg = "failed to write update trigger";
            return ESP_FAIL;
        }
        fputs("update staged via Bluetooth\n", f);
        fclose(f);
    }
    ESP_LOGI(TAG, "Update applied: %s staged for flash on next boot (version '%s')",
             STAGED_BIN, newVersion.c_str());
    return ESP_OK;
}

#endif  // GENERIC_HW
