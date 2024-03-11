/*
 * Created on Tue 20 Feb 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Owen O'Hehir
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Arduino.h>
#include <unity.h>
// #include <driver/i2s.h>
#include "I2SMEMSSampler.h"
#include "project_config.h"

// I2SMEMSSampler.h requires these to be defined
TaskHandle_t i2s_TaskHandler;
TaskHandle_t ei_TaskHandler;

// i2s config for reading from I2S
i2s_config_t i2s_mic_Config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_DEFAULT_SAMPLE_RATE, // fails when hardcoded to 22050
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,

#ifdef I2S_DEFAULT_CHANNEL_FORMAT_LEFT
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
#else
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
#endif

    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = I2S_INTR_PIRO,
    .dma_buf_count =
        I2S_DMA_BUFFER_COUNT, //  so 2000 sample  buffer at 16khz sr gives us
                              //  125ms to do our writing
    .dma_buf_len = I2S_DMA_BUFFER_LEN, //  8 buffers gives us half  second
    .use_apll =
        true, //  the only thing that works with LowPower/APLL is 16khz 12khz??
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0,
    //  .mclk_multiple =I2S_MCLK_MULTIPLE_DEFAULT,   //
    //  I2S_MCLK_MULTIPLE_DEFAULT= 0,       /*!< Default value. mclk =
    //  sample_rate * 256 */ .bits_per_chan=I2S_BITS_PER_CHAN_DEFAULT
};

// i2s microphone pins
i2s_pin_config_t i2s_mic_pins = {.mck_io_num = I2S_PIN_NO_CHANGE,
                                 .bck_io_num = I2S_MIC_SERIAL_CLOCK,
                                 .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
                                 .data_out_num = I2S_PIN_NO_CHANGE,
                                 .data_in_num = I2S_MIC_SERIAL_DATA};

extern "C" {
void app_main(void);
}

void setUp(void) {
}

void tearDown(void) {}

void test_init() {
  I2SMEMSSampler input;
  input.init(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, I2S_DEFAULT_BIT_SHIFT, false, true);
  TEST_ASSERT_EQUAL(ESP_OK, input.install_and_start());
  delay(100);
  TEST_ASSERT_EQUAL(ESP_OK, input.uninstall());
  delay(100);
}

void test_sample_rates()
{
    I2SMEMSSampler input;

    for (uint32_t i = 1000; i < 34000; i = i + 2000) {
        i2s_mic_Config.sample_rate = i;
        i2s_mic_Config.use_apll = true;
        input.init(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, I2S_DEFAULT_BIT_SHIFT, false, true);
        TEST_ASSERT_EQUAL(ESP_OK, input.install_and_start());
        delay(100);
        TEST_ASSERT_EQUAL(ESP_OK, input.uninstall());
        delay(100);
    }

    delay(100);
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init);
  RUN_TEST(test_sample_rates);
  return UNITY_END();
}

void app_main(void) {
    runUnityTests();
}
