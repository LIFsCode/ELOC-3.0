/**
 * @file I2SMEMSSampler.h
 * @author The Authors
 * @brief
 * @version 0.1
 * @date 2025-01-08
 *
 * @copyright Copyright (c) 2025
 *
 */
#ifndef I2SMEMSSAMPLER_H
#define I2SMEMSSAMPLER_H

#include "WAVFileWriter.h"
#include "../../../include/ei_inference.h"
#include "../../../include/project_config.h"
#include <driver/i2s.h>

extern TaskHandle_t i2s_TaskHandler;
extern TaskHandle_t ei_TaskHandler;


class I2SMEMSSampler {
 private:

   /**
    * Ideally it would be possible to probe hardware to see if it's
    * running, but doesn't seem possible. Use this instead ..
   */
   bool i2s_installed_and_started = false;

   i2s_pin_config_t i2s_pins_config;
   i2s_port_t i2s_port = I2S_NUM_0;
   i2s_config_t i2s_config;

   int volume2_pwr = I2S_DEFAULT_VOLUME;
   WAVFileWriter *writer = nullptr;
   int32_t *raw_samples = nullptr;

   // Set some reasonable values as default
   uint32_t i2s_sampling_rate = I2S_DEFAULT_SAMPLE_RATE;
   uint32_t ei_sampling_freq = I2S_DEFAULT_SAMPLE_RATE;

   // Handle scenario where EI_CLASSIFIER_FREQUENCY != I2S sample rate
   // Will skip packing EI buffer at this rate
   // i.e. if I2S sample rate = 16000 Hz & EI_CLASSIFIER_FREQUENCY = 4000Hz
   // ei_skip_rate = 4
   int ei_skip_rate = 1;
   inference_t *inference;

   /**
    * The number of SAMPLES (i.e. not bytes) to read in the read() thread
    */
   size_t i2s_samples_to_read;

   /**
    * Stop read thread by setting to false
    */
   bool enable_read = true;

   /**
    * @brief Read I2S samples from DMA buffer
    * @return The number of SAMPLES (i.e. not bytes) read
    */
   virtual int read();

   /**
    *
    */
   virtual void start_read_thread();

   /**
    *
    */
   static void start_read_thread_wrapper(void * _this);

    /**
     * @brief Configure the I2S pins
     *
     * @return true
     * @return false
     */
    virtual bool configureI2S();

 public:
    I2SMEMSSampler();

    /**
     * @brief Install and start the I2S driver
     *
     * @return esp_err_t
     */
    virtual esp_err_t install_and_start();

    virtual bool is_i2s_installed_and_started() { return i2s_installed_and_started; }

    virtual void init(i2s_port_t _i2s_port, const i2s_pin_config_t &_i2s_pins_config, i2s_config_t _i2s_config, int _volume2_pwr = I2S_DEFAULT_VOLUME);

    virtual esp_err_t uninstall();

    virtual ~I2SMEMSSampler();

    /**
     * @brief Zero the appropiate TX DMA buffer for the I2S port
     * @note This is required to prevent a pop at the start of the audio
     * @note This is only required for TX (i.e. recording)
     * @return esp_err_t
    */
   virtual esp_err_t zero_dma_buffer(i2s_port_t i2sPort);

    /**
     * @brief Register an external WAVFileWriter
     * @note This object will fill the WAVFileWriter buffer during reads
     *
     * @param writer WAVFileWriter object
     * @return true success
     */
   virtual bool register_wavFileWriter(WAVFileWriter *writer);

    /**
     * @brief Deregister or remove the WAVFileWriter
     * @return true
     * @return false writer not previously registered
     */
   virtual bool deregister_wavFileWriter();

    /**
     * @brief Register a edge impulse inference structure
     * @note This object will fill the inference buffer during reads
     *
     * @param ext_inference the Edge Impulse inference structure
     * @param ext_ei_sampling_freq the sampling frequency of the Edge Impulse model
     * @return true success
    */
    virtual bool register_ei_inference(inference_t *ext_inference, int ext_ei_sampling_freq);

    /**
     *
    */
    virtual int start_read_task(int i2s_bytes_to_read);
};

#endif // I2SMEMSSAMPLER_H
