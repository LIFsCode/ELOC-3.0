#include "I2SMEMSSampler.h"
#include "soc/i2s_reg.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "I2SMEMSSampler";

//extern bool ai_run_enable;

I2SMEMSSampler::I2SMEMSSampler() {
}

void I2SMEMSSampler::init(i2s_port_t i2s_port, const i2s_pin_config_t &i2s_pins, i2s_config_t i2s_config, int bitShift,
                            bool listenOnly, bool fixSPH0645) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    I2SSampler::init(i2s_port, i2s_config);
    m_i2sPins = i2s_pins;
    mBitShift = bitShift;
    mListenOnly = listenOnly;
    m_fixSPH0645 = fixSPH0645;
    i2s_sampling_rate = i2s_config.sample_rate;
    i2s_samples_to_read = I2S_DEFAULT_SAMPLE_RATE;  // Reasonable default
    writer = nullptr;
    inference = {};

    if (1) {
        ESP_LOGI(TAG, "i2s_port = %d", i2s_port);
        ESP_LOGI(TAG, "i2s_sampling_rate = %d", i2s_sampling_rate);
        ESP_LOGI(TAG, "mBitShift = %d", mBitShift);
        ESP_LOGI(TAG, "mListenOnly = %d", mListenOnly);
        ESP_LOGI(TAG, "m_fixSPH0645 = %d", m_fixSPH0645);
    }
}

bool I2SMEMSSampler::configureI2S() {
    if (m_fixSPH0645) {
        // Undocumented (?!) manipulation of I2S peripheral registers
        // to fix MSB timing issues with some I2S microphones
        REG_SET_BIT(I2S_TIMING_REG(m_i2sPort), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(m_i2sPort), I2S_RX_MSB_SHIFT);
    }

    auto ret = i2s_set_pin(m_i2sPort, &m_i2sPins);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Func: %s, error i2s_set_pin", __func__);
    }

    return ret;
}

esp_err_t I2SMEMSSampler::zero_dma_buffer(i2s_port_t i2sPort) {
    auto ret = i2s_zero_dma_buffer((i2s_port_t) i2sPort);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Func: %s, i2s_zero_dma_buffer", __func__);
    }

    return ret;
}

bool I2SMEMSSampler::register_wavFileWriter(WAVFileWriter *ext_writer) {
    writer = ext_writer;

    if (writer == nullptr) {
        ESP_LOGE(TAG, "register_wavFileWriter() - WAVFileWriter not registered");
        return false;
    } else {
        return true;
    }
}

bool I2SMEMSSampler::deregister_wavFileWriter() {
    if (writer == nullptr) {
        ESP_LOGE(TAG, "deregister_wavFileWriter() - WAVFileWriter not previously registered");
        return false;
    } else {
        writer = nullptr;
        return true;
    }
}

bool I2SMEMSSampler::register_ei_inference(inference_t *ext_inference, int ext_ei_sampling_freq) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    inference = ext_inference;
    ei_sampling_freq = ext_ei_sampling_freq;
    ei_skip_rate = i2s_sampling_rate / ei_sampling_freq;

    ESP_LOGV(TAG, "i2s_sampling_rate = %d, ei_sampling_freq = %d, ei_skip_rate = %d", i2s_sampling_rate, ei_sampling_freq, ei_skip_rate);

    return true;
}

