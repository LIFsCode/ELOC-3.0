/**
 * @file MlAlloc.cpp
 * @brief Memory placement for the ELOC ML runtime (see MlAlloc.hpp)
 *
 * Alignment is done here, by over-allocating and keeping the original pointer just in front of the
 * aligned block, rather than with heap_caps_aligned_alloc(). The IDF heap runs from IRAM, and its
 * aligned path would add ~1.5 KB of IRAM code; this build has only a few hundred bytes of IRAM left.
 */

#include "MlAlloc.hpp"

#include <stdint.h>

#if defined(ESP_PLATFORM)
    #include "esp_heap_caps.h"
#else
    #include <stdlib.h>
#endif

/**
 * A Fast allocation only takes internal RAM when at least this much stays free afterwards. The rest
 * of the firmware lives on it at run time: the SD driver's 512 B DMA bounce buffer for every sector
 * (the FATFS buffers are in PSRAM), Bluetooth, logging, JSON, and the AI task's stack, which is
 * created after the front-end. A 2 s / FFT 1024 model took all of it but ~1 KB, and every SD access
 * then failed with ESP_ERR_NO_MEM.
 */
#ifndef ELOC_ML_INTERNAL_RESERVE
    #define ELOC_ML_INTERNAL_RESERVE (24 * 1024)
#endif

namespace eloc_ml {

namespace {

void* rawAlloc(size_t bytes, MemKind kind) {
#if defined(ESP_PLATFORM)
    if (kind == MemKind::Fast &&
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= bytes + ELOC_ML_INTERNAL_RESERVE) {
        void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (ptr != nullptr) {
            return ptr;
        }
    }
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    (void)kind;
    return malloc(bytes);
#endif
}

void rawFree(void* ptr) {
#if defined(ESP_PLATFORM)
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

}  // namespace

void* mlAlloc(size_t bytes, MemKind kind, size_t align) {
    if (bytes == 0 || align == 0 || (align & (align - 1)) != 0) {
        return nullptr;
    }
    if (align < sizeof(void*)) {
        align = sizeof(void*);
    }
    void* raw = rawAlloc(bytes + align - 1 + sizeof(void*), kind);
    if (raw == nullptr) {
        return nullptr;
    }
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw) + sizeof(void*) + align - 1) & ~(uintptr_t)(align - 1);
    reinterpret_cast<void**>(aligned)[-1] = raw;
    return reinterpret_cast<void*>(aligned);
}

void mlFree(void* ptr) {
    if (ptr == nullptr) {
        return;
    }
    rawFree(reinterpret_cast<void**>(ptr)[-1]);
}

}  // namespace eloc_ml
