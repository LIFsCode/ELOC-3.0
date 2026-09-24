/**
 * @file CompiledModel.hpp
 * @brief The device model package compiled into the firmware
 *
 * To change the model: download eloc_device_package.zip from ELOC Model Training, replace
 * lib/eloc_ml/model/eloc_model_data.h with the one inside, Full Clean and build esp32dev-tflm.
 */

#ifndef ELOC_ML_COMPILEDMODEL_HPP_
#define ELOC_ML_COMPILEDMODEL_HPP_

#include <stddef.h>
#include <stdint.h>

namespace eloc_ml {

/// The compiled-in .tflite bytes (16-byte aligned, in flash)
const uint8_t* compiledModelData();
size_t compiledModelSize();

}  // namespace eloc_ml

#endif  // ELOC_ML_COMPILEDMODEL_HPP_
