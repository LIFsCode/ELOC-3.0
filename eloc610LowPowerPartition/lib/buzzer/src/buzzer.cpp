/*
 * Created on Sun Mar 12 2023
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



#include "buzzer.h"


#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_system.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/ledc.h"


#define LEDC_TIMER LEDC_TIMER_0
#define DUTY_CYCLE 50


#define DEFAULT_FREQ 440
#define DEFAULT_RESOLUTION 10
#define DEFAULT_CHANNEL 0
#define DEFAULT_FREQ 440


#define TAG "BUZZER"


const int GPIO_SENSE_BIT = BIT0;

BuzzerBase::BuzzerBase(unsigned int channel, unsigned int resolution, unsigned int pin, unsigned int defaultFreq, bool speedModeHighLow) :
    mPin(pin), mChannel(channel), mResolution(resolution), mFreq(defaultFreq), mSpeedModeHighLow(speedModeHighLow)
{
    this->init();
}

BuzzerBase::BuzzerBase(unsigned int pin) :
    mPin(pin), mChannel(DEFAULT_CHANNEL), mResolution(DEFAULT_RESOLUTION), mFreq(DEFAULT_FREQ), mSpeedModeHighLow(true)
{
    

    this->init();
}

BuzzerBase::~BuzzerBase() {
    
}


esp_err_t BuzzerBase::init() {
    ESP_LOGI(TAG, "initialize");

    //gpio_set_direction(static_cast<gpio_num_t>(mPin), GPIO_MODE_OUTPUT);

    esp_err_t err;
    ledc_timer_config_t timer_conf;
	timer_conf.clk_cfg = LEDC_AUTO_CLK;
	timer_conf.speed_mode = GetSpeedMode();
	timer_conf.timer_num  = LEDC_TIMER;
	timer_conf.freq_hz    = mFreq;
    timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(mResolution);

    ESP_LOGI(TAG, "configure timer");

	err = ledc_timer_config(&timer_conf);

    ESP_LOGI(TAG, "configure timer done");
    ESP_ERROR_CHECK(err);

	ledc_channel_config_t ledc_conf;
	ledc_conf.gpio_num   = mPin;
	ledc_conf.speed_mode = GetSpeedMode();
	ledc_conf.channel    = static_cast<ledc_channel_t>(mChannel);
	ledc_conf.intr_type  = LEDC_INTR_DISABLE;
	ledc_conf.timer_sel  = LEDC_TIMER;
    ledc_conf.hpoint     = 0;
	ledc_conf.duty       = 0;
                                 // 50%=0x3FFF, 100%=0x7FFF for 15 Bit
	                            // 50%=0x01FF, 100%=0x03FF for 10 Bit
                                
    ESP_LOGI(TAG, "configure channel ");
	err = ledc_channel_config(&ledc_conf);
    ESP_LOGI(TAG, "configure channel done");
    ESP_ERROR_CHECK(err);
    
    return ESP_OK;
}

uint32_t BuzzerBase::CalcDutyCycle() {
    uint32_t duty = (DUTY_CYCLE * (1<<mResolution)) / 100;
    ESP_LOGI(TAG, "Calculate duty cycle to %lu", duty);
    return duty;
}
ledc_mode_t BuzzerBase::GetSpeedMode() {
    return mSpeedModeHighLow ? LEDC_HIGH_SPEED_MODE : LEDC_LOW_SPEED_MODE;
}

void BuzzerBase::cfgFrequency(unsigned int freq) {
    esp_err_t err;
    ledc_timer_config_t timer_conf;
    mFreq = freq;
	timer_conf.speed_mode = GetSpeedMode();
	timer_conf.timer_num  = LEDC_TIMER;
	timer_conf.clk_cfg = LEDC_AUTO_CLK;
	timer_conf.freq_hz    = mFreq;
    timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(mResolution);
    ESP_LOGI(TAG, "configure frequency to %u", mFreq);
    //TODO: add an error case here in case no frequency divider could be found --> adjust resolution
	ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));
}

void BuzzerBase::start() {
    ESP_LOGI(TAG, "start");
    ledc_set_duty(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel), CalcDutyCycle());
    ledc_update_duty(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel));
  //  ESP_ERROR_CHECK(ledc_set_duty_and_update(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel), CalcDutyCycle(), 0));
}
void BuzzerBase::stop() {
    ESP_LOGI(TAG, "stop");
    ledc_set_duty(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel), 0);
    ledc_update_duty(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel));
   // ESP_ERROR_CHECK(ledc_set_duty_and_update(GetSpeedMode(), static_cast<ledc_channel_t>(mChannel), 0, 0));
}

esp_err_t BuzzerBase::ledcWriteTone(uint32_t freq, uint32_t duration) {

    ESP_LOGI(TAG, "ledcWriteTone %lu", freq);
	// start
    if(mChannel > 15) {
        return 0;
    }
    if(!freq) {
        stop();
        return 0;
    }
    cfgFrequency(freq);
    start();

	vTaskDelay(duration/portTICK_PERIOD_MS);
	// stop
    stop();
    return ESP_OK;
}


esp_err_t BuzzerBase::ledcWriteNote(note_t note, uint8_t octave, uint32_t duration){
    const uint16_t noteFrequencyBase[12] = {
    //   C        C#       D        Eb       E        F       F#        G       G#        A       Bb        B
        4186,    4435,    4699,    4978,    5274,    5588,    5920,    6272,    6645,    7040,    7459,    7902
    };

    if(octave > 8 || note >= NOTE_MAX){
        return 0;
    }
    double noteFreq =  (double)noteFrequencyBase[note] / (double)(1 << (8-octave));
    return ledcWriteTone(noteFreq, duration);
}

