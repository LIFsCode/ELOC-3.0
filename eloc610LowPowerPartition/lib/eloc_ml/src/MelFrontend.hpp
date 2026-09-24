/**
 * @file MelFrontend.hpp
 * @brief ELOC feature front-end, spec version 1, in C++
 *
 * Implements exactly "Feature front-end, spec version 1" of ELOC_management/DEVICE_MODEL_PACKAGE.md
 * (executable reference: ie/cloud_training/eloc_features.py, FeatureSpec.compute), float32
 * throughout. Every buffer is allocated in init(); compute() never allocates.
 *
 * Keep ESP/Arduino headers out of this file: it also builds on the host for the native golden-vector
 * test (test/test_generic_mel_frontend).
 */

#ifndef ELOC_ML_MELFRONTEND_HPP_
#define ELOC_ML_MELFRONTEND_HPP_

#include <stddef.h>
#include <stdint.h>

#include "ModelPackage.hpp"
#include "RealFft.hpp"

namespace eloc_ml {

class MelFrontend {
 public:
    MelFrontend() = default;
    ~MelFrontend();
    MelFrontend(const MelFrontend&) = delete;
    MelFrontend& operator=(const MelFrontend&) = delete;

    /**
     * @brief Precompute the window table and FFT plan and allocate the per-frame buffers
     * @param cfg the package's feature config; its band/weight arrays must outlive this object
     * @return false when memory ran out
     */
    bool init(const FeatureConfig& cfg);

    void release();

    bool ready() const { return mReady; }

    /**
     * @brief Features of one window
     * @param window   cfg.windowSamples int16 samples
     * @param features cfg.nFrames * cfg.nMels floats, written time-major ([t * nMels + m])
     */
    void compute(const int16_t* window, float* features);

    /**
     * @brief Quantize features for the int8 model input: clamp(round(v / scale) + zero_point)
     * @note  Rounds half to even like NumPy's np.round, so the result matches the package's golden
     *        inputs exactly rather than only within one step.
     */
    static void quantize(const float* features, size_t count, const TensorQuant& quant, int8_t* out);

    /// Bytes of internal-RAM working set (window table, FFT plan and buffers, PCEN state)
    size_t workingSetBytes() const;

 private:
    FeatureConfig mCfg;
    bool mReady = false;

    RealFft mFft;
    float* mWindow = nullptr;      ///< [frameLength] periodic Hann
    float* mFftIn = nullptr;       ///< [fftLength] windowed frame, zero padded
    Complex* mFftOut = nullptr;    ///< [nBins]
    float* mPower = nullptr;       ///< [nBins]
    float* mPcenState = nullptr;   ///< [nMels] PCEN smoother, only for pcen transforms
};

}  // namespace eloc_ml

#endif  // ELOC_ML_MELFRONTEND_HPP_
