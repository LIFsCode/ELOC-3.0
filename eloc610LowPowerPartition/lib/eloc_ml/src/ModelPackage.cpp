/**
 * @file ModelPackage.cpp
 * @brief Parse and validate an ELOC device model package (see ModelPackage.hpp)
 */

#include "ModelPackage.hpp"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

#include "MlAlloc.hpp"
#include "tensorflow/lite/schema/schema_generated.h"

namespace eloc_ml {

namespace {

// Sanity limits. They only reject garbage; every real package is far inside them.
constexpr uint32_t kMaxWindowSamples = 96000;
constexpr uint32_t kMaxFftLength = 4096;
constexpr uint32_t kMaxMels = 512;

// TFLITE_SCHEMA_VERSION lives in micro_interpreter.h, which does not build on the host
constexpr uint32_t kTfliteSchemaVersion = 3;

/// ArduinoJson allocator placing the (temporary) document in PSRAM on the device
struct LargeJsonAllocator {
    void* allocate(size_t size) { return mlAlloc(size, MemKind::Large, 8); }
    void deallocate(void* ptr) { mlFree(ptr); }
    // Only used by shrinkToFit()/garbageCollect(), which this parser never calls
    void* reallocate(void*, size_t) { return nullptr; }
};
using JsonDoc = BasicJsonDocument<LargeJsonAllocator>;

bool getUint(JsonVariantConst v, uint32_t& out) {
    if (!v.is<uint32_t>()) {
        return false;
    }
    out = v.as<uint32_t>();
    return true;
}

bool getInt(JsonVariantConst v, int32_t& out) {
    if (!v.is<int32_t>()) {
        return false;
    }
    out = v.as<int32_t>();
    return true;
}

bool getFloat(JsonVariantConst v, float& out) {
    if (!v.is<float>()) {
        return false;
    }
    out = v.as<float>();
    return isfinite(out);
}

/// true when @p v is absent, or a string equal to @p expected
bool absentOrEquals(JsonVariantConst v, const char* expected) {
    if (v.isNull()) {
        return true;
    }
    const char* s = v.as<const char*>();
    return s != nullptr && strcmp(s, expected) == 0;
}

void copyString(char* dst, size_t dstLen, const char* src) {
    snprintf(dst, dstLen, "%s", src != nullptr ? src : "");
}

bool isPowerOfTwo(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

bool equalsIgnoreCase(const char* a, const char* b) {
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) {
            return false;
        }
    }
    return *a == *b;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/**
 * @brief Decode standard base64 (with '=' padding) into @p out
 * @return number of bytes written, or -1 when the input is malformed or @p outLen is too small
 */
long base64Decode(const char* in, size_t inLen, uint8_t* out, size_t outLen) {
    if (inLen % 4 != 0) {
        return -1;
    }
    size_t o = 0;
    for (size_t i = 0; i < inLen; i += 4) {
        int v[4];
        int pad = 0;
        for (int j = 0; j < 4; j++) {
            char c = in[i + j];
            if (c == '=') {
                // Padding only in the last group, and only at its end
                if (i + 4 != inLen || j < 2) {
                    return -1;
                }
                v[j] = 0;
                pad++;
            } else {
                if (pad > 0) {
                    return -1;
                }
                v[j] = base64Value(c);
                if (v[j] < 0) {
                    return -1;
                }
            }
        }
        uint32_t triple = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) |
                          (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
        int bytes = 3 - pad;
        if (o + bytes > outLen) {
            return -1;
        }
        out[o++] = static_cast<uint8_t>(triple >> 16);
        if (bytes > 1) out[o++] = static_cast<uint8_t>(triple >> 8);
        if (bytes > 2) out[o++] = static_cast<uint8_t>(triple);
    }
    return static_cast<long>(o);
}

}  // namespace

ModelPackage::~ModelPackage() {
    unload();
}

void ModelPackage::unload() {
    mlFree(mBandStart);
    mlFree(mBandLength);
    mlFree(mWeights);
    mBandStart = nullptr;
    mBandLength = nullptr;
    mWeights = nullptr;
    mFeatures = FeatureConfig();
    mData = nullptr;
    mLen = 0;
    mLabelCount = 0;
    mOutputCount = 0;
    for (auto& d : mDetection) {
        d = DetectionDefaults();
    }
    mJobId[0] = '\0';
    mName[0] = '\0';
    mCreatedUtc[0] = '\0';
    mArenaEstimate = 0;
    mMacs = 0;
    mLoaded = false;
}

bool ModelPackage::fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(mError, sizeof(mError), fmt, args);
    va_end(args);
    // Leave nothing half-initialized behind, but keep the error text
    char saved[sizeof(mError)];
    memcpy(saved, mError, sizeof(saved));
    unload();
    memcpy(mError, saved, sizeof(mError));
    return false;
}

