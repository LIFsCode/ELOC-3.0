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

#ifndef FIRMWAREUPDATE_FWFRAMEPARSER_HPP_
#define FIRMWAREUPDATE_FWFRAMEPARSER_HPP_

#include <stdint.h>
#include <stddef.h>

/**
 * Binary frame format used for the Bluetooth firmware transfer
 * (see README-FirmwareUpdate-From-App-Plan.md, Phase 1).
 *
 * All fields little-endian:
 *
 *   [seq:u16][len:u16][payload:len bytes][crc32:u32]
 *
 * - crc32 covers seq + len + payload (everything before the crc field),
 *   IEEE 802.3 reflected polynomial 0xEDB88320 — identical to
 *   java.util.zip.CRC32 on the Android side.
 * - len == 0 is an in-band "end of stream" sentinel: the app uses it to
 *   leave binary receive mode cleanly (pause/abort). It carries no payload.
 *
 * This header is intentionally free of any ESP/Arduino dependency so the
 * parser can be unit-tested in the native environment
 * (pio test -e generic_unit_tests).
 */
namespace fwupd {

/// Max payload accepted per frame. Must be >= the max negotiable chunkSize.
static const uint16_t FW_FRAME_MAX_PAYLOAD = 8192;
/// Default chunk size if the app does not request one in setFwUpdateBegin.
static const uint16_t FW_FRAME_DEFAULT_CHUNK = 4096;
static const size_t FW_FRAME_HEADER_SIZE = 4;
static const size_t FW_FRAME_CRC_SIZE = 4;

/// Software CRC32 (IEEE, reflected, poly 0xEDB88320). crc starts at 0.
inline uint32_t fwCrc32(uint32_t crc, const uint8_t* buf, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

/**
 * Incremental byte-stream parser for the frame format above.
 * Feed it one byte at a time; it reports a complete frame (or an error)
 * once the trailing CRC has been consumed. The caller owns the payload
 * buffer so it can live in PSRAM or be reused between frames.
 */
class FwFrameParser {
 public:
    enum class Status {
        NEED_MORE,   ///< frame not complete yet
        FRAME,       ///< complete frame with valid CRC available
        BAD_LEN,     ///< header announced a payload larger than maxPayload
        BAD_CRC,     ///< frame consumed but CRC mismatch
    };

    FwFrameParser() { setBuffer(nullptr, 0); }

    /// Payload destination; maxPayload bounds the accepted frame size.
    void setBuffer(uint8_t* payloadBuf, uint16_t maxPayload) {
        mPayload = payloadBuf;
        mMaxPayload = maxPayload;
        reset();
    }

    /// Discard any partial frame and start hunting for a new header.
    void reset() {
        mState = State::HEADER;
        mPos = 0;
        mCrc = 0;
        mSeq = 0;
        mLen = 0;
        mRxCrc = 0;
    }

    /// Feed a single byte. Returns FRAME exactly once per complete frame.
    Status feed(uint8_t byte) {
        switch (mState) {
            case State::HEADER:
                mHdr[mPos++] = byte;
                if (mPos == FW_FRAME_HEADER_SIZE) {
                    mSeq = static_cast<uint16_t>(mHdr[0]) | (static_cast<uint16_t>(mHdr[1]) << 8);
                    mLen = static_cast<uint16_t>(mHdr[2]) | (static_cast<uint16_t>(mHdr[3]) << 8);
                    mCrc = fwCrc32(0, mHdr, FW_FRAME_HEADER_SIZE);
                    if (mLen > mMaxPayload || (mLen > 0 && mPayload == nullptr)) {
                        reset();
                        return Status::BAD_LEN;
                    }
                    mPos = 0;
                    mState = (mLen == 0) ? State::CRC : State::PAYLOAD;
                }
                return Status::NEED_MORE;

            case State::PAYLOAD:
                mPayload[mPos++] = byte;
                if (mPos == mLen) {
                    mCrc = fwCrc32(0, mHdr, FW_FRAME_HEADER_SIZE);  // header part
                    // CRC over payload is accumulated below in one go to keep the
                    // per-byte path cheap: recompute continuation over the payload.
                    mCrc = fwCrc32Continue(mCrc, mPayload, mLen);
                    mPos = 0;
                    mState = State::CRC;
                }
                return Status::NEED_MORE;

            case State::CRC:
                mRxCrc |= static_cast<uint32_t>(byte) << (8 * mPos);
                mPos++;
                if (mPos == FW_FRAME_CRC_SIZE) {
                    uint32_t expected = mCrc;
                    uint32_t received = mRxCrc;
                    uint16_t seq = mSeq;
                    uint16_t len = mLen;
                    reset();
                    mLastSeq = seq;
                    mLastLen = len;
                    return (expected == received) ? Status::FRAME : Status::BAD_CRC;
                }
                return Status::NEED_MORE;
        }
        return Status::NEED_MORE;
    }

    /// Valid after feed() returned FRAME or BAD_CRC: header fields of that frame.
    uint16_t frameSeq() const { return mLastSeq; }
    uint16_t frameLen() const { return mLastLen; }

 private:
    /// CRC continuation: extend an existing CRC over more data.
    static uint32_t fwCrc32Continue(uint32_t crc, const uint8_t* buf, size_t len) {
        crc = ~crc;
        for (size_t i = 0; i < len; i++) {
            crc ^= buf[i];
            for (int k = 0; k < 8; k++) {
                crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
            }
        }
        return ~crc;
    }

    enum class State { HEADER, PAYLOAD, CRC };

    State mState = State::HEADER;
    uint8_t mHdr[FW_FRAME_HEADER_SIZE] = {};
    uint8_t* mPayload = nullptr;
    uint16_t mMaxPayload = 0;
    size_t mPos = 0;
    uint32_t mCrc = 0;
    uint32_t mRxCrc = 0;
    uint16_t mSeq = 0;
    uint16_t mLen = 0;
    uint16_t mLastSeq = 0;
    uint16_t mLastLen = 0;
};

}  // namespace fwupd

#endif  // FIRMWAREUPDATE_FWFRAMEPARSER_HPP_
