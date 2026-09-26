/**
 * @file RealFft.cpp
 * @brief KissFFT implementation of RealFft
 */

#include "RealFft.hpp"

#include "MlAlloc.hpp"
#include "../third_party/kissfft/kiss_fftr.h"

namespace eloc_ml {

static_assert(sizeof(Complex) == sizeof(kiss_fft_cpx), "Complex must match kiss_fft_cpx");
static_assert(sizeof(kiss_fft_scalar) == sizeof(float), "KissFFT must be built for float");

RealFft::~RealFft() {
    release();
}

bool RealFft::init(uint32_t n) {
    release();
    if (n < 4 || (n % 2) != 0) {
        return false;
    }
    // Ask for the plan size first, then build the plan in our own (MemKind::Fast) block so the
    // transform itself never allocates
    size_t bytes = 0;
    kiss_fftr_alloc(static_cast<int>(n), 0, nullptr, &bytes);
    if (bytes == 0) {
        return false;
    }
    mPlanMem = mlAlloc(bytes, MemKind::Fast);
    if (mPlanMem == nullptr) {
        return false;
    }
    mPlan = kiss_fftr_alloc(static_cast<int>(n), 0, mPlanMem, &bytes);
    if (mPlan == nullptr) {
        release();
        return false;
    }
    mN = n;
    mPlanBytes = bytes;
    return true;
}

void RealFft::release() {
    mlFree(mPlanMem);
    mPlanMem = nullptr;
    mPlan = nullptr;
    mN = 0;
    mPlanBytes = 0;
}

void RealFft::forward(const float* in, Complex* out) const {
    kiss_fftr(static_cast<kiss_fftr_cfg>(mPlan), in, reinterpret_cast<kiss_fft_cpx*>(out));
}

}  // namespace eloc_ml
