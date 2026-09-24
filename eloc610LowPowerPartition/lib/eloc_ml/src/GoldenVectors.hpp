/**
 * @file GoldenVectors.hpp
 * @brief Reader for eloc_golden_vectors.bin (DEVICE_MODEL_PACKAGE.md, "Golden vectors")
 *
 * Reference windows with the expected features, quantized inputs and raw outputs, used by the
 * native front-end test and the on-target TFLM test. Python reference:
 * device_package.read_golden_vectors(). Host-safe (no ESP headers).
 */

#ifndef ELOC_ML_GOLDENVECTORS_HPP_
#define ELOC_ML_GOLDENVECTORS_HPP_

#include <stddef.h>
#include <stdint.h>

namespace eloc_ml {

struct GoldenHeader {
    uint32_t version = 0;
    uint32_t count = 0;
    uint32_t nSamples = 0;
    uint32_t nFrames = 0;
    uint32_t nMels = 0;
    uint32_t nOutputs = 0;
    uint32_t recordBytes = 0;
};

struct GoldenRecord {
    static constexpr uint32_t kSynthetic = 0xFFFFFFFFu;  ///< labelIndex of silence/sine/noise

    uint32_t labelIndex = 0;
    char name[29] = "";
    const int16_t* samples = nullptr;   ///< [nSamples]
    const float* features = nullptr;    ///< [nFrames * nMels], after normalization
    const int8_t* input = nullptr;      ///< [nFrames * nMels] quantized model input
    const int8_t* output = nullptr;     ///< [nOutputs] raw model output
    const float* scores = nullptr;      ///< [nOutputs] dequantized output
};

class GoldenVectors {
 public:
    /**
     * @brief Parse a whole file held in memory (4-byte aligned). Nothing is copied: @p data must
     *        outlive the records handed out by record().
     */
    bool parse(const uint8_t* data, size_t len);

    const GoldenHeader& header() const { return mHeader; }
    const char* error() const { return mError; }

    /// Record @p i, or false when out of range
    bool record(uint32_t i, GoldenRecord& out) const;

 private:
    const uint8_t* mData = nullptr;
    size_t mLen = 0;
    GoldenHeader mHeader;
    const char* mError = "";
};

}  // namespace eloc_ml

#endif  // ELOC_ML_GOLDENVECTORS_HPP_
