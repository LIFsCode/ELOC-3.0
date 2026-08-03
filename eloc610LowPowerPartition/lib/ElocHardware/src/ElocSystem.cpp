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
#include <esp32/clk.h>
#include <driver/rtc_io.h>
#include <byteswap.h>

// arduino includes
#include "Arduino.h"
#include "EasyBuzzer.h"

#include "config.h"
#include "SDCardSDIO.h"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "ElocLora.hpp"
#include "Battery.hpp"
#include "strutils.h"
#include "logging.hpp"

//TODO move sd card into ELOC system
extern SDCardSDIO sd_card;

static const char *TAG = "ElocSystem";

/// IO expander used by readSdCardDetect(). A plain pointer rather than ElocSystem::GetInstance()
/// because the hook is installed from the ElocSystem constructor, where the singleton does not
/// exist yet.
static ELOC_IOEXP* gSdDetectIoExp = nullptr;

/**
 * @brief Card-detect hook handed to SDCardSDIO (see SDCardSDIO::CardDetectFn).
 *        Reads IO4 and applies the board polarity.
 * @return true when a card is inserted
 */
static bool readSdCardDetect() {
    if (gSdDetectIoExp == nullptr) {
        return true;  // no expander: presence unknown, assume a card is there
    }
    return gSdDetectIoExp->getSdDetectLevel() == (SDCARD_DETECT_PRESENT_LEVEL != 0);
}

class StatusLED
{
private:
    ELOC_IOEXP& mIOExpInstance;
    uint32_t mIObit;
    int32_t mDurationMs;
    uint32_t mOnDurationMs;
    uint32_t mOffDurationMs;
    uint32_t mRepeats;
    uint32_t mRepeatCnt;
    bool mIsBlinking;
    bool mIsRepeating;

    uint32_t mStartedMs;
    uint32_t mPeriodStartMs;
    bool mCurrentState;
    const char* mName;
 public:
    StatusLED(ELOC_IOEXP& IOExpInstance, uint32_t bit, const char* name ="") :
        mIOExpInstance(IOExpInstance), mIObit(bit),
        mDurationMs(-1), mOnDurationMs(0), mOffDurationMs(0), mRepeats(0),mRepeatCnt(0),
        mIsBlinking(false), mIsRepeating(false), mStartedMs(0), mPeriodStartMs(0), mName(name) {
    }
    virtual ~StatusLED() {}

    esp_err_t  setState(bool val) {
        mDurationMs = -1;
        mIsBlinking = false;
        mIsRepeating = false;
        return mIOExpInstance.setOutputBit(mIObit, val);
    }
    esp_err_t setBlinking(bool initial, uint32_t onMs, uint32_t offMs, int32_t durationMs) {
        mIsBlinking = true;
        mOnDurationMs = onMs;
        mOffDurationMs = offMs;
        mDurationMs = durationMs;
        mCurrentState = initial;
        mStartedMs = millis();
        mPeriodStartMs = mStartedMs;
        ESP_LOGV(TAG, "%s: mOnDurationMs %d, mOffDurationMs %d, mDurationMs=%d", mName, mOnDurationMs, mOffDurationMs, mDurationMs);
        return mIOExpInstance.setOutputBit(mIObit, mCurrentState);
    }
    esp_err_t setBlinkingPeriodic(uint32_t onMs, uint32_t offMs, int32_t durationMs, uint32_t repeats) {
        mIsRepeating = true;
        mRepeats = repeats*2;  // double the repeats for high low period
        mRepeatCnt = 0;
        ESP_LOGV(TAG, "%s: Repeated mode %d repeats", mName, mRepeats);
        return setBlinking(false, onMs, offMs, durationMs);
    }

