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

// Native unit test for the BT firmware-transfer frame parser
// (lib/FirmwareUpdate/src/FwFrameParser.hpp). Run with:
//   pio test -e generic_unit_tests

#include <string.h>
#include <vector>

#include "unity.h"
#include "../../lib/FirmwareUpdate/src/FwFrameParser.hpp"

using fwupd::FwFrameParser;
using fwupd::fwCrc32;

static uint8_t payloadBuf[fwupd::FW_FRAME_MAX_PAYLOAD];
static FwFrameParser parser;

void setUp(void) {
    parser.setBuffer(payloadBuf, sizeof(payloadBuf));
}

void tearDown(void) {
}

/// Build a wire-format frame: [seq:u16 LE][len:u16 LE][payload][crc32:u32 LE]
static std::vector<uint8_t> buildFrame(uint16_t seq, const uint8_t* payload, uint16_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(seq & 0xFF);
    frame.push_back(seq >> 8);
    frame.push_back(len & 0xFF);
    frame.push_back(len >> 8);
    for (uint16_t i = 0; i < len; i++) {
        frame.push_back(payload[i]);
    }
    uint32_t crc = fwCrc32(0, frame.data(), frame.size());
    for (int i = 0; i < 4; i++) {
        frame.push_back((crc >> (8 * i)) & 0xFF);
    }
    return frame;
}

/// Feed a byte vector; returns the terminal parser status of the LAST byte.
static FwFrameParser::Status feedAll(const std::vector<uint8_t>& data) {
    FwFrameParser::Status st = FwFrameParser::Status::NEED_MORE;
    for (uint8_t b : data) {
        st = parser.feed(b);
    }
    return st;
}

void test_crc32_known_value() {
    // CRC32("123456789") = 0xCBF43926 (standard IEEE check value,
    // identical to java.util.zip.CRC32 on the app side)
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, fwCrc32(0, data, 9));
}

void test_single_frame_ok() {
    uint8_t payload[100];
    for (int i = 0; i < 100; i++) payload[i] = i;
    auto frame = buildFrame(7, payload, 100);

    TEST_ASSERT_EQUAL(FwFrameParser::Status::FRAME, feedAll(frame));
    TEST_ASSERT_EQUAL_UINT16(7, parser.frameSeq());
    TEST_ASSERT_EQUAL_UINT16(100, parser.frameLen());
    TEST_ASSERT_EQUAL_MEMORY(payload, payloadBuf, 100);
}

void test_frame_only_reported_once() {
    uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto frame = buildFrame(0, payload, 10);

    int frames = 0;
    for (uint8_t b : frame) {
        if (parser.feed(b) == FwFrameParser::Status::FRAME) frames++;
    }
    TEST_ASSERT_EQUAL_INT(1, frames);
}

void test_back_to_back_frames() {
    uint8_t p1[50], p2[60];
    memset(p1, 0xAA, sizeof(p1));
    memset(p2, 0x55, sizeof(p2));
    auto f1 = buildFrame(0, p1, 50);
    auto f2 = buildFrame(1, p2, 60);
    f1.insert(f1.end(), f2.begin(), f2.end());

    int frames = 0;
    uint16_t lastSeq = 0xFFFF;
    for (uint8_t b : f1) {
        if (parser.feed(b) == FwFrameParser::Status::FRAME) {
            frames++;
            lastSeq = parser.frameSeq();
        }
    }
    TEST_ASSERT_EQUAL_INT(2, frames);
    TEST_ASSERT_EQUAL_UINT16(1, lastSeq);
    TEST_ASSERT_EQUAL_UINT16(60, parser.frameLen());
    TEST_ASSERT_EQUAL_MEMORY(p2, payloadBuf, 60);
}

