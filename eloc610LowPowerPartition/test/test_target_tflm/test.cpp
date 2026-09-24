/*
 * On-target test of the TFLite Micro runtime (esp32dev-tflm): the compiled-in device model package,
 * run through the same TflmClassifier the firmware uses.
 *
 * Needs the package's golden vectors on the SD card root:
 *   copy test/fixtures/eloc_golden_vectors.bin  ->  <SD card>/eloc_golden_vectors.bin
 * Run with:
 *   pio test -e target_tflm_tests
 *
 * Tolerances (DEVICE_MODEL_PACKAGE.md): quantized input within +-1, raw int8 output within +-2
 * (ESP-NN may differ from TFLite's reference kernels by one step). Logs DSP and NN time, the arena
 * the model really uses and the heap before and after, which is what the plan's T3 measures.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "esp32/clk.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ESP32Time.h"
#include "SDCardSDIO.h"

#include "CompiledModel.hpp"
#include "GoldenVectors.hpp"
#include "MlAlloc.hpp"
#include "ModelPackage.hpp"
#include "TflmClassifier.hpp"

using namespace eloc_ml;

ESP32Time timeObject;
SDCardSDIO sd_card;

static const char* kGoldenPath = "/sdcard/eloc_golden_vectors.bin";

static ModelPackage gPackage;
static TflmClassifier gClassifier;

extern "C" {
void app_main(void);
}

void setUp(void) {}

void tearDown(void) {}

static void logHeap(const char* when) {
    printf("heap %-24s internal free %6u (largest %6u), PSRAM free %7u\n", when,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

/// The firmware pins the CPU at 240 MHz while AI runs (ElocSystem PmProfile); time it the same way
void test_cpu_240mhz() {
    esp_pm_config_esp32_t pm = {};
    pm.max_freq_mhz = 240;
    pm.min_freq_mhz = 240;
    pm.light_sleep_enable = false;
    TEST_ASSERT_EQUAL(ESP_OK, esp_pm_configure(&pm));
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("CPU clock %d MHz\n", esp_clk_cpu_freq() / 1000000);
    TEST_ASSERT_EQUAL(240, esp_clk_cpu_freq() / 1000000);
}

void test_setup_sd() {
    for (int tries = 0; !sd_card.isMounted() && tries < 5; tries++) {
        sd_card.init("/sdcard");
        if (!sd_card.isMounted()) {
            printf("SD card not mounted, retrying (remove the jumper from the SD card?)\n");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    TEST_ASSERT_TRUE(sd_card.isMounted());
}

void test_load_package() {
    logHeap("before package load");
    int64_t t0 = esp_timer_get_time();
    bool ok = gPackage.load(compiledModelData(), compiledModelSize());
    int64_t t1 = esp_timer_get_time();
    TEST_ASSERT_TRUE_MESSAGE(ok, gPackage.error());
    printf("model \"%s\" job %s created %s, %u labels, %u Hz, window %u, load took %lld ms\n", gPackage.name(),
           gPackage.jobId(), gPackage.createdUtc(), static_cast<unsigned>(gPackage.labelCount()),
           static_cast<unsigned>(gPackage.sampleRate()), static_cast<unsigned>(gPackage.windowSamples()),
           (t1 - t0) / 1000);
    logHeap("after package load");
}

void test_classifier_init() {
    TEST_ASSERT_TRUE(gPackage.loaded());
    bool ok = gClassifier.init(gPackage);
    TEST_ASSERT_TRUE_MESSAGE(ok, gClassifier.error());
    printf("arena: estimate %u, allocated %u, arena_used_bytes() %u; front-end working set %u bytes\n",
           static_cast<unsigned>(gPackage.arenaEstimate()), static_cast<unsigned>(gClassifier.arenaSize()),
           static_cast<unsigned>(gClassifier.arenaUsed()), static_cast<unsigned>(gClassifier.frontendBytes()));
    logHeap("after classifier init");
}

/// T0 check: the whole chain runs and the output is sane on digital silence
void test_invoke_zeros() {
    TEST_ASSERT_TRUE(gClassifier.ready());
    int16_t* zeros = mlAllocArray<int16_t>(gPackage.windowSamples(), MemKind::Large);
    TEST_ASSERT_NOT_NULL(zeros);
    memset(zeros, 0, gPackage.windowSamples() * sizeof(int16_t));
    float probs[AI_MAX_LABELS];
    TflmClassifier::Timing timing;
    TEST_ASSERT_TRUE(gClassifier.classify(zeros, probs, &timing));
    printf("zeros: raw output %d, p(%s) = %.4f (DSP %u ms, NN %u ms)\n", gClassifier.lastRawOutput()[0],
           gPackage.label(1), probs[1], static_cast<unsigned>(timing.dspUs / 1000),
           static_cast<unsigned>(timing.nnUs / 1000));
    for (uint32_t i = 0; i < gPackage.labelCount(); i++) {
        TEST_ASSERT_TRUE(probs[i] >= 0.0f && probs[i] <= 1.0f);
    }
    mlFree(zeros);
}

void test_golden_vectors() {
    TEST_ASSERT_TRUE(gClassifier.ready());

    FILE* fp = fopen(kGoldenPath, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(fp, "copy test/fixtures/eloc_golden_vectors.bin to the SD card root");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* data = static_cast<uint8_t*>(mlAlloc(static_cast<size_t>(size), MemKind::Large, 16));
    TEST_ASSERT_NOT_NULL(data);
    size_t got = fread(data, 1, static_cast<size_t>(size), fp);
    fclose(fp);
    TEST_ASSERT_EQUAL(static_cast<size_t>(size), got);

    GoldenVectors golden;
    TEST_ASSERT_TRUE_MESSAGE(golden.parse(data, got), golden.error());
    const GoldenHeader& h = golden.header();
    const FeatureConfig& f = gPackage.features();
    TEST_ASSERT_EQUAL_MESSAGE(f.windowSamples, h.nSamples, "golden vectors belong to a different package");
    TEST_ASSERT_EQUAL(f.nFrames, h.nFrames);
    TEST_ASSERT_EQUAL(f.nMels, h.nMels);
    TEST_ASSERT_EQUAL(gPackage.outputCount(), h.nOutputs);

    uint32_t dspTotal = 0;
    uint32_t nnTotal = 0;
    int worstIn = 0;
    int worstOut = 0;
    for (uint32_t i = 0; i < h.count; i++) {
        GoldenRecord r;
        TEST_ASSERT_TRUE(golden.record(i, r));

        float probs[AI_MAX_LABELS];
        TflmClassifier::Timing timing;
        TEST_ASSERT_TRUE(gClassifier.classify(r.samples, probs, &timing));
        dspTotal += timing.dspUs;
        nnTotal += timing.nnUs;

        float maxFeature = 0.0f;
        int maxIn = 0;
        for (size_t k = 0; k < f.nFeatures(); k++) {
            float df = fabsf(gClassifier.lastFeatures()[k] - r.features[k]);
            maxFeature = df > maxFeature ? df : maxFeature;
            int dq = abs(static_cast<int>(gClassifier.lastInput()[k]) - static_cast<int>(r.input[k]));
            maxIn = dq > maxIn ? dq : maxIn;
        }
        int maxOut = 0;
        for (uint32_t k = 0; k < h.nOutputs; k++) {
            int d = abs(static_cast<int>(gClassifier.lastRawOutput()[k]) - static_cast<int>(r.output[k]));
            maxOut = d > maxOut ? d : maxOut;
        }
        printf("  %-24s raw %4d (golden %4d)  p=%.4f  |dfeat| %.1e  |din| %d  |dout| %d  DSP %3u ms  NN %3u ms\n",
               r.name, gClassifier.lastRawOutput()[0], r.output[0], probs[gPackage.labelCount() - 1], maxFeature,
               maxIn, maxOut, static_cast<unsigned>(timing.dspUs / 1000), static_cast<unsigned>(timing.nnUs / 1000));
        worstIn = maxIn > worstIn ? maxIn : worstIn;
        worstOut = maxOut > worstOut ? maxOut : worstOut;

        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1, maxIn, r.name);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(2, maxOut, r.name);
    }
    printf("%u records: worst |din| %d, |dout| %d; mean DSP %.1f ms, NN %.1f ms\n", static_cast<unsigned>(h.count),
           worstIn, worstOut, dspTotal / 1000.0 / h.count, nnTotal / 1000.0 / h.count);
    mlFree(data);
    logHeap("after golden vectors");
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cpu_240mhz);
    RUN_TEST(test_setup_sd);
    RUN_TEST(test_load_package);
    RUN_TEST(test_classifier_init);
    RUN_TEST(test_invoke_zeros);
    RUN_TEST(test_golden_vectors);
    return UNITY_END();
}

void app_main(void) {
    // Give the serial monitor time to attach before the first output
    vTaskDelay(pdMS_TO_TICKS(2000));
    runUnityTests();
}