    esp_err_t update() {
        if(mIsBlinking) {
            if (millis() - mStartedMs >= mDurationMs) {
                if (mIsRepeating) {
                    mStartedMs = millis();
                    mPeriodStartMs = mStartedMs;
                    mRepeatCnt = 0;
                } else {
                    mIsBlinking = false;
                    return mIOExpInstance.setOutputBit(mIObit, false);
                }
            }
            uint32_t periodLimit = mCurrentState ? mOnDurationMs : mOffDurationMs;
            if (millis() - mPeriodStartMs >= periodLimit) {
                if (mIsRepeating) {
                    mRepeatCnt++;
                    if (mRepeatCnt == mRepeats) {
                        // if we already repeated the required times i
                        mCurrentState = false;
                        return mIOExpInstance.setOutputBit(mIObit, false);
                    }
                    else if (mRepeatCnt >= mRepeats) {
                        return ESP_OK; // idle until mDurationMs expires
                    }
                }
                mPeriodStartMs = millis();
                mCurrentState = !mCurrentState;
                return mIOExpInstance.setOutputBit(mIObit, mCurrentState);
            }
        }
        return ESP_OK;
    }
};

ElocSystem::ElocSystem():
    mI2CInstance(NULL), mIOExpInstance(NULL), mLis3DH(NULL), mStatus(), mBuzzerIdle(true),
    mLastBuzzerStopMs(0), mRefreshStatus(false), mIntruderDetected(false),
    mFwUpdateProcessing(false), mFactoryInfo(),
    mTargetPmProfile(PmProfile::CONFIG_DEFAULT), mAppliedPmProfile(PmProfile::CONFIG_DEFAULT),
    mBtActive(false),
    mSdState(SdState::BOOT), mSdStopRequestedMs(0), mSdLogAbandoned(true),
    mSdRecModeBeforeRemoval(WAVFileWriter::Mode::disabled), mSdRemountEvent(false)
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

    err = nvs_open_from_partition("nvs", "loraKeys", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle 'loraKeys' namespace!\n", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "The NVS handle successfully opened");
        size_t strLen=sizeof(mLoraWAN_keys.devEUI);
        err = nvs_get_blob(my_handle, "devEUI", &mLoraWAN_keys.devEUI, &strLen);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'devEUI'", esp_err_to_name(err));
        mLoraWAN_keys.devEUI = __bswap_64(mLoraWAN_keys.devEUI); // swap byte order, hex2bin stores value in reverse byte order in nvs.csv

        strLen=sizeof(mLoraWAN_keys.appKey);
        err = nvs_get_blob(my_handle, "appKey", reinterpret_cast<char*>(mLoraWAN_keys.appKey), &strLen);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'appKey'", esp_err_to_name(err));

        strLen=sizeof(mLoraWAN_keys.nwkKey);
        err = nvs_get_blob(my_handle, "nwkKey", reinterpret_cast<char*>(mLoraWAN_keys.nwkKey), &strLen);
        if (err) ESP_LOGE(TAG, "%s: Missing parameter 'nwkKey'", esp_err_to_name(err));

        // Close
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Reading values from NVS done - all OK");
    }

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
            // turn on status LED on ELOC board; the StatusLED blinkers created
            // below take over boot feedback (old 5 s LED animation removed)
            ioExp.setOutputBit(ELOC_IOEXP::LED_STATUS, true);

            // Hand the SD driver the card-detect line (IO4). Must happen before the first
            // mountSDCard() so init() can validate the polarity against a known-present card.
            gSdDetectIoExp = &ioExp;
            ESP_LOGI(TAG, "SD card-detect (IO4) reads %d, '%s' by SDCARD_DETECT_PRESENT_LEVEL=%d",
                     ioExp.getSdDetectLevel(), readSdCardDetect() ? "inserted" : "removed",
                     SDCARD_DETECT_PRESENT_LEVEL);
            sd_card.setCardDetect(&readSdCardDetect);
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

        mStatusLed = new StatusLED(*mIOExpInstance, ELOC_IOEXP::LED_STATUS, "StatusLED");
        if (!mStatusLed) {
            ESP_LOGE(TAG, "Failed to create status LED!");
        }
        mBatteryLed = new StatusLED(*mIOExpInstance, ELOC_IOEXP::LED_BATTERY, "BatteryLED");
        if (!mBatteryLed) {
            ESP_LOGE(TAG, "Failed to create battery LED!");
        }

        mBatteryLed->setBlinking(true, 500, 500, -1);
        mStatusLed->setBlinking(true, 500, 500, -1);

    }

    ESP_LOGI(TAG, "Setup Buzzer");
    EasyBuzzer.setPin(BUZZER_PIN);
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
    gpio_sleep_sel_dis(BUZZER_PIN);
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

