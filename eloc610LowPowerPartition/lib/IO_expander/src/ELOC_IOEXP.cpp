/*
 * Created on Fri Apr 07 2023
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

#include "esp_err.h"
#include "esp_log.h"
#include "ELOC_IOEXP.hpp"

#define TAG "ELOC_IOEXP"

esp_err_t ELOC_IOEXP::init() {
    if (!this->checkPresence()) {
        ESP_LOGI(TAG, "Cannot finde PCA9557 at addres 0x%02X, check address pins or I2C connection!", this->IO_I2C_SLAVEADDRESS);
        mErrorCode = ESP_ERR_NOT_FOUND;
        return mErrorCode;
    }
    if ((mErrorCode = portConfig_I(LiION_DETECT))) return mErrorCode;
    if ((mErrorCode = portConfig_O (LED_STATUS | LED_BATTERY | CHARGE_EN))) return mErrorCode;
    // set outputs to default values
    if ((mErrorCode = setOutputPort(LED_STATUS))) return mErrorCode;
    if ((mErrorCode = clearOutputPort(LED_BATTERY | CHARGE_EN))) return mErrorCode;
    outputReg = LED_STATUS;

    ESP_LOGI(TAG, "PCA9557 initialized");
    return ESP_OK;
}

ELOC_IOEXP::ELOC_IOEXP(CPPI2C::I2c& i2cInstance) : 
    PCA9557_IOEXP(i2cInstance, 0x00), outputReg(0x00), mErrorCode(ESP_OK){
    mErrorCode = init();
};

esp_err_t ELOC_IOEXP::setOutputBit(uint32_t bit, bool enable) {
    if (enable) {
        if (!(outputReg & bit)) {
            outputReg |= bit;
            mErrorCode = setOutputPort (bit);
        }
    }
    else {
        if (outputReg & bit) {
            outputReg &= ~bit;
            mErrorCode = clearOutputPort (bit);
        }
    }
    return mErrorCode;
}
esp_err_t ELOC_IOEXP::toggleOutputBit(uint32_t bit) {
    mErrorCode = setOutputBit(bit, !(outputReg & bit)); // invert the output bit
    return mErrorCode;
}

esp_err_t ELOC_IOEXP::chargeBattery(bool enable) {
    mErrorCode = setOutputBit(CHARGE_EN, enable);
    return mErrorCode;
}
bool ELOC_IOEXP::hasLiIonBattery() {
    return this->readInput() & LiION_DETECT;
}
