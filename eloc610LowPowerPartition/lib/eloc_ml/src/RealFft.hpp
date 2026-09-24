/**
 * @file RealFft.hpp
 * @brief Real-input FFT used by the mel front-end
 *
 * The only place that knows which FFT library runs. It is KissFFT (BSD-3-Clause, vendored from
 * upstream mborgerding/kissfft in third_party/kissfft, not from the Edge Impulse SDK). Swapping in
 * ESP-DSP's assembly FFT later would touch this file alone.
 */

#ifndef ELOC_ML_REALFFT_HPP_
#define ELOC_ML_REALFFT_HPP_

#include <stddef.h>
#include <stdint.h>

namespace eloc_ml {

struct Complex {
    float re;
    float im;
};

class RealFft {
 public:
    RealFft() = default;
    ~RealFft();
    RealFft(const RealFft&) = delete;
    RealFft& operator=(const RealFft&) = delete;

    /**
     * @brief Build the plan for an @p n point transform (n even), allocated once
     * @return false when @p n is unsupported or memory ran out
     */
    bool init(uint32_t n);

    void release();

    /**
     * @brief Forward transform
     * @param in  n real samples (not modified)
     * @param out n/2 + 1 complex bins, DC first
     */
    void forward(const float* in, Complex* out) const;

    uint32_t size() const { return mN; }

    /// Bytes the plan occupies (for memory logging)
    size_t planBytes() const { return mPlanBytes; }

 private:
    void* mPlanMem = nullptr;
    void* mPlan = nullptr;
    uint32_t mN = 0;
    size_t mPlanBytes = 0;
};

}  // namespace eloc_ml

#endif  // ELOC_ML_REALFFT_HPP_
