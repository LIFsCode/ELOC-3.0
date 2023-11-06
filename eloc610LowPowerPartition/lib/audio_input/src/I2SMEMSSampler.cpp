#include "I2SMEMSSampler.h"
#include "soc/i2s_reg.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "I2SMEMSSampler";

I2SMEMSSampler::I2SMEMSSampler(
    i2s_port_t i2s_port,
    i2s_pin_config_t &i2s_pins,
    i2s_config_t i2s_config,
    int bitShift,
    bool listenOnly,
    bool fixSPH0645) :  I2SSampler(i2s_port, i2s_config),
                        mBitShift(bitShift),
                        mListenOnly(listenOnly)
{
    m_i2sPins = i2s_pins;
    m_fixSPH0645 = fixSPH0645;

    i2s_sampling_rate = i2s_config.sample_rate;

    writer = nullptr;
    inference = {};

    if(1){
        ESP_LOGI(TAG, "i2s_port = %d", i2s_port);
        ESP_LOGI(TAG, "i2s_sampling_rate = %d", i2s_sampling_rate);
        ESP_LOGI(TAG, "mBitShift = %d", mBitShift);
        ESP_LOGI(TAG, "mListenOnly = %d", mListenOnly);
        ESP_LOGI(TAG, "m_fixSPH0645 = %d", m_fixSPH0645);
    }
    
}

bool I2SMEMSSampler::configureI2S()
{
    if (m_fixSPH0645)
    {
        // Undocumented (?!) manipulation of I2S peripheral registers
        // to fix MSB timing issues with some I2S microphones
        REG_SET_BIT(I2S_TIMING_REG(m_i2sPort), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(m_i2sPort), I2S_RX_MSB_SHIFT);
    }

    auto ret = i2s_set_pin(m_i2sPort, &m_i2sPins);

    if (ret != ESP_OK){
        ESP_LOGE(TAG, "Func: %s, error i2s_set_pin", __func__);
    }

    return ret;
}

bool I2SMEMSSampler::zero_dma_buffer(i2s_port_t i2sPort)
{
    auto ret = i2s_zero_dma_buffer((i2s_port_t) i2sPort);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Func: %s, i2s_zero_dma_buffer", __func__);
    }

    return ret;
}

bool I2SMEMSSampler::register_wavFileWriter(WAVFileWriter *ext_writer){
    
    writer = ext_writer;

    if (writer == nullptr){
        ESP_LOGE(TAG, "register_wavFileWriter() - WAVFileWriter not registered");
        return false;
    }else{
        return true;
    }
}

bool I2SMEMSSampler::deregister_wavFileWriter(){
    
    if (writer == nullptr){
        ESP_LOGE(TAG, "deregister_wavFileWriter() - WAVFileWriter not previously registered");
        return false;
    }else{
        writer = nullptr;
        return true;
    }

}

bool I2SMEMSSampler::register_ei_inference(inference_t *ext_inference, int ext_ei_sampling_freq){

    ESP_LOGV(TAG, "Func: %s", __func__);

    inference = ext_inference;
    ei_sampling_freq = ext_ei_sampling_freq;
    ei_skip_rate = i2s_sampling_rate / ei_sampling_freq;

    ESP_LOGV(TAG, "i2s_sampling_rate = %d, ei_sampling_freq = %d, ei_skip_rate = %d", i2s_sampling_rate, ei_sampling_freq, ei_skip_rate);

    return true;
}

int I2SMEMSSampler::read(int count)
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    #define I2S_VOLUME_SCALING_FACTOR 8             // Increase volume by this factor

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
    
    // Allocate a buffer of BYTES sufficient for sample size
    int32_t *raw_samples = (int32_t *)malloc(sizeof(int32_t) * count);
    //int32_t *raw_samples = (int32_t *)heap_caps_malloc((sizeof(int32_t) * count), MALLOC_CAP_SPIRAM);

    if (raw_samples == NULL)
    {
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
    auto skip_current = 1;
    
    i2s_read(m_i2sPort, raw_samples, sizeof(int32_t) * count, &bytes_read, portMAX_DELAY);
    
    // samples_read = dma_buf_len??
    // samples_read = number of 32 bit samples read = 1024
    // bytes_read = dma_buf_len * (bits_per_sample/8) = 4096
    
    int samples_read = bytes_read / sizeof(int32_t);

    // ESP_LOGI(TAG, "samples_read = %d", samples_read); 

    if (bytes_read <= 0) {
      ESP_LOGE(TAG, "Error in I2S read : %d", bytes_read);
    }
    else 
    {
        if (bytes_read < sizeof(int32_t) * count) {
            ESP_LOGW(TAG, "Partial I2S read");
        }

        for (auto i = 0; i < samples_read; i++)
        {   
            // The ELOC 3.2 board uses Uses TDK/ INVENSENSE ICS-43434 mic
            // The Data Format is I2S, 24-bit, 2’s compliment, MSB first.
            // The data precision is 24 bits
            // Note! Scale data, then shift, otherwise lose lower end volume?
            // shift 11 - tiny sound
            // shift 8 - distortion
            // shift 14 - good but low volume, needs scaling of at least 5
            // shift 16 - very low volume, needs scaling of at least 10
            int16_t processed_sample = ((raw_samples[i] >> mBitShift) * I2S_VOLUME_SCALING_FACTOR);

            // Store into wav file buffer
            writer->buffers[writer->buf_select][writer->buf_count++] = processed_sample;
            
            if(writer != nullptr){
                /*
                 * @warning If writer == nullptr (file not created) these buffers won't exist!
                 */ 
                if (writer->buf_count >= writer->buffer_size){
                    // Swap buffers and set buf_ready
                    writer->buf_select ^= 1;
                    writer->buf_count = 0;

                    // Flag overrun for later
                    if (writer->buf_ready == 1){
                        writer_buffer_overrun = true;
                    }
                    writer->buf_ready = 1;

                    // Note: Trying to write to SD card here causes poor performance
                }
            }

            // Store into edge-impulse buffer taking into requirement to skip if necessary
            if (skip_current >= ei_skip_rate){
                ESP_LOGV(TAG, "Saving sample, skip_current = %d", skip_current);

                inference->buffers[inference->buf_select][inference->buf_count++] = processed_sample;
                skip_current = 1;

                if (inference->buf_count >= inference->n_samples)
                {
                inference->buf_select ^= 1;
                inference->buf_count = 0;

                // Flag overrun for later
                if(inference->buf_ready == 1){
                    inference_buffer_overrun = true;
                }
                
                inference->buf_ready = 1;
                }
            }
            else{
                ESP_LOGV(TAG, "Not saving sample, skip_current = %d", skip_current);
                skip_current++;
            }
        }
    }

    if(0){
        ESP_LOGI(TAG, "writer->buf_count = %d", writer->buf_count);
        ESP_LOGI(TAG, "writer->buf_select = %d", writer->buf_select);
        ESP_LOGI(TAG, "writer->buf_ready = %d", writer->buf_ready);
        
        ESP_LOGI(TAG, "inference.buf_count = %d", inference->buf_count);
        ESP_LOGI(TAG, "inference.buf_select = %d", inference->buf_select);
        ESP_LOGI(TAG, "inference.buf_ready = %d", inference->buf_ready);
    }

    if (writer_buffer_overrun == true){
        ESP_LOGE(TAG, "wav buffer overrun");
    }
    
    if (inference_buffer_overrun == true){
        ESP_LOGE(TAG, "inference buffer overrun");
    }


    free(raw_samples);
    return samples_read;
}

I2SMEMSSampler::~I2SMEMSSampler(){


}
