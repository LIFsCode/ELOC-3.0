/**
 * @file EdgeImpulse.c
 * @brief Edge Impulse class
 * @note The following functions enable continuous audio sampling and inferencing
 * @note https://docs.edgeimpulse.com/docs/tutorials/advanced-inferencing/continuous-audio-sampling
 */

#include "project_config.h"
#include "utils/ffsutils.h"
#include "EdgeImpulse.hpp"
#include "trumpet_trimmed_inferencing.h"
#include "edge-impulse-sdk/dsp/numpy_types.h"
#include "test_samples.h"
#include "ElocProcessFactory.hpp"

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
        ei_callback_func();

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
  //TODO: Check if this must be protected by a vTaskSuspendAll() & xTaskResumeAll()
  mTaskHandle = nullptr;
  vTaskDelete(NULL);
}

void EdgeImpulse::start_ei_thread_wrapper(void *_this) {
  reinterpret_cast<EdgeImpulse *>(_this)->ei_thread();
}

esp_err_t EdgeImpulse::start_ei_thread(read_func_t read_func) {
  ESP_LOGV(TAG, "Func: %s", __func__);

  status = Status::running;
  inference.status_running = true;
  detectingTime_secs = 0;

  #ifdef AI_CONTINUOUS_INFERENCE
    mSignal.total_length = EI_CLASSIFIER_SLICE_SIZE;
  #else
    mSignal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  #endif  // AI_CONTINUOUS_INFERENCE

  mSignal.get_data = read_func;

  //this->callback = _callback;

  int ret = xTaskCreatePinnedToCore(this->start_ei_thread_wrapper, "ei_thread", 1024 * 4, this, TASK_PRIO_AI, &mTaskHandle, TASK_AI_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create ei_thread");
    status = Status::not_running;
    inference.status_running = false;
    return ret;
  }

  return ESP_OK;
}

const TaskHandle_t* EdgeImpulse::getTaskHandle() {
    return &mTaskHandle;
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

esp_err_t checkSDCard();

/**
 * @brief This callback allows a thread created in EdgeImpulse to
 *        run the inference. Required due to namespace issues, static implementations etc..
 */
void EdgeImpulse::ei_callback_func() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    if (this->get_status() == EdgeImpulse::Status::running) {
        ESP_LOGV(TAG, "Running inference");
        bool m = this->microphone_inference_record();
        // Blocking function - unblocks when buffer is full
        if (!m) {
            ESP_LOGE(TAG, "ERR: Failed to record audio...");
            // Give up on this inference, come back next time
            return;
        }

        auto startCounter = cpu_hal_get_cycle_count();


        ei_impulse_result_t result = {0};

        #ifdef AI_CONTINUOUS_INFERENCE
            EI_IMPULSE_ERROR r = this->run_classifier_continuous(&mSignal, &result);
        #else
            EI_IMPULSE_ERROR r = this->run_classifier(&mSignal, &result);
        #endif  // AI_CONTINUOUS_INFERENCE

        ESP_LOGI(TAG, "Cycles taken to run inference = %d", (cpu_hal_get_cycle_count() - startCounter));

        if (r != EI_IMPULSE_OK) {
            ESP_LOGE(TAG, "ERR: Failed to run classifier (%d)", r);
            // Give up on this inference, come back next time
            return;
        }

        auto target_sound_detected = false;

        #ifdef AI_CONTINUOUS_INFERENCE
            if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW))  // NOLINT
        #else
            // Non-continuous, always print
            if (1)  // NOLINT
        #endif  //  AI_CONTINUOUS_INFERENCE
            {
                ESP_LOGI(TAG, "(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                        result.timing.dsp, result.timing.classification, result.timing.anomaly);

                String file_str;

                for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                    ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                    // Build string to save to inference results file
                    file_str += ", ";
                    file_str += result.classification[ix].value;

                    if ((strcmp(result.classification[ix].label, "background") != 0) &&
                        result.classification[ix].value > AI_RESULT_THRESHOLD) {
                        this->increment_detectedEvents();
                        target_sound_detected = true;
                    }
                }

                // ESP_LOGI(TAG, "detectedEvents = %d", this->get_detectedEvents());

                file_str += "\n";
                // Only save results & wav file if classification value exceeds a threshold
                if (save_ai_results_to_sd == true &&
                    checkSDCard() == ESP_OK &&
                    target_sound_detected == true) {
                    save_inference_result_SD(file_str);
                }

            #if EI_CLASSIFIER_HAS_ANOMALY == 1
                ESP_LOGI(TAG, "    anomaly score: %f", result.anomaly);
            #endif  // EI_CLASSIFIER_HAS_ANOMALY

            #ifdef AI_CONTINUOUS_INFERENCE
                print_results = 0;
            #endif  // AI_CONTINUOUS_INFERENCE
            }
    }

    ESP_LOGV(TAG, "Inference complete");
}

