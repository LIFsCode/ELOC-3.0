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
}

bool TensorFlow_ELOC::load_default_model()
{
    ESP_LOGI(TAG, "load_default_model");

    if (default_model->version() != TFLITE_SCHEMA_VERSION)
    {
        TF_LITE_REPORT_ERROR(error_reporter,
                             "Model provided is schema version %d not equal to supported version %d.",
                             default_model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroInterpreter inter(default_model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &inter;

    return true;
}

bool TensorFlow_ELOC::load_test_model()
{
    ESP_LOGI(TAG, "load_test_model");

    if (test_model->version() != TFLITE_SCHEMA_VERSION)
    {
        TF_LITE_REPORT_ERROR(error_reporter,
                             "Model provided is schema version %d not equal to supported version %d.",
                             test_model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroInterpreter inter(test_model, resolver, tensor_arena, kTensorArenaSize, error_reporter);
    interpreter = &inter;

    return true;
}

bool TensorFlow_ELOC::allocate_model()
{

    ESP_LOGI(TAG, "allocate_model");

    // Allocate memory from the tensor_arena for the model's tensors.
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk)
    {
        TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
        return false;
    }

    // Assign model input and output buffers (tensors) to pointers
    model_input = interpreter->input(0);
    model_output = interpreter->output(0);

    // Make sure the input has the properties we expect
    if (model_input == nullptr)
    {
        ESP_LOGI(TAG, "Error: model_input = nullptr");
        return false;
    }

    ESP_LOGI(TAG, "model_input size = %u", model_input->dims->size);
    ESP_LOGI(TAG, "model_input->dims->data[0] = %u", model_input->dims->data[0]);
    ESP_LOGI(TAG, "model_input->dims->data[1] = %u", model_input->dims->data[1]);
    // The input is a 32 bit floating point value
    ESP_LOGI(TAG, "model_input->type = %u", model_input->type);

    return true;
}

bool TensorFlow_ELOC::verify_test_model()
{
    ESP_LOGI(TAG, "verify_test_model");

    // Copy a spectrogram created from a .wav audio file of someone saying "Yes",
    // into the memory area used for the input.
    const int8_t *yes_features_data = g_yes_micro_f2e59fea_nohash_1_data;
    for (size_t i = 0; i < model_input->bytes; ++i)
    {
        model_input->data.int8[i] = yes_features_data[i];
    }

    // Run the model on this input and make sure it succeeds.
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk)
    {
        ESP_LOGI(TAG, "Invoke failed\n");
    }

    // Get the output from the model, and make sure it's the expected size and type.
    TfLiteTensor *output = interpreter->output(0);
    if (output->dims->size != 2)
    {
        ESP_LOGI(TAG, "output->dims->size != 2");
        return false;
    }
    if (output->dims->data[0] != 1)
    {
        ESP_LOGI(TAG, "output->dims->data[0] != 1");
        return false;
    }
    if (output->dims->data[1] != 4)
    {
        ESP_LOGI(TAG, "output->dims->data[1] != 4");
        return false;
    }
    if (output->type != kTfLiteInt8)
    {
        ESP_LOGI(TAG, "output->type != kTfLiteInt8");
        return false;
    }

    // There are four possible classes in the output, each with a score.
    const int kSilenceIndex = 0;
    const int kUnknownIndex = 1;
    const int kYesIndex = 2;
    const int kNoIndex = 3;

    // Make sure that the expected "Yes" score is higher than the other classes.
    uint8_t silence_score = output->data.int8[kSilenceIndex] + 128;
    uint8_t unknown_score = output->data.int8[kUnknownIndex] + 128;
    uint8_t yes_score = output->data.int8[kYesIndex] + 128;
    uint8_t no_score = output->data.int8[kNoIndex] + 128;
    if (yes_score < silence_score)
    {
        ESP_LOGI(TAG, "yes_score < silence_score");
        return false;
    }
    if (yes_score < unknown_score)
    {
        ESP_LOGI(TAG, "yes_score < unknown_score");
        return false;
    }
    if (yes_score < no_score)
    {
        ESP_LOGI(TAG, "yes_score < no_score");
        return false;
    }

    // Now test with a different input, from a recording of "No".
    const int8_t *no_features_data = g_no_micro_f9643d42_nohash_4_data;
    for (size_t i = 0; i < model_input->bytes; ++i)
    {
        model_input->data.int8[i] = no_features_data[i];
    }

    // Run the model on this "No" input.
    invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk)
    {
        MicroPrintf("Invoke failed\n");
    }

    // Get the output from the model, and make sure it's the expected size and type
    output = interpreter->output(0);
    if (output->dims->size != 2)
    {
        ESP_LOGI(TAG, "output->dims->size != 2");
        return false;
    }
    if (output->dims->data[0] != 1)
    {
        ESP_LOGI(TAG, "output->dims->data[0] != 1");
        return false;
    }
    if (output->dims->data[1] != 4)
    {
        ESP_LOGI(TAG, "output->dims->data[1] != 4");
        return false;
    }
    if (output->type != kTfLiteInt8)
    {
        ESP_LOGI(TAG, "output->type != kTfLiteInt8");
        return false;
    }

    // Make sure that the expected "No" score is higher than the other classes.
    silence_score = output->data.int8[kSilenceIndex] + 128;
    unknown_score = output->data.int8[kUnknownIndex] + 128;
    yes_score = output->data.int8[kYesIndex] + 128;
    no_score = output->data.int8[kNoIndex] + 128;
    if (no_score < silence_score)
    {
        ESP_LOGI(TAG, "no_score < silence_score");
        return false;
    }
    if (no_score < unknown_score)
    {
        ESP_LOGI(TAG, "no_score < unknown_score");
        return false;
    }
    if (no_score < yes_score)
    {
        ESP_LOGI(TAG, "no_score < yes_score");
        return false;
    }

    ESP_LOGI(TAG, "verify_test_model ran successfully\n");
    return true;
}

TensorFlow_ELOC::~TensorFlow_ELOC()
{
    ESP_LOGI(TAG, "TensorFlow_ELOC destructor");
}
