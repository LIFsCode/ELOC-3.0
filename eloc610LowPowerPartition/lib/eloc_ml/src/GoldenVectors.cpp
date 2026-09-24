/**
 * @file GoldenVectors.cpp
 * @brief Reader for eloc_golden_vectors.bin (see GoldenVectors.hpp)
 */

#include "GoldenVectors.hpp"

#include <string.h>

namespace eloc_ml {

namespace {

constexpr size_t kHeaderBytes = 32;
constexpr size_t kNameBytes = 28;

size_t pad4(size_t n) {
    return (n + 3) & ~static_cast<size_t>(3);
}

uint32_t readU32(const uint8_t* p) {
    // Little-endian, like the file
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool GoldenVectors::parse(const uint8_t* data, size_t len) {
    mData = nullptr;
    mLen = 0;
    mHeader = GoldenHeader();
    if (data == nullptr || len < kHeaderBytes || memcmp(data, "ELGV", 4) != 0) {
        mError = "not an ELOC golden-vector file";
        return false;
    }
    if (reinterpret_cast<uintptr_t>(data) % 4 != 0) {
        mError = "golden vectors must be 4-byte aligned in memory";
        return false;
    }
    GoldenHeader h;
    h.version = readU32(data + 4);
    h.count = readU32(data + 8);
    h.nSamples = readU32(data + 12);
    h.nFrames = readU32(data + 16);
    h.nMels = readU32(data + 20);
    h.nOutputs = readU32(data + 24);
    h.recordBytes = readU32(data + 28);
    if (h.version != 1) {
        mError = "unsupported golden-vector version";
        return false;
    }
    const size_t features = static_cast<size_t>(h.nFrames) * h.nMels;
    const size_t expected = 4 + kNameBytes + pad4(2 * static_cast<size_t>(h.nSamples)) + 4 * features +
                            pad4(features) + pad4(h.nOutputs) + 4 * static_cast<size_t>(h.nOutputs);
    if (h.recordBytes != expected) {
        mError = "golden-vector record size does not match its header";
        return false;
    }
    if (kHeaderBytes + static_cast<size_t>(h.count) * h.recordBytes > len) {
        mError = "golden-vector file is truncated";
        return false;
    }
    mData = data;
    mLen = len;
    mHeader = h;
    mError = "";
    return true;
}

bool GoldenVectors::record(uint32_t i, GoldenRecord& out) const {
    if (mData == nullptr || i >= mHeader.count) {
        return false;
    }
    const size_t features = static_cast<size_t>(mHeader.nFrames) * mHeader.nMels;
    const uint8_t* p = mData + kHeaderBytes + static_cast<size_t>(i) * mHeader.recordBytes;

    out.labelIndex = readU32(p);
    p += 4;
    memcpy(out.name, p, kNameBytes);
    out.name[kNameBytes] = '\0';
    p += kNameBytes;
    out.samples = reinterpret_cast<const int16_t*>(p);
    p += pad4(2 * static_cast<size_t>(mHeader.nSamples));
    out.features = reinterpret_cast<const float*>(p);
    p += 4 * features;
    out.input = reinterpret_cast<const int8_t*>(p);
    p += pad4(features);
    out.output = reinterpret_cast<const int8_t*>(p);
    p += pad4(mHeader.nOutputs);
    out.scores = reinterpret_cast<const float*>(p);
    return true;
}

}  // namespace eloc_ml
