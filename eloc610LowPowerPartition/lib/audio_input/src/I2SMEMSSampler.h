#pragma once

#include "I2SSampler.h"
#include "WAVFileWriter.h"
#include "../../../include/ei_inference.h"
#include "../../../include/project_config.h"


class I2SMEMSSampler : public I2SSampler {
 private:
    i2s_pin_config_t m_i2sPins;
    bool m_fixSPH0645;
    int  mBitShift = I2S_DEFAULT_BIT_SHIFT;  // TODO: depreciated?? Using I2S_DEFAULT_BIT_SHIFT + I2S_DEFAULT_VOLUME instead from config.h
    bool mListenOnly = false;
    WAVFileWriter *writer = nullptr;
    const TaskHandle_t* mWriterTaskHandle = nullptr;

    int32_t *raw_samples = nullptr;

    /**
     * @brief The volume shift is used to scale the adjust the volume
     */
    int volume_shift = I2S_DEFAULT_VOLUME;

    // Set some reasonable values as default
    uint32_t i2s_sampling_rate = I2S_DEFAULT_SAMPLE_RATE;
    uint32_t ei_sampling_freq = I2S_DEFAULT_SAMPLE_RATE;

    // Handle scenario where EI_CLASSIFIER_FREQUENCY != I2S sample rate
    // Will skip packing EI buffer at this rate
    // i.e. if I2S sample rate = 16000 Hz & EI_CLASSIFIER_FREQUENCY = 4000Hz
    // ei_skip_rate = 4
    int ei_skip_rate = 1;
    inference_t *inference;
    const TaskHandle_t* mInferenceTaskHandle = nullptr;

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
    int read() override;

    /**
     *
    */
    virtual void start_read_thread();

    /**
     *
    */
    static void start_read_thread_wrapper(void * _this);

 protected:
    bool configureI2S() override;

 public:
    I2SMEMSSampler();

    virtual void init(
        i2s_port_t i2s_port,
        const i2s_pin_config_t &i2s_pins,
        i2s_config_t i2s_config,
        int  bitShift = I2S_DEFAULT_BIT_SHIFT,     // TODO: depreciated?? Using I2S_DEFAULT_BIT_SHIFT + I2S_DEFAULT_VOLUME instead from config.h
        bool listenOnly = false,
        bool fixSPH0645 = false);

    virtual ~I2SMEMSSampler();

    virtual void setBitShift(int bitShift) {
        mBitShift = bitShift;
    }
    virtual void setListenOnly(bool listenOnly) {
        mListenOnly = listenOnly;
    }

    /**
     * @brief Zero the appropiate TX DMA buffer for the I2S port
     * @note This is required to prevent a pop at the start of the audio
     * @note This is only required for TX (i.e. recording)
     * @return esp_err_t
    */
    esp_err_t zero_dma_buffer(i2s_port_t i2sPort) override;

    /**
     * @brief Register an external WAVFileWriter
     * @note This object will fill the WAVFileWriter buffer during reads
     *
     * @param writer WAVFileWriter object
     * @return true success
     */
    virtual bool register_wavFileWriter(WAVFileWriter *writer, const TaskHandle_t* taskHandle);

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
    virtual bool register_ei_inference(inference_t *ext_inference, int ext_ei_sampling_freq, const TaskHandle_t* taskHandle);

    /**
     *
    */
    virtual int start_read_task(int i2s_bytes_to_read);
};