static const char* toString(ElocSystem::PmProfile profile) {
    switch (profile) {
        case ElocSystem::PmProfile::AI_MAX_PERF:         return "AI_MAX_PERF";
        case ElocSystem::PmProfile::RECORDING_LOW_POWER: return "RECORDING_LOW_POWER";
        case ElocSystem::PmProfile::CONFIG_DEFAULT:      return "CONFIG_DEFAULT";
    }
    return "UNKNOWN";
}

esp_err_t ElocSystem::pm_configure() {
    /**
     * Setup Power Management based on the targeted PmProfile
     * @warning Must not run while the BT controller is up (BBPLL relock), see pm_requestProfile()
     * @ref https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-reference/system/power_management.html#configuration
     */
    PmProfile profile = mTargetPmProfile;

    // The low-power recording profile only applies without LoRa: with LoRa enabled DFS/light
    // sleep are not usable anyway (min is forced to max below), so use the configured values.
    if (profile == PmProfile::RECORDING_LOW_POWER && getConfig().loraConfig.loraEnable) {
        ESP_LOGI(TAG, "LoraWAN is enabled, using configured CPU frequencies instead of the low-power recording profile");
        profile = PmProfile::CONFIG_DEFAULT;
    }

    esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = getConfig().cpuMaxFrequencyMHZ,
        .min_freq_mhz = getConfig().cpuMinFrequencyMHZ,
        .light_sleep_enable = getConfig().cpuEnableLightSleep
    };
    switch (profile) {
        case PmProfile::AI_MAX_PERF:
            // AI inference needs full performance: pin the clock at 240 MHz so the model
            // never runs downclocked (DFS off via min = max).
            cfg.max_freq_mhz = 240;
            cfg.min_freq_mhz = 240;
            break;
        case PmProfile::RECORDING_LOW_POWER:
            // min = max, i.e. DFS OFF. This used to be min 10 (min 20 at >= 30 kHz, see
            // https://github.com/LIFsCode/ELOC-3.0/issues/30), but scaling APB between 10 and
            // 80 MHz corrupts BOTH software time bases on this chip, by different amounts:
            //
            //   esp_timer  (TG0 LACT, divided from APB)  ran ~41 % fast
            //   FreeRTOS tick (CCOUNT -> every ESP_LOGx timestamp)  ran ~23 % fast
            //
            // Both are rescaled on every frequency switch by esp_pm's on_freq_update(), via
            // different correction paths, which is how they end up disagreeing with each other.
            // Measured over a 2-day recordOn_detectOff run and cross-checked against the WAV
            // filename timestamps, which come from the DFS-immune RTC slow clock: the audio
            // itself was real-time to 0.03 %, so only the timers were wrong.
            //
            // The audio is unaffected either way (I2S runs off the APLL, which DFS never
            // touches), but the skew is not cosmetic:
            //   - anything scheduled off esp_timer fires ~41 % early. The GPS acquisition
            //     window is the one that hurts: on a duty-cycle timer wake this profile is
            //     applied in setup (main.cpp), BEFORE the GPS time-sync wait and long before
            //     I2S installs, so a nominal 30 s becomes ~21 s real and the patient 180 s
            //     cold-start window becomes ~128 s — below the ~30-35 s a cold GNSS fix needs.
            //   - every log timestamp is wrong, so the SD log can't be used for timing analysis.
            //
            // Cost of min = max is only the idle dip from 80 down to 10 MHz; the large win
            // (80 instead of 240 MHz) is kept. Revisit only with a DFS-immune time base.
            cfg.max_freq_mhz = 80;
            cfg.min_freq_mhz = 80;
            break;
        case PmProfile::CONFIG_DEFAULT:
        default:
            if (cfg.max_freq_mhz > CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ) {
                ESP_LOGW(TAG, "CPU Max Frequency is set to %d MHz, but CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ = %d MHz",
                    cfg.max_freq_mhz, CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ);
            }
            break;
    }
    if (getConfig().loraConfig.loraEnable) {
        cfg.min_freq_mhz = cfg.max_freq_mhz;
        cfg.light_sleep_enable = false;
        ESP_LOGW(TAG, "LoraWAN is enabled! This does not allow to use Power Management, e.g. DFS or light sleep!"
                    "Overwrite Settings: min CPU freq = max CPU freq (%d MHz) and light sleep to %s",
            cfg.min_freq_mhz, cfg.light_sleep_enable ? "on" : "off");
    }
    // Logged AFTER the LoRa override above, so this line always reports what is actually being
    // applied. It used to sit before it, printing the requested values — with LoRa enabled that
    // meant a "light sleep on" line followed by the override warning, which is exactly the
    // ambiguity you do not want in the one line used to verify the PM profile on the bench.
    ESP_LOGI(TAG, "Applying PM profile %s: CPU max %d MHz, min %d MHz, light sleep %s",
        toString(profile), cfg.max_freq_mhz, cfg.min_freq_mhz, cfg.light_sleep_enable ? "on" : "off");
    if (esp_err_t err = this->pm_configure(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PM config with %s", esp_err_to_name(err));
        return err;
    }

    /* esp_pm_configure() only stores the new config; the actual frequency switch is
     * deferred until the next PM *mode* transition — which may never come once the BT
     * controller permanently holds its APB lock (the CPU then silently stays at the boot
     * frequency). Briefly acquiring a CPU_FREQ_MAX lock forces the switch to happen right
     * here, at a controlled moment (pm_configure() is called before the radios start). */
    esp_pm_lock_handle_t applyLock = nullptr;
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "pm_apply", &applyLock) == ESP_OK) {
        esp_pm_lock_acquire(applyLock);
        esp_pm_lock_release(applyLock);
        esp_pm_lock_delete(applyLock);
    }
    mAppliedPmProfile = mTargetPmProfile;
    ESP_LOGI(TAG, "CPU frequency now %d MHz", esp_clk_cpu_freq() / 1000000);
    return ESP_OK;
}

