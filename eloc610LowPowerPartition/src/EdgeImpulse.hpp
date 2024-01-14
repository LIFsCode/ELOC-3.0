/**
 * @file EdgeImpulse.h
 * @brief Edge Impulse header file
 *
 */

#ifndef ELOC610LOWPOWERPARTITION_SRC_EDGEIMPULSE_HPP_
#define ELOC610LOWPOWERPARTITION_SRC_EDGEIMPULSE_HPP_

// If your target is limited in memory remove this macro to save 10K RAM
// But if you do results in errors: '.... insn does not satisfy its constraints'
#define EIDSP_QUANTIZE_FILTERBANK 0
#define I2S_DATA_SCALING_FACTOR 1

#include <functional>  // std::function
#include "esp_err.h"
#include "esp_log.h"
#include "ei_inference.h"  // inference_t
#include "project_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "utils/time_utils.hpp"

extern TaskHandle_t ei_TaskHandler;

#ifndef PIO_UNIT_TESTING
// For unit testing
#include <I2SMEMSSampler.h>
extern I2SMEMSSampler *input;
#endif

#include "edge-impulse-sdk/dsp/numpy_types.h"
#include "edge-impulse-sdk/classifier/ei_classifier_types.h"  // Need for typedef struct ei_signal_t* signal;

class EdgeImpulse {
 public:
    /**
     * @brief Note ideal but need access from I2SMEMSSampler
     *        & main.cpp to the buffers in this struct
     */
    inference_t inference;

    enum class Status { not_running = 0, running = 1};

 private:
    bool debug_nn = false;  // Set this to true to see e.g. features generated from the raw signal
    Status status = Status::not_running;

    /**
     * @brief This callback allows the classifier to be run from main.cpp
     *        This is required due to namespace issues, static implementations etc..
     */
    std::function<void()> callback;

    /**
     * @brief Detecting time since last activated
     */
    uint32_t detectingTime_secs = 0;

    /**
     * @brief Detecting time time since boot
     */
    uint32_t totalDetectingTime_secs = 0;

 public:
    /**
     * @brief Construct a new Edge Impulse object
     *
     * @param i2s_sample_rate Sample rate of I2S microphone
     */
    explicit EdgeImpulse(int i2s_sample_rate);

    /**
     * @brief Output to console the inferencing settings
     * @note summary of inferencing settings (from model_metadata.h)
     * @note Note not EI code
     */
    void output_inferencing_settings();

    /**
     * This function is repeatedly called by capture_samples()
     * When sufficient samples are collected:
     *  1. inference.buf_ready = 1
     *  2. microphone_inference_record() is unblocked
     *  3. classifier is run in main loop()
     */
    void audio_inference_callback(uint32_t n_bytes);

    /**
     * This is initiated by a task created in microphone_inference_record_start()
     * When periodically called it:
     *  1. reads data from I2S DMA buffer,
     *  2. scales it
     *  3. Calls audio_inference_callback()
     */
    void capture_samples(void *arg);

    /**
     * @brief   Init inferencing struct and setup/start PDM
     * @param   n_samples number of samples of the buffer required.
     * @note    For continuous inferencing = EI_CLASSIFIER_SLICE_SIZE
     *          For non-continuous inferencing = EI_CLASSIFIER_RAW_SAMPLE_COUNT
     * @return  true if successful
     */
    bool buffers_setup(uint32_t n_samples);

    /**
     * @brief  Wait on new data.
     *         Blocking function.
     *         Unblocked by audio_inference_callback() setting inference.buf_ready
     *
     * @return     True when finished
     */
    bool microphone_inference_record(void);

    /**
     * Get raw audio signal data
     */
    int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);

    /**
     * @brief Free buffers allocated in buffers_setup()
     */
    void free_buffers(void);

    /**
     * @brief Get the ei running status object
     * 
     * @return true running
     * @return false not running
     */
    enum Status get_status() const {
        return status;
    }

    /**
     * @brief Set the ei running status object
     * 
     * @param newStatus true or false
     */
    void set_status(enum Status newStatus) {
        status = newStatus;
    }

    /**
     * @brief Get the Inference object
     * @deprecated ??
     * @return inference_t& 
     */
    inference_t &getInference() {
        return inference;
    }

    /**
     * @brief Init static vars
     * @note: Only for Continuous inferencing!
     */
    void run_classifier_init();

    /**
     * @brief Start a continuous inferencing task
     * 
     * @param signal 
     * @param result 
     * @return EI_IMPULSE_ERROR 
     */
    EI_IMPULSE_ERROR run_classifier_continuous(ei::signal_t *signal, ei_impulse_result_t *result);

    /**
     * @brief Start a non-continuous inferencing task
     * 
     * @param signal 
     * @param result 
     * @return EI_IMPULSE_ERROR 
     */
    EI_IMPULSE_ERROR run_classifier(ei::signal_t *signal, ei_impulse_result_t *result);

    /**
     * @brief Start a continuous inferencing thread 
     */
    void ei_thread();

    /**
     * @brief Wrapper for ei_thread()
     * @param _this 
     */
    static void start_ei_thread_wrapper(void *_this);

    /**
     * @brief Start a continuous inferencing task
     * 
     * @return ESP_OK on success
     */
    esp_err_t start_ei_thread(std::function<void()> callback);

    /**
     * @brief Get the detectingTime
     * @note This is the time since last activated
     * @return float 
     */
    float get_detectingTime_hr() const {
        return detectingTime_secs / 3600.0;
    }

    /**
     * @brief Get the totalDetectingTime
     * @note  This is the total time since boot
     * @return float 
     */
    float get_totalDetectingTime_hr() const {
        return totalDetectingTime_secs / 3600.0;
    }
};

#endif  //  ELOC610LOWPOWERPARTITION_SRC_EDGEIMPULSE_HPP_
