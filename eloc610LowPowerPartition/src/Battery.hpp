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

#ifndef BATTERY_HPP_
#define BATTERY_HPP_

#include "CPPANALOG/analogio.h"
#include "ElocSystem.hpp"
#include "esp_err.h"

typedef struct {
    // use LiFePo defaults: just in case bat_limits_t is used uninitialized
    float Voff = 2.7;
    float Vlow = 3.18;
    float Vfull = 3.3;
}bat_limits_t;

class Battery
{
private:
    bool mChargingEnabled;
    float mVoltageOffset;
    ElocSystem& mSys;
    CPPANALOG::CppAdc1 mAdc;
    float mVoltage;

    const uint32_t AVG_WINDOW;
    
    virtual esp_err_t setChargingEnable(bool enable); 
    bat_limits_t getLimits();
    Battery();
    esp_err_t init();
public:
    virtual ~Battery();
    static Battery& GetInstance() {
        static Battery battery;
        return battery;
    }

    virtual float getVoltage();
    virtual float getSoC();

    bool isLow();
    bool isFull();
    bool isEmpty();

    virtual esp_err_t setDefaultChargeEn(bool enable); 
    bool isBatteryPresent();
};


#endif // BATTERY_HPP_