const DetectionDefaults& ModelPackage::detectionDefaults(uint32_t i) const {
    static const DetectionDefaults none;
    return i < mLabelCount ? mDetection[i] : none;
}

bool ModelPackage::load(const uint8_t* data, size_t len) {
    unload();
    mError[0] = '\0';

    if (data == nullptr || len < 16) {
        return fail("model is empty");
    }
    // TFLM reads weights in place; the package aligns every buffer to 16 relative to the file start
    if (reinterpret_cast<uintptr_t>(data) % 16 != 0) {
        return fail("model buffer is not 16-byte aligned");
    }

    flatbuffers::Verifier verifier(data, len);
    if (!tflite::VerifyModelBuffer(verifier)) {
        return fail("not a valid TFLite model");
    }
    const tflite::Model* model = tflite::GetModel(data);
    if (model->version() != kTfliteSchemaVersion) {
        return fail("TFLite schema version %u, expected %u", static_cast<unsigned>(model->version()),
                    static_cast<unsigned>(kTfliteSchemaVersion));
    }
    if (model->subgraphs() == nullptr || model->subgraphs()->size() != 1) {
        return fail("model must have exactly one subgraph");
    }

    // Find the embedded configuration
    const char* json = nullptr;
    size_t jsonLen = 0;
    const auto* metadata = model->metadata();
    const auto* buffers = model->buffers();
    if (metadata != nullptr && buffers != nullptr) {
        for (uint32_t i = 0; i < metadata->size(); i++) {
            const tflite::Metadata* entry = metadata->Get(i);
            if (entry->name() == nullptr || strcmp(entry->name()->c_str(), kMetadataName) != 0) {
                continue;
            }
            if (entry->buffer() >= buffers->size()) {
                return fail("metadata %s points past the buffer table", kMetadataName);
            }
            const auto* bytes = buffers->Get(entry->buffer())->data();
            if (bytes == nullptr || bytes->size() == 0) {
                return fail("metadata %s is empty", kMetadataName);
            }
            json = reinterpret_cast<const char*>(bytes->data());
            jsonLen = bytes->size();
            break;
        }
    }
    if (json == nullptr) {
        return fail("no %s metadata: not an ELOC device model package", kMetadataName);
    }

    mData = data;
    mLen = len;
    if (!parseConfig(json, jsonLen)) {
        return false;  // fail() already ran
    }
    mLoaded = true;
    return true;
}

