/**
 * @file TflmClassifier.cpp
 * @brief TensorFlow Lite Micro classifier (see TflmClassifier.hpp)
 */

// TFLM and ESP-NN are built for the device only; the native test env compiles this library too
#if defined(ESP_PLATFORM)

#include "TflmClassifier.hpp"

#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "MlAlloc.hpp"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace eloc_ml {

namespace {

const char* TAG = "TflmClassifier";

/**
 * The operators this firmware runs: exactly SUPPORTED_OPS in ie/cloud_training/device_package.py.
 * This is a two-sided contract: the training worker marks any model using another operator as not
 * firmware-ready. Add an operator in both places or in neither.
 */
constexpr int kOpCount = 22;
using OpResolver = tflite::MicroMutableOpResolver<kOpCount>;

bool registerOps(OpResolver& r) {
    return r.AddAdd() == kTfLiteOk && r.AddAveragePool2D() == kTfLiteOk && r.AddBatchToSpaceNd() == kTfLiteOk &&
           r.AddConcatenation() == kTfLiteOk && r.AddConv2D() == kTfLiteOk && r.AddDepthwiseConv2D() == kTfLiteOk &&
           r.AddDequantize() == kTfLiteOk && r.AddExpandDims() == kTfLiteOk && r.AddFullyConnected() == kTfLiteOk &&
           r.AddLogistic() == kTfLiteOk && r.AddMaxPool2D() == kTfLiteOk && r.AddMean() == kTfLiteOk &&
           r.AddMul() == kTfLiteOk && r.AddPad() == kTfLiteOk && r.AddQuantize() == kTfLiteOk &&
           r.AddRelu() == kTfLiteOk && r.AddRelu6() == kTfLiteOk && r.AddReshape() == kTfLiteOk &&
           r.AddSoftmax() == kTfLiteOk && r.AddSpaceToBatchNd() == kTfLiteOk && r.AddSqueeze() == kTfLiteOk &&
           r.AddStridedSlice() == kTfLiteOk;
}

/// Floor for the first arena attempt; the host-side estimate is only a starting point
constexpr size_t kMinArenaBytes = 64 * 1024;

bool sameScale(float a, float b) {
    return fabsf(a - b) <= 1e-6f * fabsf(b);
}

}  // namespace

TflmClassifier::~TflmClassifier() {
    release();
}

bool TflmClassifier::fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(mError, sizeof(mError), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s", mError);
    char saved[sizeof(mError)];
    memcpy(saved, mError, sizeof(saved));
    release();
    memcpy(mError, saved, sizeof(mError));
    return false;
}

void TflmClassifier::destroyInterpreter() {
    if (mInterpreter != nullptr) {
        mInterpreter->~MicroInterpreter();
        mlFree(mInterpreter);
        mInterpreter = nullptr;
    }
    mlFree(mArena);
    mArena = nullptr;
    mArenaSize = 0;
}

void TflmClassifier::release() {
    destroyInterpreter();
    if (mResolver != nullptr) {
        static_cast<OpResolver*>(mResolver)->~OpResolver();
        mlFree(mResolver);
        mResolver = nullptr;
    }
    mlFree(mFeatures);
    mFeatures = nullptr;
    mFrontend.release();
    mPkg = nullptr;
    mReady = false;
    mError[0] = '\0';
}

bool TflmClassifier::buildInterpreter(size_t arenaSize) {
    destroyInterpreter();
    arenaSize = (arenaSize + 15) & ~static_cast<size_t>(15);
    mArena = static_cast<uint8_t*>(mlAlloc(arenaSize, MemKind::Large, 16));
    void* mem = mlAlloc(sizeof(tflite::MicroInterpreter), MemKind::Large, alignof(tflite::MicroInterpreter));
    if (mArena == nullptr || mem == nullptr) {
        mlFree(mem);
        destroyInterpreter();
        ESP_LOGE(TAG, "Could not allocate a %u byte tensor arena in PSRAM", static_cast<unsigned>(arenaSize));
        return false;
    }
    mArenaSize = arenaSize;
    mInterpreter = new (mem) tflite::MicroInterpreter(tflite::GetModel(mPkg->modelData()),
                                                      *static_cast<OpResolver*>(mResolver), mArena, arenaSize);
    if (mInterpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGW(TAG, "AllocateTensors() failed with a %u byte arena", static_cast<unsigned>(arenaSize));
        destroyInterpreter();
        return false;
    }
    return true;
}