void EdgeImpulse::test_inference(read_func_t read_func) {

    this->buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);

    if (1) {
        // Run stored audio samples through the model to test it
        // Use non-continuous process for this
        ESP_LOGI(TAG, "Testing model against pre-recorded sample data...");

        static_assert((EI_CLASSIFIER_RAW_SAMPLE_COUNT <= TEST_SAMPLE_LENGTH),
                       "TEST_SAMPLE_LENGTH must be at least equal to EI_CLASSIFIER_RAW_SAMPLE_COUNT");

        if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH) {
            ESP_LOGI(TAG, "TEST_SAMPLE length is greater than the Edge Impulse model length, applying down-sampling");
        }

        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = read_func;
        ei_impulse_result_t result = {0};

        // Artificially fill buffer with test data
        auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        auto skip_current = ei_skip_rate;   // Make sure to fill first sample, then start skipping if needed

        for (auto i = 0; i < test_array_size; i++) {
            ESP_LOGI(TAG, "Running test category: %s", test_array_categories[i]);

            for (auto test_sample_count = 0, inference_buffer_count = 0; (test_sample_count < TEST_SAMPLE_LENGTH) &&
                    (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT); test_sample_count++) {
                if (skip_current >= ei_skip_rate) {
                    this->inference.buffers[0][inference_buffer_count++] = test_array[i][test_sample_count];
                    skip_current = 1;
                } else {
                    skip_current++;
                }
            }

            // Mark buffer as ready
            // Mark active buffer as inference.buffers[1], inference run on inactive buffer
            this->inference.buf_select = 1;
            this->inference.buf_count = 0;
            this->inference.buf_ready = 1;

            EI_IMPULSE_ERROR r = this->run_classifier(&signal, &result);
            if (r != EI_IMPULSE_OK) {
                ESP_LOGW(TAG, "ERR: Failed to run classifier (%d)", r);
                return;
            }

            // print the predictions
            ESP_LOGI(TAG, "Test model predictions:");
            ESP_LOGI(TAG, "    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                    result.timing.dsp, result.timing.classification, result.timing.anomaly);
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                if (strcmp(result.classification[ix].label, "trumpet") == 0 &&
                    strcmp(test_array_categories[i], "trumpet") == 0) {
                    if (result.classification[ix].value < AI_RESULT_THRESHOLD) {
                        ESP_LOGW(TAG, "Test of trumpet sample appears to be poor, check model!");
                    }
                }
            }
        }
    }
    // Free buffers as the buffer size for continuous & non-continuous differs
    this->free_buffers();
}

int EdgeImpulse::create_inference_result_file_SD() {
    if (!ffsutil::folderExists(mSessionFolder.c_str())) {
        ESP_LOGE(TAG, "Session folder %s does not exist!", mSessionFolder.c_str());
        return -1;
    }

    ei_results_filename = mSessionFolder;
    ei_results_filename += "/EI-results-ID-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_ID;
    ei_results_filename += "-DEPLOY-VER-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
    ei_results_filename += ".csv";

    if (1) {
        ESP_LOGI(TAG, "EI results filename: %s", ei_results_filename.c_str());
    }

    FILE *fp_result = fopen(ei_results_filename.c_str(), "wb");
    if (!fp_result) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    String file_string;

    // Possible other details to include in file
    if (0) {
        file_string += "EI Project ID, ";
        file_string += EI_CLASSIFIER_PROJECT_ID;
        file_string += "\nEI Project owner, ";
        file_string += EI_CLASSIFIER_PROJECT_OWNER;
        file_string += "\nEI Project name, ";
        file_string += EI_CLASSIFIER_PROJECT_NAME;
        file_string += "\nEI Project deploy version, ";
        file_string += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
    }

    // Column headers
    file_string += "\n\nHour:Min:Sec Day, Month Date Year";

    for (auto i = 0; i < EI_CLASSIFIER_NN_OUTPUT_COUNT; i++) {
        file_string += " ,";
        file_string += this->get_ei_classifier_inferencing_categories(i);
    }

    file_string += "\n";

    fputs(file_string.c_str(), fp_result);
    fclose(fp_result);

    inference_result_file_SD_available = true;

    return 0;
}

int EdgeImpulse::save_inference_result_SD(String results_string) {
    if (inference_result_file_SD_available == false) {
        if (create_inference_result_file_SD() != 0)
            return -1;
    }

    FILE *fp_result = fopen(ei_results_filename.c_str(), "a");

    if (!fp_result) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    String file_string = timeObject.getTimeDate(false) + " " + results_string;

    fputs(file_string.c_str(), fp_result);
    fclose(fp_result);

    return 0;
}

int EdgeImpulse::enableSaveResultsToSD(const String& sessionFolder) {
    mSessionFolder = sessionFolder;
    save_ai_results_to_sd = true;
    return create_inference_result_file_SD();
}
void EdgeImpulse::disableSaveResultsToSD() {
    save_ai_results_to_sd = false;
    mSessionFolder = "";
}

