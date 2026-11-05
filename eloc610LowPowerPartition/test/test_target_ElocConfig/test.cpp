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
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <unity.h>
#include "ElocConfig.hpp"


extern "C" {
void app_main(void);
}

void setUp(void) {
}

void tearDown(void) {

}

void test_init() {
  setFactorySerialNumber(123);
  readConfig();
}

void test_default_node_name_format() {
  TEST_ASSERT_EQUAL_STRING("ELOC_00000", formatDefaultNodeName(0).c_str());
  TEST_ASSERT_EQUAL_STRING("ELOC_00123", formatDefaultNodeName(123).c_str());
  TEST_ASSERT_EQUAL_STRING("ELOC_00250", formatDefaultNodeName(250700250).c_str());
  TEST_ASSERT_EQUAL_STRING("ELOC_23456", formatDefaultNodeName(123456).c_str());
}

void test_compiled_defaults() {
  String configJson;
  TEST_ASSERT_TRUE(printConfig(configJson, CfgType::DEFAULT_CFG));

  DynamicJsonDocument doc(3072);
  TEST_ASSERT_FALSE(deserializeJson(doc, configJson));
  TEST_ASSERT_EQUAL_INT(3600, doc["config"]["secondsPerFile"].as<int>());
  TEST_ASSERT_EQUAL_INT(80, doc["config"]["cpuMaxFrequencyMHZ"].as<int>());
  TEST_ASSERT_EQUAL_INT(80, doc["config"]["cpuMinFrequencyMHZ"].as<int>());
  TEST_ASSERT_FALSE(doc["config"]["cpuEnableLightSleep"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("ICS-43434", doc["mic"]["MicType"].as<const char*>());
  TEST_ASSERT_FALSE(doc["mic"]["MicUseAPLL"].as<bool>());
}

// Regression: once internal RAM had no contiguous 4 KB block, printConfig returned true with "{}".
void test_print_config_with_low_internal_heap() {
  const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t maxBlocks = 256;
  static void* held[maxBlocks];
  size_t heldCount = 0;

  String configJson;
  TEST_ASSERT_TRUE(configJson.reserve(3000));

  while ((heap_caps_get_largest_free_block(internalCaps) >= 4096) && (heldCount < maxBlocks)) {
    void* block = heap_caps_malloc(2048, internalCaps);
    if (block == nullptr) {
      break;
    }
    held[heldCount++] = block;
  }
  const size_t largestBlock = heap_caps_get_largest_free_block(internalCaps);
  const bool printed = printConfig(configJson);
  for (size_t i = 0; i < heldCount; i++) {
    heap_caps_free(held[i]);
  }

  TEST_ASSERT_TRUE_MESSAGE(largestBlock < 4096, "Could not use up internal RAM");
  TEST_ASSERT_TRUE(printed);
  DynamicJsonDocument doc(3072);
  TEST_ASSERT_FALSE(deserializeJson(doc, configJson));
  TEST_ASSERT_TRUE(doc.containsKey("device"));
  TEST_ASSERT_TRUE(doc.containsKey("config"));
  TEST_ASSERT_TRUE(doc.containsKey("mic"));
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init);
  RUN_TEST(test_default_node_name_format);
  RUN_TEST(test_compiled_defaults);
  RUN_TEST(test_print_config_with_low_internal_heap);
  return UNITY_END();
}

void app_main(void) {
    runUnityTests();
}