bool ModelPackage::parseConfig(const char* json, size_t jsonLen) {
    auto check = [&](JsonObjectConst root) -> bool {
        // --- format
        const char* format = root["format"] | "";
        if (strcmp(format, "eloc-model") != 0) {
            return fail("format \"%s\" is not \"eloc-model\"", format);
        }
        int32_t formatVersion = 0;
        if (!getInt(root["format_version"], formatVersion) || formatVersion != kPackageFormatVersion) {
            return fail("package format_version %d, this firmware reads %d", static_cast<int>(formatVersion),
                        kPackageFormatVersion);
        }

        // --- model
        JsonObjectConst model = root["model"];
        copyString(mJobId, sizeof(mJobId), model["job_id"] | "");
        copyString(mName, sizeof(mName), model["name"] | "");
        copyString(mCreatedUtc, sizeof(mCreatedUtc), model["created_utc"] | "");
        if (mJobId[0] == '\0') {
            return fail("model.job_id is missing");
        }
        if (mName[0] == '\0') {
            copyString(mName, sizeof(mName), mJobId);
        }

        JsonArrayConst labels = model["labels"];
        if (labels.isNull() || labels.size() < 2) {
            return fail("model.labels needs at least 2 labels");
        }
        if (labels.size() > AI_MAX_LABELS) {
            return fail("%u labels, this firmware supports up to %d", static_cast<unsigned>(labels.size()),
                        AI_MAX_LABELS);
        }
        mLabelCount = labels.size();
        for (uint32_t i = 0; i < mLabelCount; i++) {
            const char* label = labels[i].as<const char*>();
            if (label == nullptr || label[0] == '\0') {
                return fail("model.labels[%u] is empty", static_cast<unsigned>(i));
            }
            copyString(mLabels[i], kLabelLen, label);
        }
        // Index 0 is background by definition; the names are the ones the EI path also skips
        if (!equalsIgnoreCase(mLabels[0], "background") && !equalsIgnoreCase(mLabels[0], "other") &&
            !equalsIgnoreCase(mLabels[0], "others")) {
            return fail("model.labels[0] is \"%s\", expected the background class", mLabels[0]);
        }

        const char* activation = model["output"]["activation"] | "";
        if (strcmp(activation, "sigmoid") == 0) {
            mActivation = OutputActivation::Sigmoid;
        } else if (strcmp(activation, "softmax") == 0) {
            mActivation = OutputActivation::Softmax;
        } else {
            return fail("model.output.activation \"%s\" is not sigmoid or softmax", activation);
        }
        if (!getUint(model["output"]["count"], mOutputCount)) {
            return fail("model.output.count is missing");
        }
        // Same mapping as label_probabilities() in the training worker
        bool binarySigmoid = mOutputCount == 1 && mLabelCount == 2 && mActivation == OutputActivation::Sigmoid;
        if (!binarySigmoid && mOutputCount != mLabelCount) {
            return fail("%u model outputs do not match %u labels (%s)", static_cast<unsigned>(mOutputCount),
                        static_cast<unsigned>(mLabelCount), activation);
        }

        // --- audio
        FeatureConfig& f = mFeatures;
        JsonObjectConst audio = root["audio"];
        if (!getUint(audio["sample_rate"], f.sampleRate) || f.sampleRate < 1000 || f.sampleRate > 96000) {
            return fail("audio.sample_rate is missing or out of range");
        }
        if (!getUint(audio["window_samples"], f.windowSamples) || f.windowSamples == 0 ||
            f.windowSamples > kMaxWindowSamples) {
            return fail("audio.window_samples is missing or out of range");
        }

        // --- features
        JsonObjectConst features = root["features"];
        int32_t specVersion = 0;
        if (!getInt(features["spec_version"], specVersion) || specVersion != kFeatureSpecVersion) {
            return fail("features.spec_version %d, this firmware implements %d", static_cast<int>(specVersion),
                        kFeatureSpecVersion);
        }
        // Fixed by spec version 1; checked so a future change cannot be misread silently
        if (!absentOrEquals(features["input_scale"], "int16 / 32768") ||
            !absentOrEquals(features["window"], "hann_periodic") ||
            !absentOrEquals(features["layout"], "time_major")) {
            return fail("features input_scale/window/layout differ from spec version 1");
        }
        if (!getUint(features["frame_length"], f.frameLength) || !getUint(features["frame_step"], f.frameStep) ||
            !getUint(features["fft_length"], f.fftLength) || !getUint(features["n_frames"], f.nFrames) ||
            !getUint(features["n_mels"], f.nMels)) {
            return fail("features: frame_length/frame_step/fft_length/n_frames/n_mels missing");
        }
        if (!isPowerOfTwo(f.fftLength) || f.fftLength < 16 || f.fftLength > kMaxFftLength) {
            return fail("features.fft_length %u is not a power of two in 16..%u", static_cast<unsigned>(f.fftLength),
                        static_cast<unsigned>(kMaxFftLength));
        }
        if (f.frameLength < 2 || f.frameLength > f.fftLength) {
            return fail("features.frame_length %u must be 2..fft_length", static_cast<unsigned>(f.frameLength));
        }
        if (f.frameStep == 0 || f.frameLength > f.windowSamples) {
            return fail("features.frame_step is 0 or frame_length exceeds the window");
        }
        uint32_t expectedFrames = 1 + (f.windowSamples - f.frameLength) / f.frameStep;
        if (f.nFrames != expectedFrames) {
            return fail("features.n_frames %u, the window gives %u", static_cast<unsigned>(f.nFrames),
                        static_cast<unsigned>(expectedFrames));
        }
        if (f.nMels == 0 || f.nMels > kMaxMels) {
            return fail("features.n_mels %u out of range", static_cast<unsigned>(f.nMels));
        }

        const char* transform = features["transform"] | "";
        if (strcmp(transform, "log") == 0) {
            f.transform = Transform::Log;
        } else if (strcmp(transform, "linear") == 0) {
            f.transform = Transform::Linear;
        } else if (strcmp(transform, "pcen") == 0) {
            f.transform = Transform::Pcen;
        } else if (strcmp(transform, "pcen+log") == 0) {
            f.transform = Transform::PcenLog;
        } else {
            return fail("features.transform \"%s\" is unknown", transform);
        }
        if (!features["log_floor"].isNull() && (!getFloat(features["log_floor"], f.logFloor) || f.logFloor <= 0.0f)) {
            return fail("features.log_floor must be > 0");
        }
        if (f.transform == Transform::Pcen || f.transform == Transform::PcenLog) {
            JsonObjectConst pcen = features["pcen"];
            if (pcen.isNull()) {
                return fail("features.pcen is missing for transform %s", transform);
            }
            // Unset keys keep the spec defaults (PCEN_DEFAULTS in eloc_features.py)
            PcenParams& p = f.pcen;
            if ((!pcen["smoothing"].isNull() && !getFloat(pcen["smoothing"], p.smoothing)) ||
                (!pcen["gain"].isNull() && !getFloat(pcen["gain"], p.gain)) ||
                (!pcen["bias"].isNull() && !getFloat(pcen["bias"], p.bias)) ||
                (!pcen["power"].isNull() && !getFloat(pcen["power"], p.power)) ||
                (!pcen["eps"].isNull() && !getFloat(pcen["eps"], p.eps))) {
                return fail("features.pcen has a non-numeric value");
            }
            if (p.eps <= 0.0f || p.bias <= 0.0f) {
                return fail("features.pcen eps and bias must be > 0");
            }
        }
        if (!getFloat(features["normalization"]["mean"], f.mean) ||
            !getFloat(features["normalization"]["divisor"], f.divisor) || f.divisor == 0.0f) {
            return fail("features.normalization mean/divisor missing or divisor is 0");
        }

        // --- sparse mel filterbank
        JsonObjectConst mel = features["mel"];
        JsonArrayConst starts = mel["band_start"];
        JsonArrayConst lengths = mel["band_length"];
        if (starts.isNull() || lengths.isNull() || starts.size() != f.nMels || lengths.size() != f.nMels) {
            return fail("features.mel band_start/band_length must have n_mels (%u) entries",
                        static_cast<unsigned>(f.nMels));
        }
        mBandStart = mlAllocArray<uint16_t>(f.nMels, MemKind::Large);
        mBandLength = mlAllocArray<uint16_t>(f.nMels, MemKind::Large);
        if (mBandStart == nullptr || mBandLength == nullptr) {
            return fail("out of memory for the mel bands");
        }
        uint32_t totalWeights = 0;
        for (uint32_t m = 0; m < f.nMels; m++) {
            uint32_t start = 0;
            uint32_t length = 0;
            if (!getUint(starts[m], start) || !getUint(lengths[m], length)) {
                return fail("features.mel band %u is not an integer", static_cast<unsigned>(m));
            }
            if (start + length > f.nBins()) {
                return fail("features.mel band %u runs past FFT bin %u", static_cast<unsigned>(m),
                            static_cast<unsigned>(f.nBins() - 1));
            }
            mBandStart[m] = static_cast<uint16_t>(start);
            mBandLength[m] = static_cast<uint16_t>(length);
            totalWeights += length;
        }

        const char* b64 = mel["weights_f32le_b64"] | "";
        size_t b64Len = strlen(b64);
        size_t weightBytes = static_cast<size_t>(totalWeights) * sizeof(float);
        if (totalWeights > 0) {
            mWeights = mlAllocArray<float>(totalWeights, MemKind::Large);
            if (mWeights == nullptr) {
                return fail("out of memory for %u mel weights", static_cast<unsigned>(totalWeights));
            }
        }
        // Little-endian float32, which both the ESP32 and the host test are
        long decoded = base64Decode(b64, b64Len, reinterpret_cast<uint8_t*>(mWeights), weightBytes);
        if (decoded < 0 || static_cast<size_t>(decoded) != weightBytes) {
            return fail("features.mel weights: %ld bytes decoded, the bands need %u", decoded,
                        static_cast<unsigned>(weightBytes));
        }
        f.bandStart = mBandStart;
        f.bandLength = mBandLength;
        f.weights = mWeights;
        f.nWeights = totalWeights;

        // --- tensors
        JsonObjectConst input = root["tensors"]["input"];
        JsonArrayConst inShape = input["shape"];
        if (inShape.size() != 3 || inShape[0].as<uint32_t>() != 1 || inShape[1].as<uint32_t>() != f.nFrames ||
            inShape[2].as<uint32_t>() != f.nMels) {
            return fail("tensors.input.shape is not [1, %u, %u]", static_cast<unsigned>(f.nFrames),
                        static_cast<unsigned>(f.nMels));
        }
        JsonObjectConst output = root["tensors"]["output"];
        if (strcmp(input["type"] | "", "int8") != 0 || strcmp(output["type"] | "", "int8") != 0) {
            return fail("input and output tensors must be int8");
        }
        if (!getFloat(input["scale"], mInputQuant.scale) || !getInt(input["zero_point"], mInputQuant.zeroPoint) ||
            !getFloat(output["scale"], mOutputQuant.scale) || !getInt(output["zero_point"], mOutputQuant.zeroPoint) ||
            mInputQuant.scale <= 0.0f || mOutputQuant.scale <= 0.0f) {
            return fail("tensors: input/output scale or zero_point missing");
        }
        uint32_t outElements = 1;
        JsonArrayConst outShape = output["shape"];
        for (size_t i = 1; i < outShape.size(); i++) {
            outElements *= outShape[i].as<uint32_t>();
        }
        if (outShape.size() < 2 || outElements != mOutputCount) {
            return fail("tensors.output.shape does not hold model.output.count (%u) values",
                        static_cast<unsigned>(mOutputCount));
        }
        mArenaEstimate = root["tensors"]["arena_bytes_estimate"] | 0u;
        mMacs = root["tensors"]["macs"] | 0u;

        // --- inference: Phase 1 runs back-to-back windows with no score averaging
        JsonObjectConst inference = root["inference"];
        uint32_t hop = 0;
        uint32_t average = 0;
        if (!getUint(inference["hop_samples"], hop) || !getUint(inference["average_count"], average)) {
            return fail("inference.hop_samples/average_count missing");
        }
        if (hop != f.windowSamples || average != 1) {
            return fail("inference hop %u / average %u: only non-overlapping, unaveraged windows are supported",
                        static_cast<unsigned>(hop), static_cast<unsigned>(average));
        }

        // --- recommended detection settings (optional, shown only)
        JsonObjectConst detection = root["detection"];
        for (uint32_t i = 0; i < mLabelCount; i++) {
            JsonObjectConst d = detection[static_cast<const char*>(mLabels[i])];
            if (d.isNull()) {
                continue;
            }
            DetectionDefaults& out = mDetection[i];
            out.present = getFloat(d["threshold"], out.threshold) &&
                          getUint(d["observation_window_s"], out.observationWindowS) &&
                          getUint(d["required_detections"], out.requiredDetections);
        }
        return true;
    };

    // The JSON is ~6 KB, a third of it the base64 filterbank. ArduinoJson copies every string out
    // of a const input, so start at twice the text and grow if that is not enough. The document
    // lives in PSRAM and is gone when this function returns.
    size_t capacity = jsonLen * 2 + 4096;
    for (int attempt = 0; attempt < 4; attempt++, capacity *= 2) {
        JsonDoc doc(capacity);
        if (doc.capacity() == 0) {
            return fail("out of memory parsing the model config (%u bytes)", static_cast<unsigned>(capacity));
        }
        DeserializationError err = deserializeJson(doc, json, jsonLen);
        if (err == DeserializationError::NoMemory) {
            continue;
        }
        if (err) {
            return fail("model config is not valid JSON (%s)", err.c_str());
        }
        return check(doc.as<JsonObjectConst>());
    }
    return fail("model config is too large to parse");
}

}  // namespace eloc_ml
