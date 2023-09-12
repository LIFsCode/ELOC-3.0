
#include "I2SSampler.h"
#include "driver/i2s.h"

#include "esp_err.h"
#include "esp_log.h"

static const char* TAG = "I2SSampler";

I2SSampler::I2SSampler(i2s_port_t i2sPort, const i2s_config_t &i2s_config) : m_i2sPort(i2sPort), m_i2s_config(i2s_config)
{
}

esp_err_t I2SSampler::start()
{
    //install and start i2s driver
    //i2s_driver_install(m_i2sPort, &m_i2s_config, 0, NULL);
    
    auto r = i2s_driver_install(m_i2sPort, &m_i2s_config, 0, NULL);
    
    if(r != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install error: %s", esp_err_to_name(r));
        return r;
    }

    // set up the I2S configuration from the subclass
    return(configureI2S());

}

void I2SSampler::stop()
{
    // clear any I2S configuration
    unConfigureI2S();
    // stop the i2S driver
    i2s_driver_uninstall(m_i2sPort);
}
