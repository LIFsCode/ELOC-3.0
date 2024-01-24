#pragma once

#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>

/**
 * Base Class for both the ADC and I2S sampler
 **/
class I2SSampler {
 protected:
    i2s_port_t m_i2sPort = I2S_NUM_0;
    i2s_config_t m_i2s_config;

    /**
     * Ideally it would be possible to probe hardware to see if it's
     * running, but doesn't seem possible. Use this instead ..
    */
    bool i2s_installed_and_started = false;

    /**
     * @brief Configure I2S port
    */
    virtual bool configureI2S() = 0;

    /**
     * @brief Un-configure I2S port
    */
    virtual void unConfigureI2S() {}

    /**
     * @note nothing to do for the default case
     */ 
    virtual void processI2SData(void *samples, size_t count) {}

 public:
    /**
     * @brief Construct a new I2SSampler object
     * 
     */
    I2SSampler();

    /**
     * @brief Setup port and configuration
     * 
     * @param i2sPort 
     * @param i2sConfig 
     */
    virtual void init(i2s_port_t i2sPort, const i2s_config_t &i2sConfig);

    /**
     * @brief Zero the appropiate DMA buffer for the I2S port
     * @return true on success
    */
    virtual esp_err_t zero_dma_buffer(i2s_port_t i2sPort) = 0;

    /**
     * @brief Install & start the I2S port
     * @return true on success
    */
    virtual esp_err_t install_and_start();

    /**
     * @brief Read data from I2S 
     * @return number of bytes read 
     */
    virtual int read() = 0;

    /**
     * @brief Uninstall and stop the I2S port
     */
    virtual esp_err_t uninstall();

    /**
     * @brief Get the current sample rate
     */
    virtual int sample_rate() {
        return m_i2s_config.sample_rate;
    }

    /**
     * @brief Check if I2S port already installed & started
     */
    virtual bool is_i2s_installed_and_started() {return i2s_installed_and_started;}

};
