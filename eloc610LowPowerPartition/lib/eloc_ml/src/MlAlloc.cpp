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

namespace eloc_ml {

namespace {

void* rawAlloc(size_t bytes, MemKind kind) {
#if defined(ESP_PLATFORM)
    if (kind == MemKind::Large) {
        return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return ptr;
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
