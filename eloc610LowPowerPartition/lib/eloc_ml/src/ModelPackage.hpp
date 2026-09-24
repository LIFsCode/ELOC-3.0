/**
 * @file ModelPackage.hpp
 * @brief Parse and validate an ELOC device model package (format version 1)
 *
 * The package is one .tflite file from ELOC Model Training with its whole configuration embedded as
 * TFLite metadata "eloc_model_config" (UTF-8 JSON). The format contract is
 * ELOC_management/DEVICE_MODEL_PACKAGE.md.
 *
 * load() takes any memory buffer, so the compiled-in model and a later SD-card/app-pushed model
 * (Phase 2) go through the same checks. Keep ESP/Arduino headers out of this file: it also builds
 * on the host for the native test (test/test_generic_mel_frontend).
 */

#ifndef ELOC_ML_MODELPACKAGE_HPP_
#define ELOC_ML_MODELPACKAGE_HPP_

#include <stddef.h>
#include <stdint.h>
#include "../../../include/project_config.h"  // AI_MAX_LABELS

namespace eloc_ml {

/// Package format and feature spec versions this firmware understands
constexpr int kPackageFormatVersion = 1;
constexpr int kFeatureSpecVersion = 1;

/// Name of the TFLite metadata entry holding the configuration JSON
constexpr const char* kMetadataName = "eloc_model_config";

constexpr size_t kLabelLen = 32;    ///< including the terminating NUL
constexpr size_t kIdLen = 48;
constexpr size_t kNameLen = 64;
constexpr size_t kDateLen = 32;

/// Step 6 of the feature spec
enum class Transform : uint8_t { Linear, Log, Pcen, PcenLog };

/// How the model's outputs map to labels
enum class OutputActivation : uint8_t {
    Sigmoid,  ///< one output = p(labels[1]); labels[0] (background) gets 1 - p
    Softmax,  ///< output[i] = p(labels[i])
};

struct TensorQuant {
    float scale = 1.0f;
    int32_t zeroPoint = 0;
};

struct PcenParams {
    float smoothing = 0.98f;
    float gain = 0.98f;
    float bias = 2.0f;
    float power = 0.5f;
    float eps = 1e-6f;
};

/**
 * @brief Everything the mel front-end needs (DEVICE_MODEL_PACKAGE.md, "Feature front-end, spec 1")
 * @note  The band and weight arrays are owned by the ModelPackage and live as long as it does.
 */
struct FeatureConfig {
    uint32_t sampleRate = 0;
    uint32_t windowSamples = 0;
    uint32_t frameLength = 0;
    uint32_t frameStep = 0;
    uint32_t fftLength = 0;
    uint32_t nFrames = 0;
    uint32_t nMels = 0;
    Transform transform = Transform::Log;
    float logFloor = 1e-6f;
    PcenParams pcen;
    float mean = 0.0f;
    float divisor = 1.0f;

    const uint16_t* bandStart = nullptr;   ///< [nMels] first FFT bin of each band
    const uint16_t* bandLength = nullptr;  ///< [nMels] number of bins (may be 0)
    const float* weights = nullptr;        ///< all bands' weights, concatenated in band order
    uint32_t nWeights = 0;

    uint32_t nBins() const { return fftLength / 2 + 1; }
    uint32_t nFeatures() const { return nFrames * nMels; }
};

/**
 * @brief The model's recommended detection settings for one label ("detection" in the config)
 * @note  Shown in status only, never applied: the user's own inference config always wins.
 */
struct DetectionDefaults {
    bool present = false;
    float threshold = 0.0f;           ///< 0..1 (the app sends threshold * 100)
    uint32_t observationWindowS = 0;
    uint32_t requiredDetections = 0;
};

class ModelPackage {
 public:
    ModelPackage() = default;
    ~ModelPackage();
    ModelPackage(const ModelPackage&) = delete;
    ModelPackage& operator=(const ModelPackage&) = delete;

    /**
     * @brief Parse and validate a package
     * @param data the .tflite bytes. Must be 16-byte aligned and stay valid (unchanged) for as long
     *             as this package or an interpreter built from it is in use; it is not copied.
     * @param len  size of @p data in bytes
     * @return true when the package is usable; otherwise error() says why
     */
    bool load(const uint8_t* data, size_t len);

    /// Release everything load() allocated
    void unload();

    bool loaded() const { return mLoaded; }

    /// Readable reason of the last failed load(), "" after a successful one
    const char* error() const { return mError; }

    const uint8_t* modelData() const { return mData; }
    size_t modelSize() const { return mLen; }

    const FeatureConfig& features() const { return mFeatures; }
    const TensorQuant& inputQuant() const { return mInputQuant; }
    const TensorQuant& outputQuant() const { return mOutputQuant; }

    OutputActivation activation() const { return mActivation; }
    uint32_t outputCount() const { return mOutputCount; }

    uint32_t labelCount() const { return mLabelCount; }
    const char* label(uint32_t i) const { return i < mLabelCount ? mLabels[i] : ""; }
    const DetectionDefaults& detectionDefaults(uint32_t i) const;

    const char* jobId() const { return mJobId; }
    const char* name() const { return mName; }
    const char* createdUtc() const { return mCreatedUtc; }

    uint32_t sampleRate() const { return mFeatures.sampleRate; }
    uint32_t windowSamples() const { return mFeatures.windowSamples; }
    uint32_t arenaEstimate() const { return mArenaEstimate; }
    uint32_t macs() const { return mMacs; }

 private:
    bool fail(const char* fmt, ...);
    bool parseConfig(const char* json, size_t len);

    bool mLoaded = false;
    char mError[128] = "";

    const uint8_t* mData = nullptr;
    size_t mLen = 0;

    FeatureConfig mFeatures;
    uint16_t* mBandStart = nullptr;
    uint16_t* mBandLength = nullptr;
    float* mWeights = nullptr;

    TensorQuant mInputQuant;
    TensorQuant mOutputQuant;
    OutputActivation mActivation = OutputActivation::Sigmoid;
    uint32_t mOutputCount = 0;

    uint32_t mLabelCount = 0;
    char mLabels[AI_MAX_LABELS][kLabelLen] = {};
    DetectionDefaults mDetection[AI_MAX_LABELS];

    char mJobId[kIdLen] = "";
    char mName[kNameLen] = "";
    char mCreatedUtc[kDateLen] = "";
    uint32_t mArenaEstimate = 0;
    uint32_t mMacs = 0;
};

}  // namespace eloc_ml

#endif  // ELOC_ML_MODELPACKAGE_HPP_
