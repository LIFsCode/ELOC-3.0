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

#ifndef PIO_UNIT_TESTING
    // For unit testing
    #include <I2SMEMSSampler.h>
    extern I2SMEMSSampler *input;
#endif

// #include "../include/test_samples.h"



class EdgeImpulse {

private:

    typedef struct
    {
        int16_t *buffer;
        uint8_t buf_ready;
        uint32_t buf_count;
        uint32_t n_samples;
    } inference_t;

    /** Audio buffers, pointers and selectors */
    inference_t inference;
    static const uint32_t sample_buffer_size = 2048;
    signed short sampleBuffer[sample_buffer_size];
    bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
    int print_results;
    bool record_status;

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
     * @brief      Init inferencing struct and setup/start PDM
     *
     * @param[in]  n_samples  The n samples
     *
     * @return     { description_of_the_return_value }
     */
    bool microphone_inference_start(uint32_t n_samples);

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
     * @brief      Stop PDM and release buffers
     */
    void microphone_inference_end(void);

    /**
     * @brief      Start recording audio
     *
     * @param[in]  n_samples  The n samples
     *
     * @return     { description_of_the_return_value }
     */


};

#endif // _EDGE_IMPULSE_H_