esp_err_t ElocSystem::pm_requestProfile(PmProfile profile) {
    bool changed = (profile != mTargetPmProfile);
    mTargetPmProfile = profile;
    if (profile == mAppliedPmProfile) {
        return ESP_OK;
    }
    if (mBtActive) {
        // Switching between the 320 MHz PLL (80/160 MHz) and the 480 MHz PLL (240 MHz)
        // relocks the BBPLL the BT radio is clocked from and would crash the device.
        // The profile stays pending and is applied by setBluetoothActive(false) once the
        // BT controller is torn down (recording start / BT timeout / app disconnect).
        if (changed) {
            ESP_LOGI(TAG, "PM profile %s requested, deferred until Bluetooth shuts down", toString(profile));
        }
        return ESP_OK;
    }
    return pm_configure();
}

void ElocSystem::setBluetoothActive(bool active) {
    mBtActive = active;
    if (!active && (mTargetPmProfile != mAppliedPmProfile)) {
        ESP_LOGI(TAG, "Bluetooth is down, applying deferred PM profile");
        pm_configure();
    }
}

void ElocSystem::handleSdCardHotSwap() {
    esp_err_t sdStatus = sd_card.update();

    switch (mSdState) {
        case SdState::BOOT:
            // First cycle: adopt whatever setup() achieved, without reporting it as a hot-swap.
            mSdState = (sdStatus == ESP_OK) ? SdState::MOUNTED : SdState::ABSENT;
            break;

        case SdState::MOUNTED:
            if (sdStatus == ESP_OK) {
                break;
            }
            // Card gone (pulled out, or it stopped answering). sd_card has already cleared its
            // mounted flag, so nothing opens a new file from here on. The volume itself must not
            // be released yet: the wav writer may still be inside fwrite() on it.
            ESP_LOGW(TAG, "SD card lost (%s) - stopping recording before unmounting",
                     esp_err_to_name(sdStatus));
            mSdRecModeBeforeRemoval = wav_writer.get_mode();
            if (mSdRecModeBeforeRemoval != WAVFileWriter::Mode::disabled) {
                // Makes the writer task close its file and exit; the WAV keeps whatever was
                // already flushed, its header stays at the pre-recording values.
                wav_writer.set_mode(WAVFileWriter::Mode::disabled);
            }
            mSdLogAbandoned = Logging::abandonSdCard();
            mSdStopRequestedMs = millis();
            mSdState = SdState::STOPPING;
            break;

        case SdState::STOPPING:
            if (!mSdLogAbandoned) {
                // A task was still inside the log file. The hook is already detached, so it
                // drains within an SDMMC timeout - retry until the handle is released, because
                // unmounting under it would free the FATFS it is writing to.
                mSdLogAbandoned = Logging::abandonSdCard();
            }
            if (wav_writer.wav_recording_in_progress || !mSdLogAbandoned) {
                // Unsigned elapsed-time compare, so a millis() wrap cannot stall the teardown
                if ((millis() - mSdStopRequestedMs) < SD_WRITER_STOP_TIMEOUT_MS) {
                    break;  // still finishing up, check again next cycle
                }
                // Somebody is wedged in a blocking SDMMC operation. Unmounting now would pull the
                // FATFS object out from under them, so keep waiting rather than crash - the card
                // is unusable either way and re-inserting it just re-arms this timeout.
                ESP_LOGE(TAG, "SD card still held after %d ms (recording=%d, log=%d), delaying unmount",
                         SD_WRITER_STOP_TIMEOUT_MS, wav_writer.wav_recording_in_progress,
                         !mSdLogAbandoned);
                mSdStopRequestedMs = millis();
                break;
            }
            sd_card.unmount();
            mSdState = SdState::ABSENT;
            ESP_LOGW(TAG, "SD card unmounted - insert a card to resume");
            break;

        case SdState::ABSENT:
            if (sdStatus != ESP_OK) {
                break;  // no card yet (update() throttles the retries)
            }
            ESP_LOGI(TAG, "SD card re-mounted, %.2f GB free", sd_card.freeSpaceGB());
            mSdRemountEvent = true;  // main loop re-creates the session folder and resumes
            mSdState = SdState::MOUNTED;
            break;
    }
}

