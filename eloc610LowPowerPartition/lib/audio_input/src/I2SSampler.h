#pragma once

#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>

/**
 * Base Class for both the ADC and I2S sampler
 **/
class I2SSampler
{
protected:
    i2s_port_t m_i2sPort = I2S_NUM_0;
    i2s_config_t m_i2s_config;
    virtual bool configureI2S() = 0;
    
    /**
     * @brief Un-configure I2S port
    */
    virtual void unConfigureI2S(){};
    
    
    virtual void processI2SData(void *samples, size_t count){
        // nothing to do for the default case
    };

public:
    I2SSampler(i2s_port_t i2sPort, const i2s_config_t &i2sConfig);

    /**
     * @brief Zero the appropiate DMA buffer for the I2S port
     * @return true on success
    */
    virtual bool zero_dma_buffer(i2s_port_t i2sPort) = 0;

    /**
     * @brief Install the I2S port
     * TODO: This really should be renamed install, not start
     * @return true on success
    */
    bool start();

    virtual int read(int count) = 0;
    
    /**
     * @brief Unintsall the I2S port
     * TODO: This really should be renamed uninstall, not stop
     * 
     */
    void stop();
    
    int sample_rate()
    {
        return m_i2s_config.sample_rate;
    }
};
