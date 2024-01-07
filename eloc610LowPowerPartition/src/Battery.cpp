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
#include <istream>
#include <fstream>
#include "config.h"

#include "CPPANALOG/analogio.h"

#include "ArduinoJson.h"
#include "WString.h"

#include "utils/ffsutils.h"
#include "utils/jsonutils.hpp"

#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "Battery.hpp"

static const char* TAG = "Battery";

static const uint32_t JSON_DOC_SIZE = 512;

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

// from https://www.researchgate.net/figure/LiPo-Voltage-SOC-state-of-charge-table-SOC-Cell-Voltage-V-2-Cells-Voltage-V-3_tbl1_341375744
static const std::vector<socLUT_t> C_LiION_SOCs =
{
    {3.27,   0},
    {3.61,   5},
    {3.69,  10},
    {3.71,  15},
    {3.73,  20},
    {3.75,  25},
    {3.77,  30},
    {3.79,  35},
    {3.80,  40},
    {3.82,  45},
    {3.84,  50},
    {3.85,  55},
    {3.87,  60},
    {3.91,  65},
    {3.95,  70},
    {3.98,  75},
    {4.02,  80},
    {4.08,  85},
    {4.11,  90},
    {4.15,  95},
    {4.20, 100},
};  

// from https://footprinthero.com/lifepo4-battery-voltage-charts
static const std::vector<socLUT_t> C_LiFePo_SOCs =
{
    {2.50,   0},
    {3.00,   9},
    {3.13,  14},
    {3.20,  17},
    {3.23,  20},
    {3.25,  30},
    {3.28,  40},
    {3.30,  70},
    {3.33,  90},
    {3.35,  99},
    {3.40, 100},
};    

const char* Battery::CAL_FILE = "/spiffs/battery.cal";

Battery::Battery() : 
    mChargingEnabled(true),
    mVoltageOffset(0.0), // TODO: read voltage offset from calibration info file (if present)
    mSys(ElocSystem::GetInstance()),
    mAdc(BAT_ADC),
    mVoltage(0.0),
    mBatteryType(BAT_NONE),
    mLastReadingMs(0),
    mCalibrationValid(false),
    mHasIoExpander(ElocSystem::GetInstance().hasIoExpander()),
    AVG_WINDOW(getConfig().batteryConfig.avgSamples),
    UPDATE_INTERVAL_MS(getConfig().batteryConfig.updateIntervalMs)
{
    //TODO: Read from config file if battery charging should be enabled by default
    setChargingEnable(mChargingEnabled);

    if (!mAdc.CheckCalFuse()) {
        ESP_LOGW(TAG, "ADC %d has not been calibrated!", BAT_ADC);
    }
    if (mHasIoExpander) {
        mBatteryType = mSys.getIoExpander().hasLiIonBattery() ? BAT_LiPo : BAT_LiFePo;
    }
    if (getVoltage() < 0.5) {
        mBatteryType = BAT_NONE;
    }
    if (getConfig().batteryConfig.noBatteryMode) {
        mBatteryType = BAT_NONE;
        ESP_LOGI(TAG, "Disabled battery readings via config!");
    }
    else {
        ESP_LOGI(TAG, "Detected %s", getBatType());
    }
    if (esp_err_t err = loadCalFile() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load calibration with %s", esp_err_to_name(err));
    }
}

Battery::~Battery()
{
}

const char* Battery::getBatType() const {
    switch(mBatteryType) {
        case BAT_LiPo: 
            return "LiPo";
        case BAT_LiFePo: 
            return "LiFePo";
        default:
             return "no Battery";
    }
}

const bat_limits_t& Battery::getLimits() const {
    switch(mBatteryType) {
        case BAT_LiPo: 
            return C_LiION_LIMITS;
        case BAT_LiFePo: 
            return C_LiFePo_LIMITS;
        default:
        // use LiFePo as default limits, for a save version
        return C_LiFePo_LIMITS;
    }
}

