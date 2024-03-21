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
  struct tm tm = {0, 0, 0, 0, 0, 0, 0, 0, 0};
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

void test_setTime() {
  ESP32Time timeObject;
  struct tm tm = {0, 0, 0, 0, 0, 0, 0, 0, 0};

  strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
  time_t timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  TEST_ASSERT_EQUAL(timeSinceEpoch , timeObject.getEpoch());
  TEST_ASSERT_EQUAL((tm.tm_year + 1900), timeObject.getYear());     // Convert to calendar year
  TEST_ASSERT_EQUAL(tm.tm_mon + 1,  timeObject.getMonth());         // Jan = 0
  TEST_ASSERT_EQUAL(tm.tm_mday, timeObject.getDay());
  TEST_ASSERT_EQUAL(tm.tm_hour, timeObject.getHour(true));          // true -> 24 hr clock
  TEST_ASSERT_EQUAL(tm.tm_min, timeObject.getMinute());
  TEST_ASSERT_EQUAL(tm.tm_sec, timeObject.getSecond());

  strptime("Jan 1 2024 01:59:59", "%b %d %Y %H:%M:%S %Y", &tm);
  timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  TEST_ASSERT_EQUAL(timeSinceEpoch , timeObject.getEpoch());
  TEST_ASSERT_EQUAL((tm.tm_year + 1900), timeObject.getYear());     // Convert to calendar year
  TEST_ASSERT_EQUAL(tm.tm_mon + 1,  timeObject.getMonth());         // Jan = 0
  TEST_ASSERT_EQUAL(tm.tm_mday, timeObject.getDay());
  TEST_ASSERT_EQUAL(tm.tm_hour, timeObject.getHour(true));          // true -> 24 hr clock
  TEST_ASSERT_EQUAL(tm.tm_min, timeObject.getMinute());
  TEST_ASSERT_EQUAL(tm.tm_sec, timeObject.getSecond());

  strptime("Dec 31 2024 23:59:59", "%b %d %Y %H:%M:%S %Y", &tm);
  timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  TEST_ASSERT_EQUAL(timeSinceEpoch , timeObject.getEpoch());
  TEST_ASSERT_EQUAL((tm.tm_year + 1900), timeObject.getYear());     // Convert to calendar year
  TEST_ASSERT_EQUAL(tm.tm_mon + 1,  timeObject.getMonth());         // Jan = 0
  TEST_ASSERT_EQUAL(tm.tm_mday, timeObject.getDay());
  TEST_ASSERT_EQUAL(tm.tm_hour, timeObject.getHour(true));          // true -> 24 hr clock
  TEST_ASSERT_EQUAL(tm.tm_min, timeObject.getMinute());
  TEST_ASSERT_EQUAL(tm.tm_sec, timeObject.getSecond());
}

void test_getDateTimeFilename() {
  ESP32Time timeObject;
  struct tm tm = {0, 0, 0, 0, 0, 0, 0, 0, 0};

  strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
  time_t timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  char s[51];
  strftime(s, 50, "%Y-%m-%d_%H_%M_%S", &tm);
  String expected = String(s);
  String actual = timeObject.getDateTimeFilename();
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());

  strptime("Jan 1 2024 01:59:59", "%b %d %Y %H:%M:%S %Y", &tm);
  timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  strftime(s, 50, "%Y-%m-%d_%H_%M_%S", &tm);
  expected = String(s);
  actual = timeObject.getDateTimeFilename();
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());

  strptime("Dec 31 2024 23:59:59", "%b %d %Y %H:%M:%S %Y", &tm);
  timeSinceEpoch = mktime(&tm);
  timeObject.setTime(timeSinceEpoch);

  strftime(s, 50, "%Y-%m-%d_%H_%M_%S", &tm);
  expected = String(s);
  actual = timeObject.getDateTimeFilename();
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());
}

void test_accuracy_esp32time() {
  bool TEST_LEVEL = 0;

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
  RUN_TEST(test_setTime);
  RUN_TEST(test_getDateTimeFilename);
  // RUN_TEST(test_accuracy_freertos);
  // RUN_TEST(test_accuracy_esp32time);
  return UNITY_END();
}

void app_main(void) {
    runUnityTests();
}
