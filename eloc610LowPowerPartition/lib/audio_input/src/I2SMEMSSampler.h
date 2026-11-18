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
#include <esp_timer.h>
#include <atomic>

extern TaskHandle_t i2s_TaskHandler;
extern TaskHandle_t ei_TaskHandler;

/**
 * Listener for the raw I2S buffers, see I2SMEMSSampler::set_raw_tap().
 * @param samples the 32-bit words exactly as read from I2S (24-bit data in the upper bits),
 *                i.e. before the volume shift. Only valid during the call.
 * @param count   number of samples
 * @param ctx     the pointer given to set_raw_tap()
 */
typedef void (*i2s_raw_tap_fn_t)(const int32_t *samples, size_t count, void *ctx);


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
    * Clip-warning throttling. At a wrong I2S clock every single read clips, which
    * floods the SD card log (and the log writes then starve the wav writer). Collect
    * the clip count and emit at most one warning per second.
    */
   int64_t clip_log_last_us = 0;
   uint32_t clip_accum = 0;
   uint32_t clip_reads = 0;

   /**
    * Raw sample tap (microphone self-test). Written by another task while the read task runs,
    * hence atomic. The context is written before the function pointer and never cleared, so the
    * read task cannot see a function without its context.
    */
   std::atomic<i2s_raw_tap_fn_t> raw_tap{nullptr};
   std::atomic<void *> raw_tap_ctx{nullptr};

   /**
    * DC offset removal filter for DMM-4026-B-I2S-R microphone
    * Uses exponential moving average to track DC component
    */
   #if defined(I2S_PUI_DMM_4026_B_I2S_R) && defined(I2S_DMM_4026_ENABLE_DC_FILTER)
   float dc_filter_state = 0.0f;
   static constexpr float DC_FILTER_ALPHA = I2S_DMM_4026_DC_FILTER_ALPHA;  // Filter coefficient from config
   uint32_t dc_filter_sample_count = 0;  // For debug logging
   #endif

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

    /**
     * @brief Choose the I2S clock source for the next install_and_start(). init() takes it from
     *        the config ("MicUseAPLL"); the main loop overrides it per recording mode.
     * @note  No effect on a driver that is already installed.
     */
    void set_use_apll(bool use) { i2s_config.use_apll = use; }
    bool get_use_apll() const { return i2s_config.use_apll; }

    /// The I2S sample rate set by init(), i.e. the rate the inference buffer is decimated from
    uint32_t get_i2s_sampling_rate() const { return i2s_sampling_rate; }

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
     * @brief Hand every raw I2S buffer to one listener, in addition to the wav writer and the
     *        inference buffer. Used by the microphone self-test (lib/Commands/src/MicTest.cpp).
     * @note The listener runs in the I2S read task (highest application priority), once per
     *       buffer: it must only copy the samples and return. It may still be called once after
     *       it has been removed, so its context must stay valid.
     * @param fn  listener, or nullptr to remove it
     * @param ctx passed back to the listener unchanged
     */
    void set_raw_tap(i2s_raw_tap_fn_t fn, void *ctx);

    /**
     *
    */
    virtual int start_read_task(int i2s_bytes_to_read);
};

#endif // I2SMEMSSAMPLER_H