bool ElocSystem::consumeSdRemountEvent() {
    bool event = mSdRemountEvent;
    mSdRemountEvent = false;
    return event;
}

esp_err_t ElocSystem::handleSystemStatus(bool btEnabled, bool btConnected) {
    // NOTE: the SD hot-swap poll deliberately does NOT run here. It is driven from the main loop
    // (handleSdCardHotSwap()), because a missing card makes mount attempts and log teardown block
    // for seconds at a time - long enough for this task to stop serving SPP and drop the app.

    // notifyStatusRefresh() only runs on a knock event, so an active alarm would otherwise
    // stay latched forever after detection is disabled via setConfig — clear it here.
    // (Duty-cycle mode counts as disabled: intruder detection is a 24/7-only feature.)
    if (mIntruderDetected &&
        (!getConfig().IntruderConfig.detectEnable || getDutyCycleConfig().enable)) {
        ESP_LOGI(TAG, "Intruder detection disabled, clearing active alarm");
        mIntruderDetected = false;
    }

    Status_t status;
    status.batteryLow = Battery::GetInstance().isLow();
    status.btEnabled = btEnabled;
    status.btConnected = btConnected;
    status.recMode = wav_writer.get_mode();
    status.ai_run_enable = ai_run_enable;
    status.sdCardMounted = sd_card.isMounted();
    status.intruderDetected = mIntruderDetected;
    if ((mStatus == status) && !mRefreshStatus) {
        if (esp_err_t err = mStatusLed->update()) {
            ESP_LOGE(TAG, "mStatusLed->update() failed with %s", esp_err_to_name(err));
        }
        if (esp_err_t err = mBatteryLed->update()) {
            ESP_LOGE(TAG, "mBatteryLed->update() failed with %s", esp_err_to_name(err));
        }
        if (!mBuzzerIdle) {
            EasyBuzzer.update();
        }
    }
    else {
        if (mStatus.intruderDetected && !mIntruderDetected) {
            // release intruder alarm buzzer beeping
            setBuzzerIdle();
        }
        if (mIntruderDetected) {
            setBuzzerBeep(494, 50);
        }
        else if (status.batteryLow) {
            mStatusLed->setBlinkingPeriodic(50, 100, 5*1000, 3);
            mBatteryLed->setBlinkingPeriodic(50, 100, 5*1000, 3);
            setBuzzerBeep(98 , 1, 10*1000, 100);
            ESP_LOGW(TAG, "Battery Low detected!");
        }
        else if (!status.sdCardMounted) {
            mStatusLed->setBlinkingPeriodic(50, 100, 1*1000, 5);
            mBatteryLed->setBlinkingPeriodic(50, 100, 1*1000, 5);
            ESP_LOGW(TAG, "SD Card not mounted!");
        }
        else if (status.recMode != WAVFileWriter::Mode::disabled) {
            mStatusLed->setBlinking(true, 100, 900, 30*1000);
            if (status.ai_run_enable) {
                mBatteryLed->setBlinking(true, 100, 900, 30*1000);
            } else {
                mBatteryLed->setState(false);
            }
        }
        else if (status.ai_run_enable) {
            mBatteryLed->setBlinking(true, 100, 900, 30*1000);
        }
        else if (status.btConnected) {
            mStatusLed->setState(true);
            mBatteryLed->setState(true);
            if (!mStatus.btConnected) {
                setBuzzerBeep(98 , 1);
            }
        }
        else if (status.btEnabled) {
            mStatusLed->setState(true);
            mBatteryLed->setState(false);
            if (!mStatus.btEnabled) {
                setBuzzerBeep(261 , 2);
            }
        }
        else {
            // off else wise: TODO: check if this is intended behavior
            mStatusLed->setState(false);
            mBatteryLed->setState(false);
            if (mStatus.btEnabled) {
                setBuzzerBeep(523 , 2);
            }
        }

    }
    mRefreshStatus = false;
    mStatus = status;

    //TODO find a better place for Lora handling
    ElocLora::GetInstance().ElocLoraLoop();

    return ESP_OK;
}

