/**
 * @file TflmClassifier.hpp
 * @brief One window of int16 audio -> per-label probabilities, on TensorFlow Lite Micro
 *
 * front-end (MelFrontend) -> quantize into the int8 input tensor -> Invoke() (ESP-NN kernels) ->
 * dequantize -> label probabilities. Every buffer is allocated in init(), none per window.
 *
 * Device only (TFLM and ESP-NN are not built for the host test).
 */

#ifndef ELOC_ML_TFLMCLASSIFIER_HPP_
#define ELOC_ML_TFLMCLASSIFIER_HPP_

#include <stddef.h>
#include <stdint.h>

#include "MelFrontend.hpp"
#include "ModelPackage.hpp"

namespace tflite {
class MicroInterpreter;
}

namespace eloc_ml {

class TflmClassifier {
 public:
    struct Timing {
        uint32_t dspUs = 0;  ///< front-end + quantization
        uint32_t nnUs = 0;   ///< Invoke()
    };

    TflmClassifier() = default;
    ~TflmClassifier();
    TflmClassifier(const TflmClassifier&) = delete;
    TflmClassifier& operator=(const TflmClassifier&) = delete;

    /**
     * @brief Build the interpreter for a loaded package
     * @param pkg must stay loaded (and its model bytes valid) while this classifier is used
     * @return false on failure; error() says why
     */
    bool init(const ModelPackage& pkg);

    void release();

    /**
     * @brief Free the front-end's per-frame working set (internal RAM) while no windows are classified
     * @note  The model, arena and features stay. classify() fails until resume().
     */
    void suspend();

    /// Re-allocate the working set released by suspend(); false when memory ran out
    bool resume();

    bool suspended() const { return mReady && !mFrontend.ready(); }

    bool ready() const { return mReady; }
    const char* error() const { return mError; }

    /**
     * @brief Classify one window
     * @param window pkg.windowSamples() int16 samples
     * @param probs  pkg.labelCount() probabilities, index 0 = background
     * @param timing optional, filled with the time spent
     * @return false when Invoke() failed
     */
    bool classify(const int16_t* window, float* probs, Timing* timing = nullptr);

    /// The last window's features (nFrames * nMels floats) and raw int8 output
    const float* lastFeatures() const { return mFeatures; }
    const int8_t* lastRawOutput() const;

    /**
     * @brief Quantization of the model input, as read from the input tensor
     * @note  There is deliberately no "last input" accessor: TFLM's memory planner reuses the input
     *        tensor's arena space for later layers, so after Invoke() it holds other activations. To
     *        see the quantized input, quantize lastFeatures() with this.
     */
    const TensorQuant& inputQuant() const { return mInputQuant; }

    size_t arenaSize() const { return mArenaSize; }
    size_t arenaUsed() const;
    /// Internal-RAM bytes of the front-end's per-frame working set
    size_t frontendBytes() const { return mFrontend.workingSetBytes(); }

 private:
    bool fail(const char* fmt, ...);
    bool buildInterpreter(size_t arenaSize);
    void destroyInterpreter();

    const ModelPackage* mPkg = nullptr;
    bool mReady = false;
    char mError[128] = "";

    MelFrontend mFrontend;
    float* mFeatures = nullptr;     ///< [nFrames * nMels], PSRAM

    uint8_t* mArena = nullptr;      ///< tensor arena, PSRAM, 16-byte aligned
    size_t mArenaSize = 0;
    // Constructed in place in mlAlloc() memory, so no TFLM header leaks into this one (it is
    // included wherever the AI runtime is)
    void* mResolver = nullptr;      ///< tflite::MicroMutableOpResolver<N>
    tflite::MicroInterpreter* mInterpreter = nullptr;

    TensorQuant mInputQuant;        ///< from the input tensor (authoritative)
    TensorQuant mOutputQuant;       ///< from the output tensor (authoritative)
};

}  // namespace eloc_ml

#endif  // ELOC_ML_TFLMCLASSIFIER_HPP_
