/*
 * Created on Sun Apr 23 2023
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


#include <esp_log.h>
#include <driver/adc.h>
#include "config.h"

#include "CPPANALOG/analogio.h"

#include "ElocSystem.hpp"
#include "Battery.hpp"

static const char* TAG = "Battery";

//TODO: adjust these limits for Li-ION
static const bat_limits_t C_LiION_LIMITS = {
    .Voff = 2.7f,
    .Vlow = 3.18f,
    .Vfull = 3.3f
};

static const bat_limits_t C_LiFePo_LIMITS = {
    .Voff = 2.7,
    .Vlow = 3.18,
    .Vfull = 3.3
};


Battery::Battery() : 
    mChargingEnabled(true),
    mVoltageOffset(0.0), // TODO: read voltage offset from calibration info file (if present)
    mSys(ElocSystem::GetInstance()),
    mAdc(BAT_ADC),
    mVoltage(0.0),
    AVG_WINDOW(5) // TODO make this configureable
{
    //TODO: Read from config file if battery charging should be enabled by default
    setChargingEnable(mChargingEnabled);

    if (!mAdc.CheckCalFuse()) {
        ESP_LOGW(TAG, "ADC %d has been uncalibrated!", BAT_ADC);
    }
}

Battery::~Battery()
{
}


bat_limits_t Battery::getLimits()
{
    if (mSys.hasIoExpander()) {
       if (mSys.getIoExpander().hasLiIonBattery()) {
            return C_LiION_LIMITS;
       }
       else {
            return C_LiFePo_LIMITS;
       }
    }
    return bat_limits_t();
}

float Battery::getVoltage() {
    // disable charging first
    if (esp_err_t err = setChargingEnable(false)) {
        ESP_LOGI(TAG, "Failed to enable charging %s", esp_err_to_name(err));
    }
    mVoltage = 0.0;
    float accum=0.0; 
    float avg;
    for (int i=0;  i<AVG_WINDOW;i++) {
        if (mAdc.CheckCalFuse()) {
            accum+= static_cast<float>(mAdc.GetVoltage())/1000.0; // CppAdc1::GetVoltage returns mV
        }
        else {
            accum+= mAdc.GetRaw();
        }
    } 

    // TODO: add calculation of the voltage divider
    mVoltage=accum/5.0;
      

    if (mChargingEnabled) {
        if (esp_err_t err = setChargingEnable(mChargingEnabled)) 
            ESP_LOGI(TAG, "Failed to enable charging %s", esp_err_to_name(err));
    }
    //TODO: set battery 

    return mVoltage;
}

float Battery::getSoC()
{
    //TODO: Create a table for Voltage - SoC translation per battery type
    getVoltage();
    float soc = 0.0;
    if (mSys.hasIoExpander() && mSys.getIoExpander().hasLiIonBattery()) {
        if (mVoltage >= 4.2) {
            soc = 100;
        }
    }
    else {
        if (mVoltage >= 3.4) {
            soc = 100;
        }
    }
    return 0.0f;
}
bool Battery::isLow()
{
    //TODO: trigger a voltage measurement if previous measurement is too old
    if (mVoltage >= getLimits().Vfull) {
        return true;
    }
    return false;
}
bool Battery::isFull()
{
    //TODO: trigger a voltage measurement if previous measurement is too old
    if (mVoltage >= getLimits().Vfull) {
        return true;
    }
    return false;
}
bool Battery::isEmpty()
{
    return false;
}
esp_err_t Battery::setChargingEnable(bool enable)
{
    if (mSys.hasIoExpander()) {
       if (esp_err_t err = mSys.getIoExpander().chargeBattery(enable)) {
            ESP_LOGI(TAG, "Failed to %s charging %s", enable ? "enable" : "disable", esp_err_to_name(err));
            return err;
       }
    }
    return ESP_OK;
}

esp_err_t Battery::setDefaultChargeEn(bool enable)
{
    mChargingEnabled = enable;
    return setChargingEnable(enable);
}

bool Battery::isCalibrationDone() const {
    //TODO: check what kind of calibration to perform with ELOC
    //TODO: check if calibration is necessary of if it can be applied to
    //      the esp32 internal calibration --> esp_adc_cal_check_efuse()
    return false;
}

