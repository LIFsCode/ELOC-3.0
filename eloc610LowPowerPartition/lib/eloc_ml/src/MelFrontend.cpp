/**
 * @file MelFrontend.cpp
 * @brief ELOC feature front-end, spec version 1 (see MelFrontend.hpp)
 *
 * The operation order mirrors eloc_features.py so the float32 results stay close to the reference:
 * scale then window, power as re^2 + im^2, then PCEN, log, normalization.
 */

#include "MelFrontend.hpp"

#include <math.h>
#include <string.h>

#include "MlAlloc.hpp"

namespace eloc_ml {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kInt16Scale = 1.0f / 32768.0f;  // exact: a power of two, same as dividing by 32768

/**
 * @brief One mel band: sum of power[k] * weight over its run of bins
 * @note  Kept out of line on purpose. Inlined into compute(), GCC 8.4 for Xtensa (IDF 4.4) dies
 *        with "insn does not satisfy its constraints" (an ICE in postreload) on the 0.0f start
 *        value; the Edge Impulse SDK hit the same compiler bug.
 */
__attribute__((noinline)) float bandSum(const float* power, const float* weights, uint32_t len) {
    float v = 0.0f;
    for (uint32_t i = 0; i < len; i++) {
        v += power[i] * weights[i];
    }
    return v;
}
}  // namespace

MelFrontend::~MelFrontend() {
    release();
}

void MelFrontend::release() {
    mFft.release();
    mlFree(mWindow);
    mlFree(mFftIn);
    mlFree(mFftOut);
    mlFree(mPower);
    mlFree(mPcenState);
    mWindow = nullptr;
    mFftIn = nullptr;
    mFftOut = nullptr;
    mPower = nullptr;
    mPcenState = nullptr;
    mReady = false;
}

bool MelFrontend::init(const FeatureConfig& cfg) {
    release();
    mCfg = cfg;

    const bool pcen = cfg.transform == Transform::Pcen || cfg.transform == Transform::PcenLog;
    mWindow = mlAllocArray<float>(cfg.frameLength, MemKind::Fast);
    mFftIn = mlAllocArray<float>(cfg.fftLength, MemKind::Fast);
    mFftOut = mlAllocArray<Complex>(cfg.nBins(), MemKind::Fast);
    mPower = mlAllocArray<float>(cfg.nBins(), MemKind::Fast);
    if (pcen) {
        mPcenState = mlAllocArray<float>(cfg.nMels, MemKind::Fast);
    }
    if (mWindow == nullptr || mFftIn == nullptr || mFftOut == nullptr || mPower == nullptr ||
        (pcen && mPcenState == nullptr) || !mFft.init(cfg.fftLength)) {
        release();
        return false;
    }

    // Periodic Hann, computed in double and rounded once, as NumPy does
    for (uint32_t n = 0; n < cfg.frameLength; n++) {
        mWindow[n] = static_cast<float>(0.5 - 0.5 * cos(2.0 * kPi * n / cfg.frameLength));
    }
    // The zero padding is never overwritten: compute() only writes the first frameLength values
    memset(mFftIn, 0, cfg.fftLength * sizeof(float));

    mReady = true;
    return true;
}

size_t MelFrontend::workingSetBytes() const {
    size_t bytes = mFft.planBytes();
    bytes += mCfg.frameLength * sizeof(float);
    bytes += mCfg.fftLength * sizeof(float);
    bytes += mCfg.nBins() * (sizeof(Complex) + sizeof(float));
    if (mPcenState != nullptr) {
        bytes += mCfg.nMels * sizeof(float);
    }
    return bytes;
}

void MelFrontend::compute(const int16_t* window, float* features) {
    const FeatureConfig& c = mCfg;
    const uint32_t nBins = c.nBins();

    const bool pcen = mPcenState != nullptr;
    const bool log = c.transform == Transform::Log || c.transform == Transform::PcenLog;
    const float oneMinusS = 1.0f - c.pcen.smoothing;
    const float biasPow = powf(c.pcen.bias, c.pcen.power);
    if (pcen) {
        // Reset at the start of every window
        memset(mPcenState, 0, c.nMels * sizeof(float));
    }

    for (uint32_t t = 0; t < c.nFrames; t++) {
        // 1-3: scale, frame, window
        const int16_t* frame = window + t * c.frameStep;
        for (uint32_t n = 0; n < c.frameLength; n++) {
            mFftIn[n] = (static_cast<float>(frame[n]) * kInt16Scale) * mWindow[n];
        }

        // 4: zero-padded real FFT, power spectrum
        mFft.forward(mFftIn, mFftOut);
        for (uint32_t k = 0; k < nBins; k++) {
            mPower[k] = mFftOut[k].re * mFftOut[k].re + mFftOut[k].im * mFftOut[k].im;
        }

        // 5: sparse mel filterbank, then 6-7 per value
        float* out = features + t * c.nMels;
        const float* w = c.weights;
        for (uint32_t m = 0; m < c.nMels; m++) {
            const uint32_t len = c.bandLength[m];
            float v = bandSum(mPower + c.bandStart[m], w, len);
            w += len;

            if (pcen) {
                // M[t] = s*M[t-1] + (1-s)*mel[t];  (mel / (eps + M)^gain + bias)^power - bias^power
                float& state = mPcenState[m];
                state = c.pcen.smoothing * state + oneMinusS * v;
                const float denominator = expf(c.pcen.gain * logf(c.pcen.eps + state));
                v = expf(c.pcen.power * logf(v / denominator + c.pcen.bias)) - biasPow;
            }
            if (log) {
                v = logf(fmaxf(v, c.logFloor));
            }
            out[m] = (v - c.mean) / c.divisor;
        }
    }
}

void MelFrontend::quantize(const float* features, size_t count, const TensorQuant& quant, int8_t* out) {
    for (size_t i = 0; i < count; i++) {
        // rintf rounds half to even in the default rounding mode, like np.round
        float q = rintf(features[i] / quant.scale) + static_cast<float>(quant.zeroPoint);
        if (!(q >= -128.0f)) {  // also catches NaN, whose int8 conversion would be undefined
            q = -128.0f;
        } else if (q > 127.0f) {
            q = 127.0f;
        }
        out[i] = static_cast<int8_t>(q);
    }
}

}  // namespace eloc_ml
