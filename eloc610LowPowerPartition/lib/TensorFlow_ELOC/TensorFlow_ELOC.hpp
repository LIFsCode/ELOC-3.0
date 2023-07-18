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

// #include "esp_err.h"
// #include "esp_log.h"
#include <TensorFlowLite_ESP32.h>

#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"

// This sets the memory used by TensorFlow
// Needs to be set by trial & error
// Min 1, max 99 (??)
constexpr int kTensorArenaSize = 10 * 1024;

class TensorFlow_ELOC
{
    private:        
        tflite::ErrorReporter *error_reporter = nullptr;
        const tflite::Model *model = nullptr;
        tflite::MicroInterpreter *interpreter = nullptr;
        TfLiteTensor *model_input = nullptr;
        int8_t *model_input_buffer = nullptr;
        // FeatureProvider *feature_provider = nullptr;
        // RecognizeCommands *recognizer = nullptr;

        // Create an area of memory to use for input, output, and intermediate arrays.
        // The size of this will depend on the model you're using, and may need to be
        // determined by experimentation.
        uint8_t tensor_arena[kTensorArenaSize] = {0};
        //  int8_t feature_buffer[kFeatureElementCount];

    public:
        TensorFlow_ELOC();        
        ~TensorFlow_ELOC();
};




#endif // TENSORFLOW_ELOC_HPP
