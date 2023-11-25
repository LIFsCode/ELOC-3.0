/**
 * @file EdgeImpulse.h
 * @brief Edge Impulse header file
 *
 */

#ifndef _EDGE_IMPULSE_H_
#define _EDGE_IMPULSE_H_

// If your target is limited in memory remove this macro to save 10K RAM
// But if you do results in errors: '.... insn does not satisfy its constraints'
#define EIDSP_QUANTIZE_FILTERBANK 0
#define I2S_DATA_SCALING_FACTOR 1

#include "esp_err.h"
#include "esp_log.h"

#include "ei_inference.h" // inference_t

#ifndef PIO_UNIT_TESTING
// For unit testing
#include <I2SMEMSSampler.h>
extern I2SMEMSSampler *input;
#endif

// #include "../include/test_samples.h"

#include "edge-impulse-sdk/dsp/numpy_types.h"
#include "edge-impulse-sdk/classifier/ei_classifier_types.h" // Need for typedef struct ei_signal_t* signal;

class EdgeImpulse
{

public:
    /**
     * @brief Note ideal but need access from I2SMEMSSampler
     *        & main.cpp to the buffers in this struct
     */
    inference_t inference;

private:
    // static const uint32_t sample_buffer_size = 2048;
    // signed short sampleBuffer[sample_buffer_size];
    bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
    int print_results = 0;
    bool ei_running_status = false;

public:
    /**
     * @brief Construct a new Edge Impulse object
     *
     * @param i2s_sample_rate Sample rate of I2S microphone
     */
    EdgeImpulse(int i2s_sample_rate);

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
    bool get_ei_running_status() const
    {
        return ei_running_status;
    }

    /**
     * @brief Set the ei running status object
     * 
     * @param newStatus true or false
     */
    void set_ei_running_status(bool newStatus)
    {
        ei_running_status = newStatus;
    }

    /**
     * @brief Get the Inference object
     * @deprecated ??
     * @return inference_t& 
     */
    inference_t &getInference()
    {
        return inference;
    }

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
};

#endif // _EDGE_IMPULSE_H_