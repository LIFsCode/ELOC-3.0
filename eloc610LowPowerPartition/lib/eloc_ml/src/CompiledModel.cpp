/**
 * @file CompiledModel.cpp
 * @brief The only translation unit that includes the generated model header
 */

#include "CompiledModel.hpp"

#include "../model/eloc_model_data.h"

namespace eloc_ml {

const uint8_t* compiledModelData() {
    return eloc_model_tflite;
}

size_t compiledModelSize() {
    return eloc_model_tflite_len;
}

}  // namespace eloc_ml