const std::vector<socLUT_t>& Battery::getSocLUT() const {
    switch(mBatteryType) {
        case BAT_LiPo: 
            return C_LiION_SOCs;
        case BAT_LiFePo: 
            return C_LiFePo_SOCs;
        default:
        // use LiFePo as default limits, for a save version
        return C_LiFePo_SOCs;
    }
}
esp_err_t Battery::readRawVoltage() {

    // disable charging first
    if (esp_err_t err = setChargingEnable(false)) {
        ESP_LOGI(TAG, "Failed to enable charging %s", esp_err_to_name(err));
    }
    mVoltage = 0.0;
    float accum=0.0; 
    for (int i=0;  i<AVG_WINDOW;i++) {
        if (mAdc.CheckCalFuse()) {
            accum+= static_cast<float>(mAdc.GetVoltage())/1000.0; // CppAdc1::GetVoltage returns mV
        }
        else {
            accum+= mAdc.GetRaw();
        }
    } 
    mVoltage=accum/static_cast<float>(AVG_WINDOW);

    // scale the voltage according to the voltage divider on ELOC 3.0 HW
    mVoltage = mVoltage * (1500.0 + 470.0) / (1500.0);
      
    //ESP_LOGI(TAG, "scaled voltage =%.3fV, avg = %.3f", mVoltage, accum/static_cast<float>(AVG_WINDOW));

    if (mChargingEnabled) {
        if (esp_err_t err = setChargingEnable(mChargingEnabled)) {
            ESP_LOGI(TAG, "Failed to enable charging %s", esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

void Battery::updateVoltage() {

    if (mBatteryType == BAT_NONE) {
        // do not read voltages if no battery is present. Prevent disabling charger when ELOC is powered
        // only via USB. In that case it would turn off if the charger is disabled.
        return; 
    }
    int64_t nowMs = (esp_timer_get_time() / 1000ULL);
    if (((nowMs - mLastReadingMs) <= UPDATE_INTERVAL_MS) && mLastReadingMs) {
        // reduce load by only updating the battery value once every few seconds
        return;
    }
    mLastReadingMs = nowMs;

    readRawVoltage();

    if (!mCalibrationValid) {
        // if no calibration is present keep raw value
        return;
    }
    float loPoint = NAN;
    float hiPoint = NAN;
    float offset = 0;
    float scale = 1.0;
    for( std::map<float, float>::iterator iter = mCalData.begin(); iter != mCalData.end(); ++iter ) {
        Serial.print(iter->first);
        Serial.print(" : ");
        Serial.println(iter->second);
        if (iter->first < mVoltage) {
            loPoint = iter->first;
            offset =(iter->second - iter->first);
        }
        if (iter->first >= mVoltage) {
            hiPoint = iter->first;
            if (isnan(loPoint)) {
                offset = (iter->second - iter->first);
            }
            else {
                scale = (mCalData[hiPoint] - mCalData[loPoint]) / (hiPoint - loPoint);
                offset = (iter->second - iter->first*scale);
            }
            break;
        }
    }  
    float newVal = mVoltage * scale + offset;
    ESP_LOGV(TAG, "Calibrate voltage: read %.3fV, offset %.3f, scale %3f, result %.3fV", mVoltage, offset, scale, newVal);

}
    //TODO: set battery 
float Battery::getVoltage() {
    updateVoltage();
    return mVoltage;
}

float Battery::getSoC()
{
    //TODO: Create a table for Voltage - SoC translation per battery type
    updateVoltage();

    const std::vector<socLUT_t>& lut = getSocLUT();
    if (lut[0].volt > mVoltage) {
        // voltage below lowest SoC voltage
        return 0.0;
    }

    for(int i = 0; i < lut.size()-1; i++ )
    {
        if ( lut[i].volt <= mVoltage && lut[i+1].volt >= mVoltage )
        {
            double diffx = mVoltage - lut[i].volt;
            double diffn = lut[i+1].volt - lut[i].volt;

            return lut[i].soc + ( lut[i+1].soc - lut[i].soc ) * diffx / diffn; 
        }
    }

    return 100; // Not in Range
}
const char* Battery::getState() {
    if (isFull()) {
        return "Full";
    }
    else if (isLow()) {
        return "Low";
    }
    else if (isEmpty()) {
        return "Empty";
    }
    return "";
}
bool Battery::isLow()
{
    if (mBatteryType == BAT_NONE) {
        return false;
    }
    updateVoltage();
    if (mVoltage <= getLimits().Vlow) {
        return true;
    }
    return false;
}
bool Battery::isFull()
{
    if (mBatteryType == BAT_NONE) {
        return true;
    }
    updateVoltage();
    if (mVoltage >= getLimits().Vfull) {
        return true;
    }
    return false;
}
bool Battery::isEmpty()
{
    if (mBatteryType == BAT_NONE) {
        return false;
    }
    updateVoltage();
    if (mVoltage <= getLimits().Voff) {
        return true;
    }
    return false;
}
esp_err_t Battery::setChargingEnable(bool enable)
{
    if (mHasIoExpander) {
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
    return mCalibrationValid;
}

esp_err_t Battery::loadCalFile() {
    if (ffsutil::fileExist(CAL_FILE)) {
        std::ifstream inputFile;
        inputFile.open(CAL_FILE, std::fstream::in);
        if(!inputFile){
            ESP_LOGW(TAG, "file not present: %s", CAL_FILE);
            return ESP_ERR_NOT_FOUND;
        }
        StaticJsonDocument<JSON_DOC_SIZE> doc;
        DeserializationError error = deserializeJson(doc, inputFile);
            
        if (error) {
            ESP_LOGE(TAG, "Parsing %s failed with %s!", CAL_FILE, error.c_str());
        }
        // convert JSON calibration file to map
        JsonObject root = doc.as<JsonObject>();
        for (JsonPair kv : root) {
            mCalData[std::stof(kv.key().c_str())] = kv.value().as<float>();
        }
        mCalibrationValid = true;
        inputFile.close();

        return ESP_OK;
    }
    else {
        ESP_LOGW(TAG, "No bat calibration file found, running without calibration!");
        FILE *f = fopen(CAL_FILE, "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to create cal file %s!", CAL_FILE);
            return ESP_ERR_NO_MEM;
        } 
        fprintf(f, "%s", "{}");
        fclose(f);
    }
    return ESP_OK;
}

esp_err_t Battery::clearCal() {
    ESP_LOGI(TAG, "Clearing battery calibration");
    mCalibrationValid = false;
    mCalData.clear();
    FILE *f = fopen(CAL_FILE, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to clear battery calibration file %s", CAL_FILE);
        return ESP_ERR_NO_MEM;
    } 
    fprintf(f, "%s", "{}");
    fclose(f);
    return ESP_OK;
}
esp_err_t Battery::printCal(String& buf) const {

    FILE *f = fopen(CAL_FILE, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "file not present: %s", CAL_FILE);
        return ESP_ERR_NOT_FOUND;
    } else {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

        char *input = reinterpret_cast<char*>(malloc(fsize + 1));
        if (!input) {
            ESP_LOGE(TAG, "Not enough memory for reading %s", CAL_FILE);
            fclose(f);
            return ESP_ERR_NO_MEM;
        }
        memset(input, 0, fsize+1);
        fread(input, fsize, 1, f);
        ESP_LOGI(TAG, "cal file (size %ld): %s", fsize, input);
        buf = input;
        free(input);
        fclose(f);
    }
    return ESP_OK;
}
esp_err_t Battery::updateCal(const char* buf) {
    static StaticJsonDocument<JSON_DOC_SIZE> newCfg;
    newCfg.clear();

    DeserializationError error = deserializeJson(newCfg, buf);
    if (error) {
        ESP_LOGE(TAG, "Parsing config failed with %s!", error.c_str());
        return ESP_ERR_INVALID_ARG;
    }

    String cal;
    if (esp_err_t err = printCal(cal)) {
        ESP_LOGE(TAG, "Failed to load existing calibration data");
        return err;
    }
    StaticJsonDocument<JSON_DOC_SIZE> doc;
    error = deserializeJson(doc, cal);
    if (error) {
        ESP_LOGE(TAG, "Parsing existing config failed with %s!", error.c_str());
        return ESP_ERR_INVALID_ARG;
    }

    jsonutils::merge(doc, newCfg);

    // convert JSON calibration file to map
    JsonObject root = doc.as<JsonObject>();
    for (JsonPair kv : root) {
        mCalData[std::stof(kv.key().c_str())] = kv.value().as<float>();
    }
    mCalibrationValid = true;

    std::ofstream calFile;
    calFile.open(CAL_FILE, std::fstream::out);
    if(!calFile){
        ESP_LOGW(TAG, "Failed to open file: %s", CAL_FILE);
        return ESP_ERR_NOT_FOUND;
    }
    if (serializeJsonPretty(doc, calFile) == 0) {
        ESP_LOGE(TAG, "Failed serialize JSON config!");
        calFile.close();
        return ESP_FAIL;
    }
    calFile.close();
    return ESP_OK;
}

