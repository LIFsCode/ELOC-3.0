/**
 * @file MlAlloc.hpp
 * @brief Memory placement for the ELOC ML runtime
 *
 * The front-end and the model parser also compile on the host for the native test, so they must not
 * include ESP headers. They allocate through these two functions instead; only MlAlloc.cpp knows
 * about heap_caps.
 */

#ifndef ELOC_ML_MLALLOC_HPP_
#define ELOC_ML_MLALLOC_HPP_

#include <stddef.h>

namespace eloc_ml {

enum class MemKind {
    /// Big buffers touched once per window (audio, tensor arena, feature matrix, mel weights).
    /// PSRAM on the device; never falls back to internal RAM.
    Large,
    /// Small buffers touched for every frame (FFT plan and buffers, window table). Internal RAM on
    /// the device, PSRAM if internal RAM is exhausted.
    Fast,
};

/**
 * @brief Allocate @p bytes aligned to @p align (a power of two, at least sizeof(void*))
 * @return nullptr on failure
 */
void* mlAlloc(size_t bytes, MemKind kind, size_t align = 16);

/// Free memory from mlAlloc(). nullptr is ignored.
void mlFree(void* ptr);

/// Typed convenience wrapper around mlAlloc()
template <typename T>
T* mlAllocArray(size_t count, MemKind kind) {
    return static_cast<T*>(mlAlloc(count * sizeof(T), kind));
}

}  // namespace eloc_ml

#endif  // ELOC_ML_MLALLOC_HPP_
