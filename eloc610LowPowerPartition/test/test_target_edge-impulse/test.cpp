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
#include "ei_inference.h"
#include "project_config.h"
#include "test_samples.h"
#include "EdgeImpulse.hpp"              // This file includes trumpet_inferencing.h
#include "edge-impulse-sdk/dsp/numpy_types.h"
#include "test_samples.h"

#define EIDSP_QUANTIZE_FILTERBANK 0

EdgeImpulse edgeImpulse(I2S_DEFAULT_SAMPLE_RATE);
TaskHandle_t ei_TaskHandler = nullptr;

int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  return edgeImpulse.microphone_audio_signal_get_data(offset, length, out_ptr);
}

static void ei_callback_func() {
}

extern "C" {
void app_main(void);
}

void setUp(void) {}

void tearDown(void) {}

void test_init() {
  // "TEST_SAMPLE_LENGTH must be at least equal to EI_CLASSIFIER_RAW_SAMPLE_COUNT"
  TEST_ASSERT_TRUE(TEST_SAMPLE_LENGTH >= EI_CLASSIFIER_RAW_SAMPLE_COUNT);

  if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH) {
      #warning "TEST_SAMPLE length is greater than the Edge Impulse model length, applying down-sampling"
  }
}

void test_output_inferencing_settings() {
  edgeImpulse.output_inferencing_settings();
}

void test_inference_categories() {
  for (auto i = 0; i < EI_CLASSIFIER_NN_OUTPUT_COUNT; i++) {
    auto category =  edgeImpulse.get_ei_classifier_inferencing_categories(i);
    printf("Category %d: %s", i, category);
    TEST_ASSERT_NOT_NULL(category);
  }
}

void test_get_aiModel() {
  auto s = edgeImpulse.get_aiModel();
  TEST_ASSERT_NOT_NULL(s);
  // Should be at least length 5
  TEST_ASSERT_GREATER_THAN(5, s.length());
}

void test_buffer_setup() {
  TEST_ASSERT_TRUE(edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT));
}

void test_inference() {
    // Run stored audio samples through the model to test it
    // Use non-continuous process for this

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    // Artificially fill buffer with test data
    auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    auto skip_current = ei_skip_rate;   // Make sure to fill first sample, then start skipping if needed

    for (auto i = 0; i < test_array_size; i++) {
        printf("Running test category: %s", test_array_categories[i]);

        for (auto test_sample_count = 0, inference_buffer_count = 0; (test_sample_count < TEST_SAMPLE_LENGTH) &&
                (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT); test_sample_count++) {
            if (skip_current >= ei_skip_rate) {
                edgeImpulse.inference.buffers[0][inference_buffer_count++] = test_array[i][test_sample_count];
                skip_current = 1;
            } else {
                skip_current++;
            }
        }

        // Mark buffer as ready
        // Mark active buffer as inference.buffers[1], inference run on inactive buffer
        edgeImpulse.inference.buf_select = 1;
        edgeImpulse.inference.buf_count = 0;
        edgeImpulse.inference.buf_ready = 1;

        TEST_ASSERT_EQUAL(EI_IMPULSE_OK, edgeImpulse.run_classifier(&signal, &result));

        // print the predictions
        printf("Test model predictions:");
        printf("    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                result.timing.dsp, result.timing.classification, result.timing.anomaly);
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
            printf("    %s: %f", result.classification[ix].label, result.classification[ix].value);

            if (strcmp(result.classification[ix].label, "trumpet") == 0 &&
                strcmp(test_array_categories[i], "trumpet") == 0) {
                if (result.classification[ix].value < AI_RESULT_THRESHOLD) {
                    printf("Test of trumpet sample appears to be poor, check model!");
                }
            }
        }
    }
}

void test_start_ei_thread() {
  TEST_ASSERT_EQUAL(ESP_OK, edgeImpulse.start_ei_thread(ei_callback_func));
}

void test_ei_thread_status_change() {
  TEST_ASSERT_EQUAL(EdgeImpulse::Status::running, edgeImpulse.get_status());
  edgeImpulse.set_status(EdgeImpulse::Status::not_running);
  TEST_ASSERT_EQUAL(false, edgeImpulse.get_status());
}

void test_detectedEvents() {
  TEST_ASSERT_EQUAL(0, edgeImpulse.get_detectedEvents());
  edgeImpulse.increment_detectedEvents();
  TEST_ASSERT_EQUAL(1, edgeImpulse.get_detectedEvents());
}

void test_free_buffers() {
  edgeImpulse.free_buffers();
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init);
  RUN_TEST(test_output_inferencing_settings);
  RUN_TEST(test_inference_categories);
  RUN_TEST(test_get_aiModel);
  RUN_TEST(test_buffer_setup);
  RUN_TEST(test_inference);
  RUN_TEST(test_start_ei_thread);
  RUN_TEST(test_ei_thread_status_change);
  RUN_TEST(test_detectedEvents);
  RUN_TEST(test_free_buffers);
  return UNITY_END();
}

void app_main(void) { runUnityTests(); }
