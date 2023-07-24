/**
 * @file TensorFlow_ELOC.cpp
 *
 * @brief TensorFlow implementation code for the ELOC device.
 *
 * @version 0.1
 *
 * @date 2021-04-06
 *
 */

#include "TensorFlow_ELOC.hpp"

static const char *TAG = "TensorFlow_ELOC";

TensorFlow_ELOC::TensorFlow_ELOC()
{
    ESP_LOGI(TAG, "TensorFlow_ELOC constructor");

    error_reporter = &micro_error_reporter;

    const tflite::Model* model = tflite::GetModel(g_model);

    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        TF_LITE_REPORT_ERROR(error_reporter,
                             "Model provided is schema version %d not equal "
                             "to supported version %d.",
                             model->version(), TFLITE_SCHEMA_VERSION);
        return;
    }

    tflite::MicroInterpreter inter(model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &inter;

    // Allocate memory from the tensor_arena for the model's tensors.
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return;
    }

    // Assign model input and output buffers (tensors) to pointers
    model_input = interpreter->input(0);
    model_output = interpreter->output(0);

    // Make sure the input has the properties we expect
    if (model_input == nullptr)
        ESP_LOGI(TAG, "Error: model_input = nullptr");

    ESP_LOGI(TAG, "model_input size = %u", model_input->dims->size);
    ESP_LOGI(TAG, "model_input->dims->data[0] = %u", model_input->dims->data[0]);
    ESP_LOGI(TAG,  "model_input->dims->data[1] = %u", model_input->dims->data[1]);

    // The input is a 32 bit floating point value
    ESP_LOGI(TAG, "model_input->type = %u", model_input->type);

}

TensorFlow_ELOC::load_default_model()
{
    ESP_LOGI(TAG, "load_default_model");
}

TensorFlow_ELOC::load_custom_model()
{
    ESP_LOGI(TAG, "load_custom_model");
}

TensorFlow_ELOC::~TensorFlow_ELOC()
{
    ESP_LOGI(TAG, "TensorFlow_ELOC destructor");
}
