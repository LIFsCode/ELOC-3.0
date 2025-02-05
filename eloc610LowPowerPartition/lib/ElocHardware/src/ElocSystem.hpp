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

#ifndef ELOCSYSTEM_HPP_
#define ELOCSYSTEM_HPP_

#include <esp_err.h>

#include "CPPI2C/cppi2c.h"
#include "ELOC_IOEXP.hpp"
#include "lis3dh.h"

#include "ElocStatus.hpp"

//TODO: check for a good file to place this
typedef enum {
    REC_REQ_NONE = 0,
    REC_REQ_START,
    REC_REQ_STOP,
}rec_req_t;

typedef enum {
    REC_STAT_IDLE = 0,
    REC_STAT_RECORDING,
    REC_STAT_ERR_SD_FULL,
    REC_STAT_ERR_SD_NA
}rec_stat_t;

//TODO: handle rec_req_evt_queue at a single point
extern QueueHandle_t rec_req_evt_queue;
extern QueueHandle_t rec_ai_evt_queue;

class StatusLED;

struct loraWAN_keys_t {
    uint64_t devEUI;
    uint8_t appKey[16];
    uint8_t nwkKey[16];
}; 

class ElocSystem
{
public:
    typedef struct Status_t {
        bool btEnabled;
        bool btConnected;
        WAVFileWriter::Mode recMode;
        bool ai_run_enable;
        bool sdCardMounted;
        bool batteryLow;
        bool intruderDetected;
        inline bool operator==(const Status_t& rhs) const
        {
            return ((this->btEnabled == rhs.btEnabled) &&
                    (this->btConnected == rhs.btConnected) &&
                    (this->recMode == rhs.recMode) &&
                    (this->ai_run_enable == rhs.ai_run_enable) &&
                    (this->sdCardMounted == rhs.sdCardMounted) &&
                    (this->batteryLow == rhs.batteryLow) &&
                    (this->intruderDetected == rhs.intruderDetected));
        }
    }Status_t;
private:
    /* data */
    ElocSystem();
    CPPI2C::I2c* mI2CInstance;
    ELOC_IOEXP* mIOExpInstance;
    LIS3DH* mLis3DH;
    StatusLED* mStatusLed;
    StatusLED* mBatteryLed;

    Status_t mStatus;
    bool mBuzzerIdle;
    bool mRefreshStatus;
    bool mIntruderDetected;
    uint32_t mIntruderThresholdCnt;

    bool mFwUpdateProcessing;

    struct factoryInfo_t {
        uint16_t hw_gen;
        uint16_t hw_rev;
        uint32_t serialNumber;
    }mFactoryInfo; 
    loraWAN_keys_t mLoraWAN_keys;

    /**
     * @brief Set implementation-specific power management configuration. This is a wrapper for esp_pm_configure
     *        but takes certain IDF bugs into account for handling GPIOs and RTC IOs.
     * @param config pointer to implementation-specific configuration structure (e.g. esp_pm_config_esp32)
     * @return
     *      - ESP_OK on success
     *      - ESP_ERR_INVALID_ARG if the configuration values are not correct
     *      - ESP_ERR_NOT_SUPPORTED if certain combination of values is not supported,
     *        or if CONFIG_PM_ENABLE is not enabled in sdkconfig
     */
    esp_err_t pm_configure(const void* vconfig);

    // simple wrapper for BuzzerBeep with the ElocSystem specific callback
    void setBuzzerBeep(unsigned int frequency, unsigned int beeps);
    void setBuzzerBeep(unsigned int frequency, unsigned int beeps, unsigned int const pauseDuration, unsigned int const sequences);

    static void BuzzerDone() {
        ElocSystem::GetInstance().setBuzzerIdle();
    }
    void setBuzzerIdle();
public:
    inline static ElocSystem& GetInstance() {
        static ElocSystem System;
        return System;
    }
    ~ElocSystem();
    inline CPPI2C::I2c& getI2C() {
        assert(mI2CInstance != NULL);
        return *mI2CInstance;
    }
    inline ELOC_IOEXP& getIoExpander() {
        assert(mIOExpInstance != NULL);
        return *mIOExpInstance;
    }
    inline LIS3DH& getLIS3DH() {
        assert(mLis3DH != NULL);
        return *mLis3DH;
    }
    inline bool hasI2C() const {
        return mI2CInstance != NULL;
    }
    inline bool hasIoExpander() const {
        return mIOExpInstance != NULL;
    }
    inline bool hasLIS3DH() const {
        return mLis3DH != NULL;
    }
    uint16_t getTemperaure();

    /// @brief Checks and adjusts the power management options if it is necessary based on the required I2S sample rate
    /// @param sample_rate sample rate in Hz
    /// @return ESP_OK on success, error code otherwise
    esp_err_t pm_check_ForRecording(int sample_rate);

    /// @brief Configures the Power Management based on the ElocConfig
    /// @return ESP_OK on success, error code otherwise
    esp_err_t pm_configure();

    void notifyStatusRefresh();
    esp_err_t handleSystemStatus(bool btEnabled, bool btConnected);

    void notifyFwUpdateError();
    void notifyFwUpdate();
};




#endif // ELOCSYSTEM_HPP_