void test_corrupted_payload_detected() {
    uint8_t payload[32];
    memset(payload, 0x42, sizeof(payload));
    auto frame = buildFrame(3, payload, 32);
    frame[4 + 16] ^= 0x01;  // flip one payload bit

    TEST_ASSERT_EQUAL(FwFrameParser::Status::BAD_CRC, feedAll(frame));
    TEST_ASSERT_EQUAL_UINT16(3, parser.frameSeq());
}

void test_corrupted_header_detected() {
    uint8_t payload[32];
    memset(payload, 0x42, sizeof(payload));
    auto frame = buildFrame(3, payload, 32);
    frame[0] ^= 0x01;  // corrupt seq byte -> CRC over header+payload must fail

    TEST_ASSERT_EQUAL(FwFrameParser::Status::BAD_CRC, feedAll(frame));
}

void test_oversized_length_rejected_early() {
    // Header announcing more than maxPayload must be rejected as soon as the
    // header is complete, without waiting for (or writing) any payload.
    parser.setBuffer(payloadBuf, 128);
    uint16_t len = 129;
    std::vector<uint8_t> hdr = {0, 0, static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8)};

    TEST_ASSERT_EQUAL(FwFrameParser::Status::NEED_MORE, parser.feed(hdr[0]));
    TEST_ASSERT_EQUAL(FwFrameParser::Status::NEED_MORE, parser.feed(hdr[1]));
    TEST_ASSERT_EQUAL(FwFrameParser::Status::NEED_MORE, parser.feed(hdr[2]));
    TEST_ASSERT_EQUAL(FwFrameParser::Status::BAD_LEN, parser.feed(hdr[3]));
}

void test_zero_length_sentinel() {
    // len == 0 is the in-band end-of-stream sentinel and carries no payload
    auto frame = buildFrame(42, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(8, frame.size());  // header + crc only

    TEST_ASSERT_EQUAL(FwFrameParser::Status::FRAME, feedAll(frame));
    TEST_ASSERT_EQUAL_UINT16(42, parser.frameSeq());
    TEST_ASSERT_EQUAL_UINT16(0, parser.frameLen());
}

void test_max_payload_frame() {
    static uint8_t big[fwupd::FW_FRAME_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = static_cast<uint8_t>(i * 7);
    auto frame = buildFrame(1000, big, sizeof(big));

    TEST_ASSERT_EQUAL(FwFrameParser::Status::FRAME, feedAll(frame));
    TEST_ASSERT_EQUAL_UINT16(1000, parser.frameSeq());
    TEST_ASSERT_EQUAL_UINT16(sizeof(big), parser.frameLen());
    TEST_ASSERT_EQUAL_MEMORY(big, payloadBuf, sizeof(big));
}

void test_recovery_after_bad_crc() {
    // After a corrupt frame the parser resets and must parse the next clean
    // frame (the transfer exits binary mode on NAK, but the parser itself
    // must not stay wedged).
    uint8_t payload[16];
    memset(payload, 0x11, sizeof(payload));
    auto bad = buildFrame(0, payload, 16);
    bad[bad.size() - 1] ^= 0xFF;  // corrupt CRC
    TEST_ASSERT_EQUAL(FwFrameParser::Status::BAD_CRC, feedAll(bad));

    auto good = buildFrame(1, payload, 16);
    TEST_ASSERT_EQUAL(FwFrameParser::Status::FRAME, feedAll(good));
    TEST_ASSERT_EQUAL_UINT16(1, parser.frameSeq());
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_value);
    RUN_TEST(test_single_frame_ok);
    RUN_TEST(test_frame_only_reported_once);
    RUN_TEST(test_back_to_back_frames);
    RUN_TEST(test_corrupted_payload_detected);
    RUN_TEST(test_corrupted_header_detected);
    RUN_TEST(test_oversized_length_rejected_early);
    RUN_TEST(test_zero_length_sentinel);
    RUN_TEST(test_max_payload_frame);
    RUN_TEST(test_recovery_after_bad_crc);
    return UNITY_END();
}

int main(void) {
    return runUnityTests();
}
