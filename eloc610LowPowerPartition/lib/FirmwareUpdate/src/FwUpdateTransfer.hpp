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

#ifndef FIRMWAREUPDATE_FWUPDATETRANSFER_HPP_
#define FIRMWAREUPDATE_FWUPDATETRANSFER_HPP_

#ifndef GENERIC_HW

#include <stdio.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "WString.h"
#include "Stream.h"

#include "FwFrameParser.hpp"

/**
 * Bluetooth firmware-transfer state machine (Phase 1 of
 * README-FirmwareUpdate-From-App-Plan.md).
 *
 * The app stages a firmware binary onto the SD card over BT SPP:
 *   setFwUpdateBegin#meta={...}  -> preflight, open staging file, enter binary mode
 *   [binary frames]              -> stop-and-wait, each frame acked with EOT-JSON
 *   setFwUpdateApply             -> SHA-256 verify, write trigger, restart
 *
 * While binary mode is active the BluetoothSerial RX path is re-routed via
 * SerialBT.onData() into a FreeRTOS stream buffer (the default BluetoothSerial
 * RX queue is only 512 bytes and silently drops on overflow). The command
 * parser is bypassed; every error/timeout/disconnect exits binary mode and
 * keeps the partial file so the app can resume via getFwUpdateStatus + Begin.
 */
class FwUpdateTransfer {
 public:
    /// Firmware-enforced minimum battery voltage for starting a transfer/update.
    static constexpr float MIN_VOLTAGE_LIFEPO = 3.1f;
    static constexpr float MIN_VOLTAGE_LIPO   = 3.5f;
    /// Self-abort binary mode after this long without a complete frame.
    static constexpr int64_t RX_TIMEOUT_US = 30LL * 1000 * 1000;

    static FwUpdateTransfer& GetInstance() {
        static FwUpdateTransfer instance;
        return instance;
    }

    /**
     * Handle setFwUpdateBegin: parse meta JSON, run the preflight checks and
     * open the staging file. On ESP_OK the caller must route the raw BT data
     * stream to feed() (binary mode). resumeOffset > 0 if a matching partial
     * file exists (same sha256); the app then streams from that offset.
     */
    esp_err_t begin(const char* metaJson, String& errMsg, uint32_t& resumeOffset, uint16_t& chunkSize);

    /// True while raw BT data must bypass the command parser.
    bool isBinaryMode() const { return mBinaryMode; }

    /// Raw data sink, called from the BluetoothSerial data callback (BT task).
    void feed(const uint8_t* data, size_t len);

    /**
     * Pump received frames: parse, write to SD, ack via 'out'. Called
     * periodically from the BT command task while in binary mode.
     * Returns false once binary mode has ended (complete/error/timeout/
     * disconnect) — the caller must then unhook the data callback and call
     * releaseBinaryResources().
     */
    bool service(Stream& out, bool connected);

    /// Free stream buffer & payload buffer. Call only after unhooking feed().
    void releaseBinaryResources();

    /// Handle setFwUpdateAbort. discard=true also deletes the partial file.
    void abortTransfer(bool discard, String& msg);

    /// JSON payload for getFwUpdateStatus.
    void statusJson(String& payload);

    /**
     * Handle setFwUpdateApply: recompute SHA-256 of the staged file, compare
     * with the transfer metadata, sanity-check the image, write the staged
     * metadata + boot trigger. The caller schedules the restart on ESP_OK.
     */
    esp_err_t apply(String& errMsg);

 private:
    FwUpdateTransfer();

    enum class State { IDLE, RECEIVING, STAGED, ERROR };

    void endBinaryMode(State newState, const char* reason);
    bool handleFrame(Stream& out);
    void sendAck(Stream& out, uint16_t seq, int ecode, const char* state);
    /// Load size/sha from TRANSFER_META (after reboot or reconnect).
    bool loadPersistedMeta(String& sha256, uint32_t& size);

    // transfer metadata (valid while state != IDLE)
    String mSha256;
    String mVersion;
    String mVariant;
    uint32_t mExpectedSize = 0;
    uint16_t mChunkSize = 0;

    volatile bool mBinaryMode = false;
    State mState = State::IDLE;
    String mLastError;

    FILE* mFile = nullptr;
    uint32_t mBytesReceived = 0;
    uint16_t mExpectSeq = 0;
    int64_t mLastRxUs = 0;

    fwupd::FwFrameParser mParser;
    uint8_t* mPayloadBuf = nullptr;
    StreamBufferHandle_t mStream = nullptr;
    SemaphoreHandle_t mStreamMutex = nullptr;  // guards mStream between feed() (BT task) and release (cmd task)
};

#endif  // GENERIC_HW

#endif  // FIRMWAREUPDATE_FWUPDATETRANSFER_HPP_