int I2SMEMSSampler::read()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    #ifdef ENABLE_AUTOMATIC_GAIN_ADJUSTMENT
        // Automatic gain defintions
        #define LAST_GAIN_INCREASE_THD 30   // How often can gain be increased?
        #define SAMPLE_HIGH_COUNT 0.1       // % of samples before gain is decreased
        #define SAMPLE_LOW_COUNT 0.5        // % of samples before gain is increased
        #define SAMPLE_HIGH_LEVEL_THD 0.95  // SAMPLE_HIGH_COUNT reach/ exceed this volume -> decrease gain
        #define SAMPLE_LOW_LEVEL_THD 0.5    // SAMPLE_LOW_COUNT fail to reach/ exceed this volume -> increase gain
    #endif

    /**
     * Flag if there's buffer overruns - for debug purposes
     *
     * @note: The buffers are designed to overrun when with the
     * inference is not running or wav recording is not in progress.
     * It is the responsibility of the consuming task (i.e. inference or
     * wav writer) to cope with this situation.
    */
    bool writer_buffer_overrun = false;
    bool inference_buffer_overrun = false;

    // Count how many times the sample exceeds the 16 bit range
    auto sound_clip_count = 0;

    #ifdef ENABLE_AUTOMATIC_GAIN_ADJUSTMENT
        // How many samples exceed high threshold?
        auto sample_high_count = 0;

        // How many samples exceed low threshold?
        auto sample_low_count = 0;

        // When was the volume last increased?
        static auto last_gain_increase = 0;

        // Used to notification to serial monitor when volume is changed
        bool volume_change = false;
    #endif

    // Allocate a buffer of BYTES sufficient for sample size

    #ifdef I2S_BUFFER_IN_PSRAM
        int32_t *raw_samples = (int32_t *)heap_caps_malloc((sizeof(int32_t) * i2s_samples_to_read), MALLOC_CAP_SPIRAM);
    #else
        // Use MALLOC_CAP_DMA to allocate in DMA-able memory
        // MALLOC_CAP_32BIT to allocate in 32-bit aligned memory
        int32_t *raw_samples = (int32_t *)heap_caps_malloc((sizeof(int32_t) * i2s_samples_to_read), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    #endif

    if (raw_samples == NULL) {
        ESP_LOGE(TAG, "Could not allocate memory for samples");
        return 0;
    }

    size_t bytes_read = 0;

    /*
    How often do we need to skip a sample to place into the EI buffer?
    This handles scenario where  EI_CLASSIFIER_FREQUENCY != I2S sample rate
    Will skip packing EI buffer at this rate. e.g:
    if EI_CLASSIFIER_FREQUENCY = 4000Hz & I2S sample rate = 16000Hz > ei_skip_rate = 4
    if EI_CLASSIFIER_FREQUENCY = 8000Hz & I2S sample rate = 16000Hz > ei_skip_rate = 2
    if EI_CLASSIFIER_FREQUENCY = 16000Hz & I2S sample rate = 16000Hz > ei_skip_rate = 1

    TODO: Test what happens when not an even multiple!
    */
    auto skip_current = ei_skip_rate;  // Make sure first sample is saved, then skip if needed

    auto result = i2s_read(m_i2sPort, raw_samples, sizeof(int32_t) * i2s_samples_to_read, &bytes_read, portMAX_DELAY);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Error in I2S read : %d", result);
    }

    // Convert the number of bytes into the number of samples
    int samples_read = bytes_read / sizeof(int32_t);
    ESP_LOGV(TAG, "samples_read = %d", samples_read);

    if (bytes_read == 0) {
      ESP_LOGE(TAG, "Error in I2S read : %d", bytes_read);
    } else {
        if (bytes_read < sizeof(int32_t) * i2s_samples_to_read) {
            ESP_LOGW(TAG, "Partial I2S read");
        }

        #ifdef VISUALIZE_WAVEFORM
            int64_t total_raw_sample = 0;
            int64_t total_shifted_sample_32bit = 0;
            int64_t total_processed_sample_32bit = 0;
            int64_t total_processed_sample_16bit = 0;
        #endif

        /**
         * @note This bit shift = (corrected bit position of sample (loaded with MSB starting at bit 32) -
         *                         increase volume by shifting left (each shift left doubles volume))
         *       15th Nov, allow -ve volume shift, i.e. decrease volume
         */
        auto overall_bit_shift = (32 - I2S_BITS_PER_SAMPLE) - volume_shift;
        ESP_LOGV(TAG, "volume_shift = %d, overall_bit_shift = %d", volume_shift, overall_bit_shift);

        for (auto i = 0; i < samples_read; i++) {
            /**
             * I2S mics seem to be generally 16 or 24 bit, 2's complement, MSB first.
             * This data needs to be shifted right to correct position.
             * e.g. using a raw sample from a 24 bit mic fed into a 32 bit data type as an example:
             *
             * M = Most significant data (MSB), D = data, X = discarded
             *
             * Bit pos:     31 | 30 | 29 | 28 | 27 | 26 | 25 | 24 | 23 | 22 | 21 | 20 | 19 | 18 | 17 | 16 | 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
             * Raw sample:  M    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   X   X   X   X   X   X   X   X
             * Shifted:     0    0    0    0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   D   D   D   X   X   X   X   X   X   X   X   X
             *                                                                                                                                                                        ^^^^^^^^ DISCARDED ^^^^^^^
             * Note: for esp-idf/ gcc, the sign bit seems to be preserved.
             * But this samples volume is too low, so shift left to increase volume
             *
             * volume_shift: 31 | 30 | 29 | 28 | 27 | 26 | 25 | 24 | 23 | 22 | 21 | 20 | 19 | 18 | 17 | 16 | 15 | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
             * Value -1:     0    0    0    0    0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   D   D   D   D
             * Value 0:      0    0    0    0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   D   D   D  ^^^^^^^^ DISCARDED ^^^^^^^
             * Value 1:      0    0    0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   D   D   0
             * Value 2:      0    0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   D   0   0
             * Value 3:      0    0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   D   0   0   0
             * Value 4:      0    0    0    0    M    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D    D   D   D   D   D   D   0   0   0   0
             */

            #ifdef VISUALIZE_WAVEFORM
                int32_t shifted_sample = ((raw_samples[i] >> 8));
                int32_t processed_sample_32bit = shifted_sample << volume_shift;
                int16_t processed_sample_16bit = processed_sample_32bit;
            #else
                int32_t processed_sample_32bit = raw_samples[i] >> overall_bit_shift;
                int16_t processed_sample_16bit = processed_sample_32bit;
            #endif

            // Have we exceeded the 16 bit range?
            static_assert(sizeof(processed_sample_16bit) == 2, "check datatype of processed_sample_16bit is int16_t");
            static_assert(INT16_MAX == 32767, "INT16_MAX != 32767");
            static_assert(INT16_MIN == -32768, "INT16_MIN != -32768");

            if (processed_sample_32bit < INT16_MIN) {
                processed_sample_16bit = INT16_MIN;
                sound_clip_count++;
            } else if (processed_sample_32bit > INT16_MAX) {
                processed_sample_16bit = INT16_MAX;
                sound_clip_count++;
            }

            #ifdef ENABLE_AUTOMATIC_GAIN_ADJUSTMENT
            if (abs(processed_sample_16bit) > (INT16_MAX * SAMPLE_HIGH_LEVEL_THD)) {
                sample_high_count++;
            } else if (abs(processed_sample_16bit) > (INT16_MAX * SAMPLE_LOW_LEVEL_THD)) {
                sample_low_count++;
            }
            #endif

            if (writer != nullptr) {
                // Store into wav file buffer
                writer->buffers[writer->buf_select][writer->buf_count++] = processed_sample_16bit;

                if (writer->buf_count >= writer->buffer_size_in_samples) {
                    // Swap buffers and set buf_ready
                    writer->buf_select ^= 1;
                    writer->buf_count = 0;

                    // If recording a wav file & overrun => flag
                    if (writer->wav_recording_in_progress && writer->buf_ready == 1) {
                        writer_buffer_overrun = true;
                    }
                    writer->buf_ready = 1;

                    // Note: Trying to write to SD card here causes poor performance

                    // Wake up the write thread
                    if (writer->get_enable_wav_file_write() == true && i2s_TaskHandler != NULL)
                        xTaskNotify(i2s_TaskHandler, (0), eNoAction);
                }
            }

            #ifdef EDGE_IMPULSE_ENABLED

            // Check buffer exists
            if (inference->buffers[inference->buf_select] != nullptr) {
                // Store into edge-impulse buffer taking into requirement to skip if necessary
                if (skip_current >= ei_skip_rate) {
                    ESP_LOGV(TAG, "Saving sample, skip_current = %d", skip_current);

                    inference->buffers[inference->buf_select][inference->buf_count++] = processed_sample_16bit;
                    skip_current = 1;

                    if (inference->buf_count >= inference->n_samples) {
                        inference->buf_select ^= 1;
                        inference->buf_count = 0;

                        // If running inference & buffer overrun => flag
                        if (inference->status_running == true && inference->buf_ready == 1) {
                            inference_buffer_overrun = true;
                        }

                        inference->buf_ready = 1;
                        if (inference->status_running == true && ei_TaskHandler != NULL) {
                            ESP_LOGV(TAG, "Notifying inference task");
                            xTaskNotify(ei_TaskHandler, (0), eNoAction);
                        }
                    }

                } else {
                    ESP_LOGV(TAG, "Not saving sample, skip_current = %d", skip_current);
                    skip_current++;
                }
            }

            #endif  // EDGE_IMPULSE_ENABLED

            #ifdef VISUALIZE_WAVEFORM
                total_raw_sample += raw_samples[i];
                total_processed_sample_16bit += processed_sample_16bit;
                total_shifted_sample_32bit += shifted_sample;
                total_processed_sample_32bit += processed_sample_32bit;

                // Print out every 125ms/ 8 times a second
                if (i % (i2s_sampling_rate / 8) == 0) {
                    // printf(">avg_raw_sample:%f\n", float(total_raw_sample/ samples_read));
                    // printf(">avg_shifted_sample:%f\n", float(total_shifted_sample_32bit/ samples_read));
                    // printf(">avg_processed_sample_32bit:%f\n", float(total_processed_sample_32bit/ samples_read));
                    printf(">avg_processed_sample:%f\n", float(total_processed_sample_16bit/ samples_read));
                }
            #endif
        }
    }

    if (0) {
        ESP_LOGI(TAG, "writer->buf_count = %d", writer->buf_count);
        ESP_LOGI(TAG, "writer->buf_select = %d", writer->buf_select);
        ESP_LOGI(TAG, "writer->buf_ready = %d", writer->buf_ready);

        #ifdef EDGE_IMPULSE_ENABLED

        ESP_LOGI(TAG, "inference.buf_count = %d", inference->buf_count);
        ESP_LOGI(TAG, "inference.buf_select = %d", inference->buf_select);
        ESP_LOGI(TAG, "inference.buf_ready = %d", inference->buf_ready);

        #endif  // EDGE_IMPULSE_ENABLED
    }

    if (writer_buffer_overrun == true) {
        ESP_LOGW(TAG, "wav buffer overrun");
    }

    #ifdef EDGE_IMPULSE_ENABLED

    if (inference_buffer_overrun == true) {
        ESP_LOGW(TAG, "inference buffer overrun");
    }

    #endif  // EDGE_IMPULSE_ENABLED

    if (sound_clip_count > 0) {
        ESP_LOGW(TAG, "Audio sample clips occurred %d times", sound_clip_count);
    }

    // Automatic gain adjustment
    #ifdef ENABLE_AUTOMATIC_GAIN_ADJUSTMENT
    if (sample_high_count > SAMPLE_HIGH_COUNT * samples_read) {
        // TODO(oohehir): check when volume_shift is -ve
        if (volume_shift > 0) {
            volume_shift = volume_shift >> 1;
        }
        last_gain_increase = 0;
        volume_change = true;
        ESP_LOGV(TAG, "sample_high_count = %d, decreasing gain to %d", sample_high_count, volume_shift);
    } else if (sample_low_count < SAMPLE_LOW_COUNT * samples_read) {
        // Increase gain if not recently increased
        if (last_gain_increase > LAST_GAIN_INCREASE_THD) {
            volume_shift = volume_shift << 1;
            last_gain_increase = 0;
            volume_change = true;
            ESP_LOGV(TAG, "sample_low_count = %d, increasing gain to %d", sample_low_count, volume_shift);
        } else {
            last_gain_increase++;
            ESP_LOGV(TAG, "sample_low_count = %d, last_gain_increase %d", sample_low_count, last_gain_increase);
        }
    }

    // Boundary check
    if (volume_shift < -2) {
        volume_shift = -2;
    } else if (volume_shift > 4) {
        volume_shift = 4;
    }

    if (volume_change == true) {
        ESP_LOGI(TAG, "volume_shift = %d", volume_shift);
    }

    #endif

    #ifdef I2S_BUFFER_IN_PSRAM
        heap_caps_free(raw_samples);
    #else
        free(raw_samples);
    #endif

    return samples_read;
}

void I2SMEMSSampler::start_read_thread()
{
    while (enable_read) {
        auto samples_read = this->read();

        if (samples_read != i2s_samples_to_read) {
            ESP_LOGW(TAG, "samples_read = %d, i2s_samples_to_read = %d", samples_read, i2s_samples_to_read);
        }
    }

    vTaskDelete(NULL);
}

void I2SMEMSSampler::start_read_thread_wrapper(void * _this) {
  ((I2SMEMSSampler*)_this)->start_read_thread();
}

int I2SMEMSSampler::start_read_task(int i2s_samples_to_read) {
  // logical right shift divides a number by 2, throwing out any remainders
  // Need to divide by 2 because reading bytes into a int16_t buffer
  this->i2s_samples_to_read = i2s_samples_to_read;

  // Stack 1024 * X - experimentally determined
  int ret = xTaskCreatePinnedToCore(this->start_read_thread_wrapper, "I2S read", 1024 * 4, this, TASK_PRIO_I2S, NULL, TASK_I2S_CORE);

  return ret;
}

I2SMEMSSampler::~I2SMEMSSampler() {
    }