// Settle time after the buzzer stops during which accelerometer clicks are still ignored
// (covers mechanical/acoustic ringing of the case after the last beep)
static const uint32_t C_BUZZER_KNOCK_GUARD_MS = 1000;

void ElocSystem::notifyStatusRefresh() {
    static uint32_t lastRefreshMs = 0;
    static uint32_t cntFastUpdates = 0;
    const intruderConfig_t& cfg = getConfig().IntruderConfig;
    // Intruder detection is a continuous-operation (24/7) feature only. In duty-cycle mode
    // the device is mostly in deep sleep (where the knock ISR cannot run), and a knock wake
    // was tried and reverted (heap exhaustion when BT+LoRa+GPS+AI all boot together).
    if (!cfg.detectEnable || getDutyCycleConfig().enable) {
        cntFastUpdates = 0;
        mIntruderDetected = false;
        return;
    }
    // The buzzer sits on the same PCB as the LIS3DH: its vibration fires the click (knock)
    // interrupt, so the BT-connect beep would count as knocks on every app connection — and
    // an active alarm's own beeping would keep re-triggering it indefinitely. Ignore clicks
    // while the buzzer is running and for a short settle time after it stops.
    if (!mBuzzerIdle || (millis() - mLastBuzzerStopMs) < C_BUZZER_KNOCK_GUARD_MS) {
        return;
    }
    if ((millis() - lastRefreshMs) <= cfg.detectWindowMS) {
        cntFastUpdates++;
    }
    else {
        cntFastUpdates = 0;
    }
    if ((cntFastUpdates > cfg.thresholdCnt) && (cfg.thresholdCnt != 0)) {
        ESP_LOGW(TAG, "Intruder detected after %d knocks", cntFastUpdates);
        mIntruderDetected = true;
    }
    lastRefreshMs = millis();
    mRefreshStatus = true;
}

