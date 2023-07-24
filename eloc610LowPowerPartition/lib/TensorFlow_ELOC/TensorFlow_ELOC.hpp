/**
 * @file TensorFlow_ELOC.hpp
 *
 * @brief TensorFlow implementation code for the ELOC device.
 *
 * @version 0.1
 *
 * @date 2021-04-06
 *
 *
 */

#ifndef TENSORFLOW_ELOC_HPP
#define TENSORFLOW_ELOC_HPP

#include "esp_err.h"
#include "esp_log.h"

/**
 * The following files are sourced from library:
 *
 * 'TensorFlowLite_ESP32 by TensorFlow Authors'
 *
 * But could probably be sourced from:
 *
 * https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro
 *
 **/

#include "../tflite-micro/tensorflow/lite/micro/micro_interpreter.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "../tflite-micro/tensorflow/lite/micro/system_setup.h"
#include "../tflite-micro/tensorflow/lite/schema/schema_generated.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_error_reporter.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_interpreter.h"
#include "../tflite-micro/tensorflow/lite/micro/all_ops_resolver.h"

// Model files
#include "../../include/models/koogu_narw_lighter_basic.h"
#include "../../include/models/model.h"

// This sets the memory used by TensorFlow
// Needs to be set by trial & error
// Min 1, max 99 (??)
constexpr int kTensorArenaSize = 10 * 1024;

class TensorFlow_ELOC
{
private:
    tflite::MicroErrorReporter micro_error_reporter;
    tflite::ErrorReporter* error_reporter = nullptr;

    TfLiteTensor* model_input = nullptr;
    TfLiteTensor* model_output = nullptr;
    
    // TODO: Using 'AllOpsResolver' for now, but should probably use 'MicroMutableOpResolver'
    tflite::AllOpsResolver resolver;
    // static tflite::MicroMutableOpResolver<4> micro_op_resolver(error_reporter);
    
    tflite::MicroInterpreter *interpreter = nullptr;

    // int8_t *model_input_buffer = nullptr;
    // FeatureProvider *feature_provider;
    // RecognizeCommands *recognizer;
    //uint8_t feature_buffer[kFeatureElementCount];
    
    // Create an area of memory to use for input, output, and intermediate arrays.
    // The size of this will depend on the model you're using, and may need to be
    // determined by experimentation.
    uint8_t tensor_arena[kTensorArenaSize] = {0};

    //
    const tflite::Model* test_model = tflite::GetModel(g_model);
    const tflite::Model* default_model = tflite::GetModel(koogu_narw);
    
public:

    /**
     * @brief Construct a new TensorFlow_ELOC object
     * 
    */
    TensorFlow_ELOC();

    /**
     * @brief Load the model from the flash memory.
     * @details This model is the normal used for the ELOC device.
     * @returns true if the model was loaded successfully.
    */
    bool load_default_model();

    /**
     * @brief load test model from flash memory.
     * @details This model is used for testing purposes only.
     * @returns true if the model was loaded successfully.
     * 
    */
    bool load_test_model();


    ~TensorFlow_ELOC();
};

#endif // TENSORFLOW_ELOC_HPP
