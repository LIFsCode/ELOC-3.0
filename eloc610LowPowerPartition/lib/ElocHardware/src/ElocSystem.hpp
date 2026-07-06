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

    /// @brief CPU power-management profile, selected automatically from the active recording mode
    ///        (see pm_requestProfile()).
    enum class PmProfile {
        CONFIG_DEFAULT,       ///< no mode active: frequencies from the stored config
        AI_MAX_PERF,          ///< any AI detection mode: fixed 240 MHz
        RECORDING_LOW_POWER,  ///< recording only, no AI: min 10 / max 80 MHz (ignored if LoRa is enabled)
    };
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
    uint32_t mLastBuzzerStopMs;  // millis() when the buzzer last went idle (knock-sensor guard)
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

    PmProfile mTargetPmProfile;   // profile that should be in effect (may be pending while BT is up)
    PmProfile mAppliedPmProfile;  // profile last applied successfully via pm_configure()
    bool mBtActive;               // BT controller running: CPU frequency must not be switched (PLL relock)

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

    /// @brief Configures the Power Management based on the currently targeted PmProfile
    ///        (CONFIG_DEFAULT reads the frequencies from the ElocConfig).
    /// @warning Must not be called while the Bluetooth controller is running: switching
    ///          between the 320 MHz PLL (80/160 MHz) and the 480 MHz PLL (240 MHz) relocks
    ///          the BBPLL the BT radio is clocked from and crashes/WDT-resets the device.
    /// @return ESP_OK on success, error code otherwise
    esp_err_t pm_configure();

    /// @brief Request the CPU power-management profile matching the active recording mode.
    ///        Applied immediately if Bluetooth is down; otherwise stored and applied by
    ///        setBluetoothActive(false) once the BT controller has been torn down.
    ///        Safe to call periodically: it is a no-op if the profile is already applied.
    /// @return ESP_OK on success (including a deferred request), error code otherwise
    esp_err_t pm_requestProfile(PmProfile profile);

    /// @brief Track the Bluetooth controller state. Must be set true BEFORE the BT controller
    ///        starts and false only AFTER it is fully stopped. On the transition to false any
    ///        pending PM profile is applied.
    void setBluetoothActive(bool active);

    /// @brief The PM profile that should currently be in effect (may still be pending while BT is up)
    inline PmProfile getPmProfile() const {
        return mTargetPmProfile;
    }

    void notifyStatusRefresh();
    esp_err_t handleSystemStatus(bool btEnabled, bool btConnected);

    /// @brief Whether the knock-based intruder alarm is currently active
    inline bool isIntruderDetected() const {
        return mIntruderDetected;
    }

    void notifyFwUpdateError();
    void notifyFwUpdate();

    const loraWAN_keys_t& getLoraWAN_Keys() {
        return mLoraWAN_keys;
    }
};




#endif // ELOCSYSTEM_HPP_