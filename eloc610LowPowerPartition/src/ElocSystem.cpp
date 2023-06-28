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
#include "esp_log.h"
#include "config.h"

#include "ElocSystem.hpp"


static const char *TAG = "ElocSystem";

ElocSystem::ElocSystem():
    mI2CInstance(NULL), mIOExpInstance(NULL), mLis3DH(NULL)
{
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
        }
    }


}

ElocSystem::~ElocSystem()
{
}
