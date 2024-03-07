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
#include <ESP32Time.h>

#define TEST_OUTPUT GPIO_NUM_33

#ifndef BUILDDATE
#define BUILDDATE  __DATE__ " " __TIME__""
#endif

extern "C" {
void app_main(void);
}

void setUp(void) {
}

void tearDown(void) {
}

void test_init() {
  ESP32Time timeObject;
  struct tm tm;
  strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
  time_t timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);
  TEST_ASSERT_NOT_EQUAL(0, timeObject.getEpoch());
}

void test_accuracy_freertos() {
  gpio_set_direction(TEST_OUTPUT, GPIO_MODE_OUTPUT);
  bool TEST_LEVEL = 0;

  // 0.50002 Hz
  for (auto i = 0; i < 500; i++) {
    TEST_LEVEL = !TEST_LEVEL;
    gpio_set_level(TEST_OUTPUT, TEST_LEVEL);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

void test_accuracy_esp32time() {
  ESP32Time timeObject;
  struct tm tm;
  strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
  time_t timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);
  auto millis = timeObject.getSystemTimeMS();
  TEST_ASSERT_NOT_EQUAL(0, timeObject.getSystemTimeMS());

  // 0.50176Hz
  for (auto i = 0; i < 500; i++) {
    TEST_LEVEL = !TEST_LEVEL;
    gpio_set_level(TEST_OUTPUT, TEST_LEVEL);
    while (millis + 1000 > timeObject.getSystemTimeMS()) {
    }
    millis = timeObject.getSystemTimeMS();
  }
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init);
  RUN_TEST(test_accuracy_freertos);
  RUN_TEST(test_accuracy_esp32time);
  return UNITY_END();
}

void app_main(void) {
    runUnityTests();
}