bool TflmClassifier::init(const ModelPackage& pkg) {
    release();
    if (!pkg.loaded()) {
        return fail("model package is not loaded");
    }
    mPkg = &pkg;
    const FeatureConfig& f = pkg.features();

    if (!mFrontend.init(f)) {
        return fail("out of memory for the front-end buffers");
    }
    mFeatures = mlAllocArray<float>(f.nFeatures(), MemKind::Large);
    if (mFeatures == nullptr) {
        return fail("out of memory for the %u feature values", static_cast<unsigned>(f.nFeatures()));
    }

    void* mem = mlAlloc(sizeof(OpResolver), MemKind::Large, alignof(OpResolver));
    if (mem == nullptr) {
        return fail("out of memory for the op resolver");
    }
    mResolver = new (mem) OpResolver();
    if (!registerOps(*static_cast<OpResolver*>(mResolver))) {
        return fail("registering the operators failed");
    }

    // Start at the host estimate plus 25 %, at least 64 KB. If the real plan needs more, retry
    // once at twice the size before giving up.
    size_t arena = pkg.arenaEstimate() + pkg.arenaEstimate() / 4;
    if (arena < kMinArenaBytes) {
        arena = kMinArenaBytes;
    }
    if (!buildInterpreter(arena) && !buildInterpreter(arena * 2)) {
        return fail("AllocateTensors() failed with a %u byte arena (model uses an unsupported op, or too big)",
                    static_cast<unsigned>(arena * 2));
    }

    // The package config was validated on its own; now check the graph agrees with it
    if (mInterpreter->inputs_size() != 1 || mInterpreter->outputs_size() != 1) {
        return fail("model must have exactly one input and one output tensor");
    }
    const TfLiteTensor* in = mInterpreter->input(0);
    const TfLiteTensor* out = mInterpreter->output(0);
    if (in->type != kTfLiteInt8 || out->type != kTfLiteInt8) {
        return fail("model input and output must be int8");
    }
    if (in->dims->size != 3 || in->dims->data[0] != 1 || in->dims->data[1] != static_cast<int>(f.nFrames) ||
        in->dims->data[2] != static_cast<int>(f.nMels) || in->bytes != f.nFeatures()) {
        return fail("model input tensor is not [1, %u, %u]", static_cast<unsigned>(f.nFrames),
                    static_cast<unsigned>(f.nMels));
    }
    size_t outElements = 1;
    for (int i = 0; i < out->dims->size; i++) {
        outElements *= static_cast<size_t>(out->dims->data[i]);
    }
    if (outElements != pkg.outputCount()) {
        return fail("model has %u outputs, its config says %u", static_cast<unsigned>(outElements),
                    static_cast<unsigned>(pkg.outputCount()));
    }
    mInputQuant.scale = in->params.scale;
    mInputQuant.zeroPoint = in->params.zero_point;
    mOutputQuant.scale = out->params.scale;
    mOutputQuant.zeroPoint = out->params.zero_point;
    if (!sameScale(mInputQuant.scale, pkg.inputQuant().scale) ||
        mInputQuant.zeroPoint != pkg.inputQuant().zeroPoint ||
        !sameScale(mOutputQuant.scale, pkg.outputQuant().scale) ||
        mOutputQuant.zeroPoint != pkg.outputQuant().zeroPoint) {
        return fail("tensor quantization differs from the package config");
    }

    mReady = true;
    return true;
}

void TflmClassifier::suspend() {
    mFrontend.release();
}

bool TflmClassifier::resume() {
    if (!mReady) {
        return false;
    }
    return mFrontend.ready() || mFrontend.init(mPkg->features());
}

size_t TflmClassifier::arenaUsed() const {
    return mInterpreter != nullptr ? mInterpreter->arena_used_bytes() : 0;
}

const int8_t* TflmClassifier::lastInput() const {
    return mInterpreter != nullptr ? mInterpreter->input(0)->data.int8 : nullptr;
}

const int8_t* TflmClassifier::lastRawOutput() const {
    return mInterpreter != nullptr ? mInterpreter->output(0)->data.int8 : nullptr;
}

bool TflmClassifier::classify(const int16_t* window, float* probs, Timing* timing) {
    if (!mReady || !mFrontend.ready()) {
        return false;
    }
    const FeatureConfig& f = mPkg->features();

    int64_t t0 = esp_timer_get_time();
    mFrontend.compute(window, mFeatures);
    MelFrontend::quantize(mFeatures, f.nFeatures(), mInputQuant, mInterpreter->input(0)->data.int8);
    int64_t t1 = esp_timer_get_time();
    TfLiteStatus status = mInterpreter->Invoke();
    int64_t t2 = esp_timer_get_time();
    if (timing != nullptr) {
        timing->dspUs = static_cast<uint32_t>(t1 - t0);
        timing->nnUs = static_cast<uint32_t>(t2 - t1);
    }
    if (status != kTfLiteOk) {
        return false;
    }

    // (q - zero_point) * scale, then map to labels like label_probabilities() in the worker
    const int8_t* raw = mInterpreter->output(0)->data.int8;
    const uint32_t outputs = mPkg->outputCount();
    auto dequantize = [&](uint32_t i) {
        return (static_cast<float>(raw[i]) - static_cast<float>(mOutputQuant.zeroPoint)) * mOutputQuant.scale;
    };
    if (outputs == 1 && mPkg->labelCount() == 2) {
        float p = dequantize(0);
        probs[0] = 1.0f - p;
        probs[1] = p;
    } else {
        for (uint32_t i = 0; i < outputs; i++) {
            probs[i] = dequantize(i);
        }
    }
    return true;
}

}  // namespace eloc_ml

#endif  // ESP_PLATFORM
