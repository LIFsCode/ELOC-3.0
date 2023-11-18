/*
 * Created on Sun Apr 16 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <vector>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_pm.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"


static const char *TAG = "ElocSystem";

ElocSystem::ElocSystem():
    mI2CInstance(NULL), mIOExpInstance(NULL), mLis3DH(NULL), mFactoryInfo()
{
    ESP_LOGI(TAG, "Reading Factory Info from NVS");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG, "NVS partition was truncated and needs to be erased Retry nvs_flash_init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t my_handle;
    ESP_LOGI(TAG, "Opening Non-Volatile factory (NVS) handle");
    err = nvs_open_from_partition("nvs", "factory", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle 'factory' namespace!\n", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "The NVS handle successfully opened");
        err = nvs_get_u16(my_handle, "hw_gen", &mFactoryInfo.hw_gen);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'hw_gen'", esp_err_to_name(err));

        err = nvs_get_u16(my_handle, "hw_rev", &mFactoryInfo.hw_rev);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'hw_rev'", esp_err_to_name(err));

        err = nvs_get_u32(my_handle, "serial", &mFactoryInfo.serialNumber);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'serial'", esp_err_to_name(err));

        // Close
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Reading values from NVS done - all OK");
    }
    ESP_LOGI(TAG, "Factory Data: Serial=%d, HW_Gen = %d, HW_Rev = %d", 
        mFactoryInfo.serialNumber, mFactoryInfo.hw_gen, mFactoryInfo.hw_rev);


    ESP_LOGI(TAG, "Setting up I2C");

    static CPPI2C::I2c I2Cinstance (I2C_PORT);
    if (esp_err_t err = I2Cinstance.InitMaster(I2C_SDA_PIN, I2C_SCL_PIN, I2C_SPEED_HZ)) {
        ESP_LOGE(TAG, "Setting up I2c Master failed with %s", esp_err_to_name(err));
    }
    else { 
        // set up pointer for global getter functions
        mI2CInstance = &I2Cinstance;
        ESP_LOGI(TAG, "Setting up I2C devices...");

        std::vector<uint8_t> dev_addr;
        uint32_t numI2cDevices = I2Cinstance.scan(dev_addr);
        ESP_LOGI(TAG, "Found %u I2C devices:", numI2cDevices);
        for(auto it = dev_addr.begin(); it != dev_addr.end(); ++it) {
            ESP_LOGI(TAG, "\t 0x%02X", *it);
        }

        ESP_LOGI(TAG, "\t: ELOC_IOEXP PCA9557");
        static ELOC_IOEXP ioExp(I2Cinstance);
        if (esp_err_t err = ioExp.getErrorCode()) {
            ESP_LOGE(TAG, "Initializing PCA9557 IO-Expander failed with %s", esp_err_to_name(err));
        }
        else {
            mIOExpInstance = &ioExp;
            // turn on status LED on ELOC board
            ioExp.setOutputBit(ELOC_IOEXP::LED_STATUS, true);
            for (int i=0; i<10; i++) {
                // toggle Battery & Status LED in oposing order with 0.5 Hz
                ioExp.toggleOutputBit(ELOC_IOEXP::LED_STATUS);
                ioExp.toggleOutputBit(ELOC_IOEXP::LED_BATTERY);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        ESP_LOGI(TAG, "\t: LIS3DH Accelerometer");
        static LIS3DH lis3dh(I2Cinstance, LIS3DH_I2C_ADDRESS_2);
        if (esp_err_t err = lis3dh.lis3dh_init_sensor()) {
            ESP_LOGI(TAG, "Creating LIS3DH failed with %s", esp_err_to_name(err));
        }
        else {
            mLis3DH = &lis3dh;
            if (!lis3dh.lis3dh_enable_adc(false, true)) {
                ESP_LOGE(TAG, "Failed to enable Lis3DH Temperature sensor!");
            }
            else {
                ESP_LOGI(TAG, "Temperature: %d °C", lis3dh.lis3dh_get_temperature());
            }
        }
    }


}

ElocSystem::~ElocSystem()
{
}

uint16_t ElocSystem::getTemperaure() {
    if (mLis3DH) {
        return mLis3DH->lis3dh_get_temperature();
    }
    return 0;
}


esp_err_t ElocSystem::pm_configure(const void* vconfig) {
    esp_err_t err = esp_pm_configure(vconfig);
    // calling gpio_sleep_sel_dis must be done after esp_pm_configure() otherwise they will get overwritten
    // interrupt must be enabled during sleep otherwise they might continously trigger (missing pullup)
    // or won't work at all
    gpio_sleep_sel_dis(GPIO_BUTTON);
    gpio_sleep_sel_dis(LIS3DH_INT_PIN);
    // now setup GPIOs as wakeup source. This is required to wake up the recorder in tickless idle
    //BUGME: this seems to be a bug in ESP IDF: https://github.com/espressif/arduino-esp32/issues/7158
    //       gpio_wakeup_enable does not correctly switch to rtc_gpio_wakeup_enable() for RTC GPIOs.
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(LIS3DH_INT_PIN, GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(GPIO_BUTTON, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    return err;
}

esp_err_t ElocSystem::pm_check_ForRecording(int sample_rate) {
    esp_pm_config_esp32_t pm_cfg;
    if (esp_err_t err = esp_pm_get_configuration(&pm_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get PM config with %s", esp_err_to_name(err));
    }
    else {
        // see https://github.com/LIFsCode/ELOC-3.0/issues/30 for details
        // higher sample rates require a higher APB bus speed which is set to the min CPU clock
        if ((sample_rate >= 30000) && (pm_cfg.min_freq_mhz < 20)) {
            ESP_LOGI(TAG, "Samplerate %d Hz > 30 kHz, adjusting power management config", sample_rate);
            pm_cfg.min_freq_mhz = 20;
            if (esp_err_t err = this->pm_configure(&pm_cfg) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set PM config with %s", esp_err_to_name(err));
               return err;
            }
        }
    }
    return ESP_OK;
}

esp_err_t ElocSystem::pm_configure() {
    /** Setup Power Management */
    esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = getConfig().cpuMaxFrequencyMHZ, 
        .min_freq_mhz = getConfig().cpuMinFrequencyMHZ, 
        .light_sleep_enable = getConfig().cpuEnableLightSleep
    };            
    if (esp_err_t err = this->pm_configure(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PM config with %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}