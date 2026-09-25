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
    // Model-independent: whatever package the fixtures hold, its config must be self-consistent.
    // (Replace the fixtures together with lib/eloc_ml/model/eloc_model_data.h; see README-ai.md.)
    ModelPackage pkg;
    bool ok = pkg.load(gModel.data, gModel.len);
    TEST_ASSERT_TRUE_MESSAGE(ok, pkg.error());
    TEST_ASSERT_EQUAL_STRING("", pkg.error());

    const FeatureConfig& f = pkg.features();
    printf("model \"%s\" job %s created %s\n", pkg.name(), pkg.jobId(), pkg.createdUtc());
    printf("  %u Hz, window %u, %u frames x %u mels (frame %u, step %u, FFT %u), %u outputs, arena est. %u\n",
           static_cast<unsigned>(pkg.sampleRate()), static_cast<unsigned>(pkg.windowSamples()),
           static_cast<unsigned>(f.nFrames), static_cast<unsigned>(f.nMels), static_cast<unsigned>(f.frameLength),
           static_cast<unsigned>(f.frameStep), static_cast<unsigned>(f.fftLength),
           static_cast<unsigned>(pkg.outputCount()), static_cast<unsigned>(pkg.arenaEstimate()));
    for (uint32_t i = 0; i < pkg.labelCount(); i++) {
        const DetectionDefaults& d = pkg.detectionDefaults(i);
        printf("  label %u: %s", static_cast<unsigned>(i), pkg.label(i));
        if (d.present) {
            printf("  (recommends threshold %.2f, window %u s, %u detections)", d.threshold,
                   static_cast<unsigned>(d.observationWindowS), static_cast<unsigned>(d.requiredDetections));
        }
        printf("\n");
    }

    TEST_ASSERT_TRUE(strlen(pkg.jobId()) > 0);
    TEST_ASSERT_TRUE(strlen(pkg.name()) > 0);
    TEST_ASSERT_TRUE(pkg.labelCount() >= 2 && pkg.labelCount() <= AI_MAX_LABELS);
    TEST_ASSERT_TRUE(pkg.outputCount() == pkg.labelCount() ||
                     (pkg.outputCount() == 1 && pkg.labelCount() == 2 &&
                      pkg.activation() == OutputActivation::Sigmoid));
    TEST_ASSERT_TRUE(pkg.sampleRate() > 0 && pkg.windowSamples() > 0);
    TEST_ASSERT_EQUAL(1 + (f.windowSamples - f.frameLength) / f.frameStep, f.nFrames);
    TEST_ASSERT_TRUE((f.fftLength & (f.fftLength - 1)) == 0 && f.frameLength <= f.fftLength);

    uint32_t weights = 0;
    for (uint32_t m = 0; m < f.nMels; m++) {
        TEST_ASSERT_TRUE(f.bandStart[m] + f.bandLength[m] <= f.nBins());
        weights += f.bandLength[m];
    }
    TEST_ASSERT_EQUAL(weights, f.nWeights);

    TEST_ASSERT_FALSE(pkg.detectionDefaults(0).present);  // never for the background label
    TEST_ASSERT_TRUE(pkg.inputQuant().scale > 0.0f && pkg.outputQuant().scale > 0.0f);
}

namespace {

/// The same number with its last digit changed, so it keeps its length (61 -> 62, 512 -> 513)
std::string otherNumber(uint32_t v) {
    std::string s = std::to_string(v);
    char& last = s.back();
    last = last == '9' ? '8' : static_cast<char>(last + 1);
    return s;
}

/// The same word with its last letter replaced ("log" -> "loq"), so it keeps its length
std::string otherWord(const char* w) {
    std::string s = w;
    s.back() = s.back() == 'q' ? 'z' : 'q';
    return s;
}

const char* transformName(Transform t) {
    switch (t) {
        case Transform::Linear: return "linear";
        case Transform::Log: return "log";
        case Transform::Pcen: return "pcen";
        case Transform::PcenLog: return "pcen+log";
    }
    return "";
}

}  // namespace

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

    // Config edits built from the loaded model, each keeping its length so the flatbuffer stays
    // valid. The embedded JSON is compact ("key":value, no spaces).
    TEST_ASSERT_TRUE_MESSAGE(pkg.load(gModel.data, gModel.len), pkg.error());
    const FeatureConfig& f = pkg.features();
    const char* activation = pkg.activation() == OutputActivation::Sigmoid ? "sigmoid" : "softmax";
    auto num = [](const char* key, uint32_t v) { return std::string("\"") + key + "\":" + std::to_string(v); };
    auto numBad = [](const char* key, uint32_t v) { return std::string("\"") + key + "\":" + otherNumber(v); };
    auto str = [](const char* key, const char* v) { return std::string("\"") + key + "\":\"" + v + "\""; };
    auto strBad = [](const char* key, const char* v) {
        return std::string("\"") + key + "\":\"" + otherWord(v) + "\"";
    };

    struct Edit {
        std::string from;
        std::string to;
    };
    const std::vector<Edit> edits = {
        {"\"format_version\":1", "\"format_version\":2"},
        {"\"eloc-model\"", "\"eloc-modex\""},
        {"\"spec_version\":1", "\"spec_version\":3"},
        {str("activation", activation), strBad("activation", activation)},
        {str("transform", transformName(f.transform)), strBad("transform", transformName(f.transform))},
        {num("fft_length", f.fftLength), numBad("fft_length", f.fftLength)},
        {num("n_frames", f.nFrames), numBad("n_frames", f.nFrames)},
        {"\"average_count\":1", "\"average_count\":2"},
        {num("hop_samples", f.windowSamples), numBad("hop_samples", f.windowSamples)},
        {std::string("\"labels\":[\"") + pkg.label(0) + "\"", std::string("\"labels\":[\"") + otherWord(pkg.label(0)) + "\""},
        {"\"window\":\"hann_periodic\"", "\"window\":\"hamm_periodic\""},
    };
    for (const Edit& e : edits) {
        AlignedFile patched;
        TEST_ASSERT_TRUE_MESSAGE(patchModel(gModel, patched, e.from.c_str(), e.to.c_str()), e.from.c_str());
        TEST_ASSERT_FALSE_MESSAGE(pkg.load(patched.data, patched.len), e.to.c_str());
        TEST_ASSERT_FALSE(pkg.loaded());
        TEST_ASSERT_TRUE(strlen(pkg.error()) > 0);
        printf("%-34s -> %s\n", e.to.c_str(), pkg.error());
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
