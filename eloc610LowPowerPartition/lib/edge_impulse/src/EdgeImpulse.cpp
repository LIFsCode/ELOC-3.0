/**
 * @file EdgeImpulse.c
 * @brief Edge Impulse helper functions
 *
*/

#include "EdgeImpulse.hpp"
#include "trumpet_inferencing.h"

static const char *TAG = "EdgeImpulse";


/** Audio buffers, pointers and selectors */
/** Need to be globally accessible... */
inference_t inference;
const uint32_t sample_buffer_size = 2048;
signed short sampleBuffer[sample_buffer_size];
bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
bool record_status = true;


// The following functions enable continuous audio sampling and inferencing
// https://docs.edgeimpulse.com/docs/tutorials/advanced-inferencing/continuous-audio-sampling


void output_inferencing_settings(){
    // summary of inferencing settings (from model_metadata.h)
    ESP_LOGI(TAG, "Edge Impulse Inferencing settings:");
    ESP_LOGI(TAG, "Interval: %f ms.", (float)EI_CLASSIFIER_INTERVAL_MS);
    ESP_LOGI(TAG, "Frame size: %d", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ESP_LOGI(TAG, "Sample length: %d ms.", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ESP_LOGI(TAG, "No. of classes: %d", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));
}

/**
 * This function is repeatedly called by capture_samples()
 * When sufficient samples are collected:
 *  1. inference.buf_ready = 1
 *  2. microphone_inference_record() is unblocked
 *  3. classifier is run in main loop()
 */
void audio_inference_callback(uint32_t n_bytes)
{

    ESP_LOGI(TAG,"audio_inference_callback()");

    for (int i = 0; i < n_bytes >> 1; i++)
    {
        inference.buffer[inference.buf_count++] = sampleBuffer[i];

#ifdef SDCARD_WRITING_ENABLED
        if (record_buffer_idx < EI_CLASSIFIER_RAW_SAMPLE_COUNT * 10)
        {
            recordBuffer[record_buffer_idx++] = sampleBuffer[i];
        }
        else
        {
            ESP_LOGI(TAG, "Warning: Record buffer is full, skipping sample\n");
        }
#endif

        if (inference.buf_count >= inference.n_samples)
        {
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

/**
 * This is initiated by a task created in microphone_inference_record_start()
 * When periodically called it:
 *  1. reads data from I2S DMA buffer,
 *  2. scales it
 *  3. Calls audio_inference_callback()
 */
void capture_samples(void *arg)
{

    ESP_LOGI(TAG, "capture_samples()");

    record_status = false;

    const int32_t i2s_bytes_to_read = (uint32_t)arg;

    // logical right shift divides a number by 2, throwing out any remainders
    size_t i2s_samples_to_read = i2s_bytes_to_read >> 1;

    if (input == nullptr){
        ESP_LOGE(TAG, "I2SMEMSSampler input == nullptr");
        return;
    }

    if (input->start() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start I2S MEMS sampler");
        return;
    }

    record_status = true;

    // Enter a loop to collect new data from I2S
    while (record_status)
    {
        int samples_read = input->read(sampleBuffer, i2s_samples_to_read);

        // Scale the data
        for (int x = 0; x < i2s_samples_to_read; x++)
        {
            sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * I2S_DATA_SCALING_FACTOR;
        }

        if (record_status)
        {
            audio_inference_callback(i2s_bytes_to_read);
        }
        else
        {
            break;
        }
    }

    input->stop();

    vTaskDelete(NULL);
}

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
bool microphone_inference_start(uint32_t n_samples)
{
    ESP_LOGI(TAG, "microphone_inference_start()");

    record_status = false;

    inference.buffer = (int16_t *)heap_caps_malloc(n_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (inference.buffer == NULL)
    {
        ESP_LOGE(TAG, "ERR: Failed to allocate buffer for signal");
        return false;
    }

    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;

    // EI implementation
    // if (i2s_init(EI_CLASSIFIER_FREQUENCY)) {
    //     ESP_LOGE(TAG, "Failed to start I2S!");
    // }

    // TODO: getI2sConfig();
    
    if (input != nullptr)
    {
        // From restart an instance may already exist
        ESP_LOGE(TAG, "I2SMEMSSampler input != nullptr");
        return false;
    }
    // TODO: input = new I2SMEMSSampler (I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift,getConfig().listenOnly, getMicInfo().MicUseTimingFix);

    ei_sleep(100);

    record_status = true;

    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 16, (void *)sample_buffer_size, 10, NULL);

    return true;
}

/**
 * @brief  Wait on new data.
 *         Blocking function.
 *         Unblocked by audio_inference_callback() setting inference.buf_ready
 *
 * @return     True when finished
 */
bool microphone_inference_record(void)
{
    ESP_LOGI(TAG, "microphone_inference_record()");

    bool ret = true;

    while (inference.buf_ready == 0)
    {
        delay(10);
    }

    // ESP_LOGI(TAG, "microphone_inference_record() - exit");
    inference.buf_ready = 0;
    return ret;
}

/**
 * Get raw audio signal data
 */
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
   ESP_LOGI(TAG,"microphone_audio_signal_get_data()");
   numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);

    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
void microphone_inference_end(void)
{
    ESP_LOGI(TAG, "microphone_inference_end()");

    record_status = false;
    // Wait for 'capture_samples' thread to terminate
    delay(1000);
    
    if (input == nullptr)
    {
         ESP_LOGE(TAG, "I2SMEMSSampler input == nullptr");
        return;
    }
   
    input->stop();
    heap_caps_free(inference.buffer);

    delete input;

    input = nullptr;

}