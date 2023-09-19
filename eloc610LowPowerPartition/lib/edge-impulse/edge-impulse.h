/**
 * @file edge-impulse.h
 * @brief Edge Impulse header file
 * 
*/

#ifndef _EDGE_IMPULSE_H_
#define _EDGE_IMPULSE_H_

#include "esp_err.h"
#include "esp_log.h"
#include "I2SMEMSSampler.h"

/**
 * This function is repeatedly called by capture_samples()
 * When sufficient samples are collected:
 *  1. inference.buf_ready = 1
 *  2. microphone_inference_record() is unblocked
 *  3. classifier is run in main loop()
 */
static void audio_inference_callback(uint32_t n_bytes);

/**
 * This is initiated by a task created in microphone_inference_record_start()
 * When periodically called it:
 *  1. reads data from I2S DMA buffer,
 *  2. scales it
 *  3. Calls audio_inference_callback()
 */
static void capture_samples(void *arg);

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
static bool microphone_inference_start(uint32_t n_samples);

/**
 * @brief  Wait on new data.
 *         Blocking function.
 *         Unblocked by audio_inference_callback() setting inference.buf_ready
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void);

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr);

/**
 * @brief      Stop PDM and release buffers
 */
static void microphone_inference_end(void);

/**
 * @brief      Start recording audio
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */


#endif // _EDGE_IMPULSE_H_