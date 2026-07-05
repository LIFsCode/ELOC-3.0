/*
 * Strong overrides of the weak Edge Impulse porting allocators
 * (edge-impulse-sdk/porting/espressif/ei_classifier_porting.cpp).
 *
 * The stock espressif port uses plain malloc(), which with
 * CONFIG_SPIRAM_USE_CAPS_ALLOC never touches PSRAM. When BT + LoRa + AI run
 * concurrently the internal heap is borderline and the MFE (DSP) stage fails
 * its transient allocations with EIDSP_OUT_OF_MEM (-1002) -> run_classifier -5.
 *
 * These overrides keep the fast path identical (internal DRAM first) and fall
 * back to PSRAM instead of failing, so a tight heap costs one slower inference
 * rather than a dropped one.
 *
 * MUST live in src/, not lib/edge-impulse/: lib sources are linked as a static
 * archive, and the linker resolves ei_malloc from the SDK's weak definition in
 * the same archive without ever extracting an override member (verified with
 * xtensa-esp32-elf-nm: symbols stayed 'W'). Objects from src/ are always on
 * the link line, so the strong definitions here reliably win.
 */

#ifdef EDGE_IMPULSE_ENABLED

#include <string.h>
#include "esp_heap_caps.h"
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"

// Allocations at or above this size go to PSRAM first. Internal-first for
// everything starved BT during detection: the MFE matrices (~16-35 KB/cycle)
// left <4 KB contiguous internal heap and app connections failed again with
// "SDP - no buf for search rsp". Large DSP buffers run fine from PSRAM
// (measured 758-815 ms DSP vs ~610-730 ms, within the 1 s budget); small hot
// per-frame buffers (FFT, ~1-2 KB) stay internal for speed.
static constexpr size_t PSRAM_FIRST_THRESHOLD = 8192;

void *ei_malloc(size_t size) {
    if (size >= PSRAM_FIRST_THRESHOLD) {
        return heap_caps_malloc_prefer(size, 2,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return heap_caps_malloc_prefer(size, 2,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void *ei_calloc(size_t nitems, size_t size) {
    if (nitems * size >= PSRAM_FIRST_THRESHOLD) {
        return heap_caps_calloc_prefer(nitems, size, 2,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return heap_caps_calloc_prefer(nitems, size, 2,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void ei_free(void *ptr) {
    // free() routes to the owning heap (internal or PSRAM) in ESP-IDF
    free(ptr);
}

#endif  // EDGE_IMPULSE_ENABLED