void ElocSystem::setBuzzerIdle() {
    mBuzzerIdle = true;
    mLastBuzzerStopMs = millis();
    EasyBuzzer.stopBeep();
}

void ElocSystem::setBuzzerBeep(unsigned int frequency, unsigned int beeps) {
    setBuzzerBeep(frequency, beeps, 0, 1);
}

void ElocSystem::setBuzzerBeep(unsigned int frequency, unsigned int beeps, unsigned int const pauseDuration, unsigned int const sequences) {
    mBuzzerIdle = false;
    //ESP_LOGI(TAG, "Beep: freq: %u, beeps %u, pause: %u, ceq: %u", frequency, beeps, pauseDuration, sequences);
    return EasyBuzzer.beep(frequency, 100, 250, beeps, pauseDuration, sequences, BuzzerDone);
}

void ElocSystem::notifyFwUpdateError() {
    for (int i=0;i<20;i++){
        mStatusLed->setState(true);
        mBatteryLed->setState(true);
        vTaskDelay(pdMS_TO_TICKS(40));
        mStatusLed->setState(false);
        mBatteryLed->setState(false);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    mFwUpdateProcessing = false;
}

void ElocSystem::notifyFwUpdate() {
    // Toggle LEDs with the speed
    if (mFwUpdateProcessing) {
        mStatusLed->update();
        mBatteryLed->update();
    }
    else {
        // will never be reset, except by notifyFwUpdateError, a successfull update results in a reboot
        mFwUpdateProcessing = true;
        mStatusLed->setBlinking(false, 100, 100, 60*1000);
        mBatteryLed->setBlinking(false, 100, 100, 60*1000);
    }
}

#if defined(EDGE_IMPULSE_ENABLED) && defined (AI_INCREASE_CPU_FREQ)
        // #ifndef CONFIG_ESP32_DEFAULT_CPU_FREQ_240
        //         #warning "AI_INCREASE_CPU_FREQ requires CONFIG_ESP32_DEFAULT_CPU_FREQ_240"
        // #endif

        // #ifdef CONFIG_ESP32_DEFAULT_CPU_FREQ_80 || CONFIG_ESP32_DEFAULT_CPU_FREQ_160
        //         #warning "AI_INCREASE_CPU_FREQ requires CONFIG_ESP32_DEFAULT_CPU_FREQ_240"
        // #endif

        // #if CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ != 240
        //         #warning "AI_INCREASE_CPU_FREQ requires CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=240"
        // #endif
#endif  // AI_INCREASE_CPU_FREQ
