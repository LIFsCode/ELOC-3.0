/*
 * Native test of the TFLM runtime's host-side parts (lib/eloc_ml): the device model package parser
 * and the mel front-end, against the golden vectors of the package in test/fixtures/
 * (DEVICE_MODEL_PACKAGE.md). Run with:
 *   pio test -e generic_unit_tests -f test_generic_mel_frontend
 *
 * Fixtures: eloc_model.tflite and eloc_golden_vectors.bin from the same eloc_device_package.zip as
 * lib/eloc_ml/model/eloc_model_data.h. When the compiled-in model changes, replace both.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "unity.h"
#include "CompiledModel.hpp"
#include "GoldenVectors.hpp"
#include "MelFrontend.hpp"
#include "MlAlloc.hpp"
#include "ModelPackage.hpp"

using namespace eloc_ml;

namespace {

/// A 16-byte aligned copy of a file (TFLite models must be aligned)
struct AlignedFile {
    uint8_t* data = nullptr;
    size_t len = 0;
    ~AlignedFile() { mlFree(data); }
};

bool readFixture(const char* name, AlignedFile& out) {
    // pio runs native tests from the project directory; fall back to a path relative to this file
    std::string here = __FILE__;
    size_t slash = here.find_last_of("/\\");
    std::string candidates[] = {
        std::string("test/fixtures/") + name,
        (slash == std::string::npos ? std::string(".") : here.substr(0, slash)) + "/../fixtures/" + name,
    };
    for (const std::string& path : candidates) {
        FILE* fp = fopen(path.c_str(), "rb");
        if (fp == nullptr) {
            continue;
        }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        out.data = static_cast<uint8_t*>(mlAlloc(static_cast<size_t>(size), MemKind::Large, 16));
        out.len = fread(out.data, 1, static_cast<size_t>(size), fp);
        fclose(fp);
        return out.len == static_cast<size_t>(size);
    }
    printf("fixture %s not found\n", name);
    return false;
}

AlignedFile gModel;
AlignedFile gGolden;

/// Replace the first occurrence of @p from with @p to (same length) inside a copy of the model
bool patchModel(const AlignedFile& src, AlignedFile& dst, const char* from, const char* to) {
    size_t n = strlen(from);
    TEST_ASSERT_EQUAL(n, strlen(to));
    dst.data = static_cast<uint8_t*>(mlAlloc(src.len, MemKind::Large, 16));
    dst.len = src.len;
    memcpy(dst.data, src.data, src.len);
    for (size_t i = 0; i + n <= dst.len; i++) {
        if (memcmp(dst.data + i, from, n) == 0) {
            memcpy(dst.data + i, to, n);
            return true;
        }
    }
    return false;
}

}  // namespace

void setUp(void) {}

void tearDown(void) {}

void test_fixtures_present() {
    TEST_ASSERT_TRUE_MESSAGE(readFixture("eloc_model.tflite", gModel), "test/fixtures/eloc_model.tflite");
    TEST_ASSERT_TRUE_MESSAGE(readFixture("eloc_golden_vectors.bin", gGolden),
                             "test/fixtures/eloc_golden_vectors.bin");
}

void test_compiled_model_matches_fixture() {
    // The header in lib/eloc_ml/model/ and the fixture must come from the same package
    TEST_ASSERT_EQUAL(gModel.len, compiledModelSize());
    TEST_ASSERT_EQUAL_MEMORY(gModel.data, compiledModelData(), gModel.len);
    TEST_ASSERT_EQUAL(0, reinterpret_cast<uintptr_t>(compiledModelData()) % 16);
}

void test_package_loads() {
    ModelPackage pkg;
    bool ok = pkg.load(gModel.data, gModel.len);
    TEST_ASSERT_TRUE_MESSAGE(ok, pkg.error());
    TEST_ASSERT_EQUAL_STRING("", pkg.error());

    TEST_ASSERT_EQUAL_STRING("SkRQW4hUFaXslHibnmv9", pkg.jobId());
    TEST_ASSERT_EQUAL_STRING("ELOC experiment 2026-09-21", pkg.name());
    TEST_ASSERT_EQUAL_STRING("2026-09-21T19:46:52Z", pkg.createdUtc());
    TEST_ASSERT_EQUAL(2, pkg.labelCount());
    TEST_ASSERT_EQUAL_STRING("background", pkg.label(0));
    TEST_ASSERT_EQUAL_STRING("chainsaw", pkg.label(1));
    TEST_ASSERT_TRUE(pkg.activation() == OutputActivation::Sigmoid);
    TEST_ASSERT_EQUAL(1, pkg.outputCount());
    TEST_ASSERT_EQUAL(16000, pkg.sampleRate());
    TEST_ASSERT_EQUAL(16000, pkg.windowSamples());
    TEST_ASSERT_EQUAL(56320, pkg.arenaEstimate());

    const FeatureConfig& f = pkg.features();
    TEST_ASSERT_EQUAL(512, f.frameLength);
    TEST_ASSERT_EQUAL(256, f.frameStep);
    TEST_ASSERT_EQUAL(512, f.fftLength);
    TEST_ASSERT_EQUAL(61, f.nFrames);
    TEST_ASSERT_EQUAL(64, f.nMels);
    TEST_ASSERT_TRUE(f.transform == Transform::Log);
    TEST_ASSERT_EQUAL(1, f.bandStart[0]);
    TEST_ASSERT_EQUAL(236, f.bandStart[63]);
    TEST_ASSERT_EQUAL(20, f.bandLength[63]);
    TEST_ASSERT_EQUAL(499, f.nWeights);  // sum of band_length

    TEST_ASSERT_FALSE(pkg.detectionDefaults(0).present);
    const DetectionDefaults& d = pkg.detectionDefaults(1);
    TEST_ASSERT_TRUE(d.present);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, d.threshold);
    TEST_ASSERT_EQUAL(0, d.observationWindowS);
    TEST_ASSERT_EQUAL(1, d.requiredDetections);

    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.035510119050741196f, pkg.inputQuant().scale);
    TEST_ASSERT_EQUAL(-7, pkg.inputQuant().zeroPoint);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.00390625f, pkg.outputQuant().scale);
    TEST_ASSERT_EQUAL(-128, pkg.outputQuant().zeroPoint);
}

void test_package_rejects() {
    ModelPackage pkg;

    TEST_ASSERT_FALSE(pkg.load(nullptr, 0));
    TEST_ASSERT_FALSE(pkg.loaded());

    // Misaligned copy
    std::vector<uint8_t> shifted(gModel.len + 1);
    memcpy(shifted.data() + 1, gModel.data, gModel.len);
    const uint8_t* misaligned = shifted.data() + 1;
    if (reinterpret_cast<uintptr_t>(misaligned) % 16 != 0) {
        TEST_ASSERT_FALSE(pkg.load(misaligned, gModel.len));
        printf("misaligned: %s\n", pkg.error());
    }

    // Truncated flatbuffer
    TEST_ASSERT_FALSE(pkg.load(gModel.data, gModel.len / 2));
    printf("truncated: %s\n", pkg.error());

    // Config edits, each the same length so the flatbuffer stays valid
    struct Edit {
        const char* from;
        const char* to;
    };
    const Edit edits[] = {
        {"\"format_version\":1", "\"format_version\":2"},
        {"\"eloc-model\"", "\"eloc-modex\""},
        {"\"spec_version\":1", "\"spec_version\":3"},
        {"\"sigmoid\"", "\"sigmoix\""},
        {"\"transform\":\"log\"", "\"transform\":\"lug\""},
        {"\"fft_length\":512", "\"fft_length\":500"},
        {"\"n_frames\":61", "\"n_frames\":60"},
        {"\"average_count\":1", "\"average_count\":2"},
        {"\"hop_samples\":16000", "\"hop_samples\":08000"},
        {"\"labels\":[\"background\"", "\"labels\":[\"chainsaws!\""},
        {"\"window\":\"hann_periodic\"", "\"window\":\"hamm_periodic\""},
    };
    for (const Edit& e : edits) {
        AlignedFile patched;
        TEST_ASSERT_TRUE_MESSAGE(patchModel(gModel, patched, e.from, e.to), e.from);
        TEST_ASSERT_FALSE_MESSAGE(pkg.load(patched.data, patched.len), e.to);
        TEST_ASSERT_FALSE(pkg.loaded());
        TEST_ASSERT_TRUE(strlen(pkg.error()) > 0);
        printf("%-34s -> %s\n", e.to, pkg.error());
    }

    // And a good load afterwards still works
    TEST_ASSERT_TRUE_MESSAGE(pkg.load(gModel.data, gModel.len), pkg.error());
}

void test_quantize_rounds_half_to_even() {
    TensorQuant q;
    q.scale = 1.0f;
    q.zeroPoint = 0;
    const float in[] = {0.5f, 1.5f, 2.5f, -0.5f, -1.5f, 200.0f, -200.0f, NAN};
    const int8_t expected[] = {0, 2, 2, 0, -2, 127, -128, -128};
    int8_t out[8];
    MelFrontend::quantize(in, 8, q, out);
    TEST_ASSERT_EQUAL_INT8_ARRAY(expected, out, 8);
}

void test_golden_vectors() {
    ModelPackage pkg;
    TEST_ASSERT_TRUE_MESSAGE(pkg.load(gModel.data, gModel.len), pkg.error());
    const FeatureConfig& f = pkg.features();

    GoldenVectors golden;
    TEST_ASSERT_TRUE_MESSAGE(golden.parse(gGolden.data, gGolden.len), golden.error());
    const GoldenHeader& h = golden.header();
    TEST_ASSERT_EQUAL(f.windowSamples, h.nSamples);
    TEST_ASSERT_EQUAL(f.nFrames, h.nFrames);
    TEST_ASSERT_EQUAL(f.nMels, h.nMels);
    TEST_ASSERT_EQUAL(pkg.outputCount(), h.nOutputs);
    TEST_ASSERT_TRUE(h.count >= 3);  // at least the three synthetic windows

    MelFrontend frontend;
    TEST_ASSERT_TRUE(frontend.init(f));
    printf("front-end working set: %u bytes\n", static_cast<unsigned>(frontend.workingSetBytes()));

    std::vector<float> features(f.nFeatures());
    std::vector<int8_t> quantized(f.nFeatures());
    float worstFeature = 0.0f;
    int worstQuant = 0;
    for (uint32_t i = 0; i < h.count; i++) {
        GoldenRecord r;
        TEST_ASSERT_TRUE(golden.record(i, r));

        frontend.compute(r.samples, features.data());
        MelFrontend::quantize(features.data(), features.size(), pkg.inputQuant(), quantized.data());

        float maxFeature = 0.0f;
        int maxQuant = 0;
        uint32_t quantMismatches = 0;
        for (size_t k = 0; k < features.size(); k++) {
            maxFeature = fmaxf(maxFeature, fabsf(features[k] - r.features[k]));
            int dq = abs(static_cast<int>(quantized[k]) - static_cast<int>(r.input[k]));
            maxQuant = dq > maxQuant ? dq : maxQuant;
            quantMismatches += dq != 0;
        }
        printf("  %-28s label %2d  score %.4f  max |dfeature| %.2e  max |dq| %d (%u of %u differ)\n", r.name,
               r.labelIndex == GoldenRecord::kSynthetic ? -1 : static_cast<int>(r.labelIndex), r.scores[0],
               maxFeature, maxQuant, static_cast<unsigned>(quantMismatches), static_cast<unsigned>(features.size()));
        worstFeature = fmaxf(worstFeature, maxFeature);
        worstQuant = maxQuant > worstQuant ? maxQuant : worstQuant;

        // Tolerances from DEVICE_MODEL_PACKAGE.md: features 1e-3, quantized input +-1.
        // One exception. The full-scale 1 kHz sine sits exactly on FFT bin 32, so with the Hann
        // window every other bin is (mathematically) empty and some mel bands hold ~1e-10 of the
        // peak power, right at the log floor. There the float32 FFT's round-off (~1e-7 of the peak
        // amplitude) moves a feature by up to ~1.6e-3, against a float64-accurate reference. Real
        // audio never has a 100 dB range inside one frame (all other records agree to ~1e-6), and
        // the quantized input is still exact. A full complex FFT gets 6.4e-4 at twice the cost.
        const float featureTol = strcmp(r.name, "sine_1k_fullscale") == 0 ? 2e-3f : 1e-3f;
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(featureTol, 0.0f, maxFeature, r.name);
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(1, maxQuant, r.name);
    }
    printf("worst over %u records: |dfeature| %.2e, |dq| %d\n", static_cast<unsigned>(h.count), worstFeature,
           worstQuant);
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_fixtures_present);
    RUN_TEST(test_compiled_model_matches_fixture);
    RUN_TEST(test_package_loads);
    RUN_TEST(test_package_rejects);
    RUN_TEST(test_quantize_rounds_half_to_even);
    RUN_TEST(test_golden_vectors);
    return UNITY_END();
}

int main(int argc, char** argv) {
    return runUnityTests();
}
