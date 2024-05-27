/*
 * Created on Tue 20 Feb 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Owen O'Hehir
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

#include <Arduino.h>
#include <unity.h>
#include "esp_pm.h"
#include "esp32/clk.h"
#include "esp_timer.h"
#include "project_config.h"

extern "C" {
    void app_main(void);
}

void setUp(void) {
}

void tearDown(void) {
}
void test_cpu_freq_change() {
    for (auto i = 1; i < 4; i++) {
        esp_pm_config_esp32_t cfg = {i * 80, 10, true};
        esp_err_t err = esp_pm_configure(&cfg);
        TEST_ASSERT_EQUAL(ESP_OK, err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        TEST_ASSERT_EQUAL_INT(i * 80,  esp_clk_cpu_freq()/ 1000000);
    }
}
void test_cpu_freq_change_time() {
    auto cpu_freq = 0;

    for (auto i = 0; i < 20; i++) {
        cpu_freq++;

        if (cpu_freq > 3) {
            cpu_freq = 1;
        }

        auto time_start = esp_timer_get_time();
        esp_pm_config_esp32_t cfg = {cpu_freq * 80, 10, true};
        esp_err_t err = esp_pm_configure(&cfg);
        TEST_ASSERT_EQUAL(ESP_OK, err);

        while ((esp_clk_cpu_freq()/ 1000000) != cpu_freq * 80) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        auto time_end = esp_timer_get_time();

        printf("Time taken to change CPU freq: %lld\n", time_end - time_start);
    }
}


int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cpu_freq_change);
  RUN_TEST(test_cpu_freq_change_time);
  return UNITY_END();
}

/**
 * espidf framework main function
 */
void app_main(void) {
  runUnityTests();
}
