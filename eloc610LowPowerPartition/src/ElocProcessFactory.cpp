/*
 * Created on Sun Mar 03 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Fabian Lindner
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

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "project_config.h"
#include "config.h"
#include "ElocConfig.hpp"

#include "ElocProcessFactory.hpp"

ElocProcessFactory elocProcessing;

static const char* TAG = "ElocProcessing";

ElocProcessFactory::ElocProcessFactory():
    mInput(), 
#ifdef EDGE_IMPULSE_ENABLED
    mEdgeImpulse(I2S_DEFAULT_SAMPLE_RATE),
#endif
    mWav_writer(),
    mCurrentState(RecState::recordOff_detectOff),
    mReq_evt_queue(nullptr)
{
    mReq_evt_queue = xQueueCreate(10, sizeof(RecState));
    assert(mReq_evt_queue != nullptr);
    xQueueReset(mReq_evt_queue);

}

ElocProcessFactory::~ElocProcessFactory()
{
}

esp_err_t ElocProcessFactory::begin() { 
    
    /**
     * @note Using MicUseTimingFix == true or false doesn't seem to effect ICS-43434 mic
     */
    mInput.init(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift,
                               getConfig().listenOnly, getMicInfo().MicUseTimingFix);

    return ESP_OK; 
}

esp_err_t ElocProcessFactory::end() { 
    return ESP_OK;
}

void ElocProcessFactory::testInput() {

    if (!getConfig().testI2SClockInput) return;

    ESP_LOGV(TAG, "Func: %s", __func__);

    for (uint32_t i = 1000; i < 34000; i = i + 2000) {
        i2s_mic_Config.sample_rate = i;
        i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL;

        mInput.init(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);
        mInput.install_and_start();
        delay(100);
        ESP_LOGI(TAG, "Samplesrate %d Hz --> Clockrate: %f", i, i2s_get_clk(I2S_NUM_0));
        mInput.uninstall();
        delay(100);
    }

    delay(100);
}

BaseType_t ElocProcessFactory::queueNewMode(RecState newMode) {
    return xQueueSend(mReq_evt_queue, &newMode, (TickType_t)0);
}

BaseType_t ElocProcessFactory::queueNewModeISR(RecState newMode) {
    return xQueueSendFromISR(mReq_evt_queue, &newMode, (TickType_t)0);
}
