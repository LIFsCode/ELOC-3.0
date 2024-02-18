/**
 * @file EdgeImpulse.c
 * @brief Edge Impulse class
 * @note The following functions enable continuous audio sampling and inferencing
 * @note https://docs.edgeimpulse.com/docs/tutorials/advanced-inferencing/continuous-audio-sampling
 */

#include "project_config.h"
#include "EdgeImpulse.hpp"
#include "trumpet_trimmed_inferencing.h"

static const char *TAG = "EdgeImpulse";

EdgeImpulse::EdgeImpulse(int i2s_sample_rate) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    // Run stored audio samples through the model to test it
    ESP_LOGI(TAG, "Testing model against pre-recorded sample data...");

    if (EI_CLASSIFIER_RAW_SAMPLE_COUNT > i2s_sample_rate)
        ESP_LOGE(TAG, "The I2S sample rate must be at least %d", EI_CLASSIFIER_RAW_SAMPLE_COUNT);

    status = Status::not_running;
}

void EdgeImpulse::output_inferencing_settings() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    // summary of inferencing settings (from model_metadata.h)
    ESP_LOGI(TAG, "Edge Impulse Inferencing settings:");
    ESP_LOGI(TAG, "Interval: %f ms.", (float)EI_CLASSIFIER_INTERVAL_MS);
    ESP_LOGI(TAG, "Frame size: %d", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ESP_LOGI(TAG, "Sample length: %d ms.", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ESP_LOGI(TAG, "No. of classes: %d", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));
}

/**
 * @brief      Init buffers for inference
 * @return     true if successful
 */
bool EdgeImpulse::buffers_setup(uint32_t n_samples) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    // status = Status::not_running;

    // Check that n_samples is correct, should be EI_CLASSIFIER_RAW_SAMPLE_COUNT
    assert(EI_CLASSIFIER_RAW_SAMPLE_COUNT == n_samples);

    #ifdef EI_BUFFER_IN_PSRAM
        ESP_LOGI(TAG, "Allocating 2 buffers of %d bytes in RAM", n_samples);
        inference.buffers[0] = (int16_t *)heap_caps_malloc(n_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    #else
        inference.buffers[0] = edgeImpulse_buffers[0];
    #endif

    if (inference.buffers[0] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for inference buffer", n_samples * sizeof(int16_t));
        return false;
    }

    #ifdef EI_BUFFER_IN_PSRAM
        inference.buffers[1] = (int16_t *)heap_caps_malloc(n_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    #else
        inference.buffers[1] = edgeImpulse_buffers[1];
    #endif

    if (inference.buffers[1] == NULL) {
        #ifdef EI_BUFFER_IN_PSRAM
            heap_caps_free(inference.buffers[0]);
        #endif

        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;
    inference.status_running = false;

    // status = Status::running;

    return true;
}

/**
 * @brief  Wait on new data.
 *         Blocking function.
 *         Unblocked by audio_inference_callback() setting inference.buf_ready
 *
 * @return     True when finished
 */
bool EdgeImpulse::microphone_inference_record(void) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    while (inference.buf_ready == 0) {
        delay(1);
    }

    inference.buf_ready = 0;

    return true;
}

/**
 * Get raw audio signal data
 */
int EdgeImpulse::microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);

    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
void EdgeImpulse::free_buffers(void) {
    ESP_LOGV(TAG, "Func: %s", __func__);
    status = Status::not_running;
    // Delay in case I2SMEMSSampler::read() is currently loading samples into buffers
    delay(100);

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.buf_ready = 0;

    if (inference.buffers[0] != NULL) {
        #ifdef EI_BUFFER_IN_PSRAM
            ei_free(inference.buffers[0]);
        #endif
    } else {
        ESP_LOGE(TAG, "inference.buffers[0] is already NULL");
    }

    if (inference.buffers[1] != NULL) {
        #ifdef EI_BUFFER_IN_PSRAM
            ei_free(inference.buffers[1]);
        #endif
    } else {
        ESP_LOGE(TAG, "inference.buffers[0] is already NULL");
    }
}

void EdgeImpulse::run_classifier_init() {
    return ::run_classifier_init();
}

EI_IMPULSE_ERROR EdgeImpulse::run_classifier_continuous(signal_t *signal, ei_impulse_result_t *result) {

    ESP_LOGV(TAG, "Func: %s", __func__);

    // calling run_classifier_continuous from ei_run_classifier.h
    return ::run_classifier_continuous(signal, result, this->debug_nn);
}

EI_IMPULSE_ERROR EdgeImpulse::run_classifier(signal_t *signal, ei_impulse_result_t *result) {

    ESP_LOGV(TAG, "Func: %s", __func__);

    // calling run_classifier_continuous from ei_run_classifier.h
    return ::run_classifier(signal, result, this->debug_nn);
}

void EdgeImpulse::ei_thread() {
  ESP_LOGV(TAG, "Func: %s", __func__);

  static auto last_SystemTimeSecs = time_utils::getSystemTimeSecs();

  while (status == Status::running) {
    if (xTaskNotifyWait(
                          0 /* ulBitsToClearOnEntry */,
                          0 /* ulBitsToClearOnExit */,
                          NULL /* pulNotificationValue */,
                          portMAX_DELAY /* xTicksToWait*/) == pdTRUE) {
      if (inference.buf_ready == 1) {
        // Run classifier from main.cpp
        callback();

        // Update times
        auto change_SystemTimeSecs = time_utils::getSystemTimeSecs() - last_SystemTimeSecs;
        detectingTime_secs += change_SystemTimeSecs;
        totalDetectingTime_secs += change_SystemTimeSecs;
        last_SystemTimeSecs = time_utils::getSystemTimeSecs();
        ESP_LOGV(TAG, "detectingTime_secs: %d", detectingTime_secs);
        ESP_LOGV(TAG, "totalDetectingTime_secs: %d", totalDetectingTime_secs);
      }
    }  // if (xTaskNotifyWait())
  }
  ESP_LOGI(TAG, "deleting task");
  inference.status_running = false;
  vTaskDelete(NULL);
}

void EdgeImpulse::start_ei_thread_wrapper(void *_this) {
  reinterpret_cast<EdgeImpulse *>(_this)->ei_thread();
}

esp_err_t EdgeImpulse::start_ei_thread(std::function<void()> _callback) {
  ESP_LOGV(TAG, "Func: %s", __func__);

  status = Status::running;
  inference.status_running = true;
  detectingTime_secs = 0;

  this->callback = _callback;

  int ret = xTaskCreatePinnedToCore(this->start_ei_thread_wrapper, "ei_thread", 1024 * 4, this, TASK_PRIO_AI, &ei_TaskHandler, TASK_AI_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create ei_thread");
    status = Status::not_running;
    inference.status_running = false;
    return ret;
  }

  return ESP_OK;
}

String EdgeImpulse::get_aiModel() const {
    String s =  String(EI_CLASSIFIER_PROJECT_NAME) +
                (".") +
                String(EI_CLASSIFIER_PROJECT_ID) +
                "." +
                String(EI_CLASSIFIER_PROJECT_DEPLOY_VERSION);
    return s;
}

const char* EdgeImpulse::get_ei_classifier_inferencing_categories(int i) const {
    return ei_classifier_inferencing_categories[i];
}
