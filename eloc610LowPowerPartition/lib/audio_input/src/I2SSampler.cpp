
#include "I2SSampler.h"
#include "driver/i2s.h"
#include "esp_log.h"

static const char *TAG = "I2Sampler";

I2SSampler::I2SSampler(i2s_port_t i2sPort, const i2s_config_t &i2s_config) : m_i2sPort(i2sPort), m_i2s_config(i2s_config)
{
}

esp_err_t I2SSampler::install_and_start()
{
    auto ret = i2s_driver_install(m_i2sPort, &m_i2s_config, 0, NULL);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Func: %s, i2s_driver_install", __func__);
return ret;
    }

    // set up the I2S configuration from the subclass
    ret = configureI2S();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Func: %s, configureI2S", __func__);
return ret;
    }

    i2s_installed_and_started = true;

    return ret;
}

esp_err_t I2SSampler::uninstall()
{
    // clear any I2S configuration
    unConfigureI2S();
    // stop the i2S driver
    auto ret = i2s_driver_uninstall(m_i2sPort);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Func: %s, i2s_driver_uninstall", __func__);
            }

    i2s_installed_and_started = false;

    return ret;
}
