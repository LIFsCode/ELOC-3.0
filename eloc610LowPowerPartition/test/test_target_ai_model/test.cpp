/*
 * Created on Tue 20 Feb 2024
 *
 * Test that reads in wav files from SD card & runs them through the Edge
 * Impulse model
 *
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
#include <sys/stat.h>
#include <unity.h>
#include <vector>
#include "ESP32Time.h"
#include "EdgeImpulse.hpp"  // This file includes trumpet_inferencing.h
#include "SDCardSDIO.h"
#include "WAVFileReader.h"
#include "ffsutils.h"
#include "edge-impulse-sdk/dsp/numpy_types.h"
#include "ei_inference.h"
#include "project_config.h"

#include "test_samples.h"

#define EIDSP_QUANTIZE_FILTERBANK 0

ESP32Time timeObject;
SDCardSDIO sd_card;
TaskHandle_t ei_TaskHandler = nullptr;
EdgeImpulse edgeImpulse(I2S_DEFAULT_SAMPLE_RATE);

int microphone_audio_signal_get_data(size_t offset, size_t length,
                                     float *out_ptr) {
  return edgeImpulse.microphone_audio_signal_get_data(offset, length, out_ptr);
}

extern "C" {
void app_main(void);
}

void setUp(void) {}

void tearDown(void) {}

void test_init() {
  timeObject.setTime(BUILD_TIME_UNIX, 0);
  TEST_ASSERT_EQUAL(0, timeObject.setTimeZone(TIMEZONE_OFFSET));

  TEST_ASSERT_TRUE(TEST_SAMPLE_LENGTH >= EI_CLASSIFIER_RAW_SAMPLE_COUNT);
  if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH) {
      #warning "TEST_SAMPLE length is greater than the Edge Impulse model length, applying down-sampling"
  }
}

void test_setup_SD() {
  while (!sd_card.isMounted()) {
    printf("Remove jumper from SD card!\n");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    sd_card.init("/sdcard");
  }

  TEST_ASSERT_TRUE(sd_card.isMounted());
}

void test_get_aiModel() {
  auto s = edgeImpulse.get_aiModel();
  TEST_ASSERT_NOT_NULL(s);
  // Should be at least length 5
  TEST_ASSERT_GREATER_THAN(5, s.length());
  printf("Model name: %s\n", s.c_str());
}

void test_output_inferencing_settings() {
  edgeImpulse.output_inferencing_settings();
}

void test_inference_categories() {
  for (auto i = 0; i < EI_CLASSIFIER_NN_OUTPUT_COUNT; i++) {
    auto category = edgeImpulse.get_ei_classifier_inferencing_categories(i);
    printf("Category %d: %s", i, category);
    TEST_ASSERT_NOT_NULL(category);
  }
}

void run_inference() {
  // Run stored audio samples through the model to test it
  // Use non-continuous process for this
  edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = {0};

  // Artificially fill buffer with test data
  auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  auto skip_current = ei_skip_rate;  // Make sure to fill first sample, then
                                     // start skipping if needed

  for (auto i = 0; i < test_array_size; i++) {
    printf("Running test category: %s", test_array_categories[i]);

    for (auto test_sample_count = 0, inference_buffer_count = 0;
         (test_sample_count < TEST_SAMPLE_LENGTH) &&
         (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT);
         test_sample_count++) {
      if (skip_current >= ei_skip_rate) {
        edgeImpulse.inference.buffers[0][inference_buffer_count++] =
            test_array[i][test_sample_count];
        skip_current = 1;
      } else {
        skip_current++;
      }
    }

    // Mark buffer as ready
    // Mark active buffer as inference.buffers[1], inference run on inactive
    // buffer
    edgeImpulse.inference.buf_select = 1;
    edgeImpulse.inference.buf_count = 0;
    edgeImpulse.inference.buf_ready = 1;

    TEST_ASSERT_EQUAL(EI_IMPULSE_OK,
                      edgeImpulse.run_classifier(&signal, &result));

    // print the predictions
    printf("Test model predictions:");
    printf("    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
           result.timing.dsp, result.timing.classification,
           result.timing.anomaly);
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
      printf("    %s: %f", result.classification[ix].label,
             result.classification[ix].value);

      if (strcmp(result.classification[ix].label, "trumpet") == 0 &&
          strcmp(test_array_categories[i], "trumpet") == 0) {
        if (result.classification[ix].value < AI_RESULT_THRESHOLD) {
          printf("Test of trumpet sample appears to be poor, check model!");
        }
      }
    }
  }

  // Free buffers as the buffer size differs
  edgeImpulse.free_buffers();
}

void test_wav_file() {
  // Run inference on test files
  if (ffsutil::fileExist("/sdcard/wav_test_files/16K_Trumpet2.wav")) {
    FILE *fp = fopen("/sdcard/wav_test_files/16K_Trumpet2.wav", "rb");
    // create a new wave file writer
    WAVFileReader *reader = new WAVFileReader(fp);

    TEST_ASSERT_EQUAL(16, reader->bit_depth());
    TEST_ASSERT_EQUAL(1, reader->num_channels());
    TEST_ASSERT_EQUAL(16000, reader->sample_rate());
    // Expect a min of 4000 samples, i.e. 1 sec at 4KHz
    TEST_ASSERT_GREATER_OR_EQUAL(4000, reader->get_number_samples());
    fclose(fp);
  } else {
    printf("File /sdcard/wav_test_files/16K_Trumpet2.wav not found\n");
  }

  if (ffsutil::fileExist("/sdcard/wav_test_files/8K_Trumpet1.wav")) {
    FILE *fp = fopen("/sdcard/wav_test_files/8K_Trumpet1.wav", "rb");
    // create a new wave file writer
    WAVFileReader *reader = new WAVFileReader(fp);

    TEST_ASSERT_EQUAL(16, reader->bit_depth());
    TEST_ASSERT_EQUAL(1, reader->num_channels());
    TEST_ASSERT_EQUAL(8000, reader->sample_rate());
    // Expect a min of 4000 samples, i.e. 1 sec at 4KHz
    TEST_ASSERT_GREATER_OR_EQUAL(4000, reader->get_number_samples());
    fclose(fp);
  } else {
    printf("File /sdcard/wav_test_files/8K_Trumpet1.wav not found\n");
  }
}

void run_inference_from_file(WAVFileReader *reader) {
  // Run stored audio samples through the model to test it
  // Use non-continuous process for this
  edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data = &microphone_audio_signal_get_data;
  ei_impulse_result_t result = {0};

  // Artificially fill buffer with test data
  auto ei_skip_rate = reader->sample_rate() / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  auto skip_current = ei_skip_rate;  // Make sure to fill first sample, then
                                     // start skipping if needed

  if (ei_skip_rate != 1) {
    printf("Skip rate = %d\n", ei_skip_rate);
  }

  for (auto test_sample_count = 0, inference_buffer_count = 0;
        (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        test_sample_count++) {
    if (skip_current >= ei_skip_rate) {
      // Copy one int16_t sample from file to buffer
      reader->read(&edgeImpulse.inference.buffers[0][inference_buffer_count++], 1);
      skip_current = 1;
    } else {
      // Advance one int16_t sample from the file & discard
      int16_t discard_sample[1];
      reader->read(&discard_sample[0], 1);
      skip_current++;
    }
  }

  // Mark buffer as ready
  // Mark active buffer as inference.buffers[1], inference run on inactive
  // buffer
  edgeImpulse.inference.buf_select = 1;
  edgeImpulse.inference.buf_count = 0;
  edgeImpulse.inference.buf_ready = 1;

  TEST_ASSERT_EQUAL(EI_IMPULSE_OK, edgeImpulse.run_classifier(&signal, &result));

  // print the predictions
  printf("Test model predictions: \n");
  printf("    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.) \n",
          result.timing.dsp, result.timing.classification,
          result.timing.anomaly);
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    printf("    %s: %f \n", result.classification[ix].label,
            result.classification[ix].value);
  }

  // Free buffers as the buffer size differs
  edgeImpulse.free_buffers();
}

void test_ai_model() {
  // Get a list of wav files on the SD card
  const std::string file_path = "/sdcard/wav_test_files";

  // Check directory exists, create if not
  if (!ffsutil::folderExists(file_path.c_str())) {
        printf("%s does not exist, creating empty folder\n", file_path.c_str());
        mkdir(file_path.c_str(), 0777);
  }

  std::vector<std::string> wav_files;
  auto file_count = ffsutil::getFileListWithExtension(file_path.c_str(), "wav", wav_files);

  if (file_count == 0) {
    printf("No wav files found in /sdcard/wav_test_files\n");
    return;
  }

  for (auto i {0}; i < file_count; i++) {
    std::string fname = file_path + "/" + wav_files.at(i);
    printf("Testing file %s \n", fname.c_str());
    FILE *fp = fopen(fname.c_str(), "rb");
    // create a new wave file writer & ques file to start of data
    WAVFileReader *reader = new WAVFileReader(fp);
    run_inference_from_file(reader);
    fclose(fp);
  }
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init);
  RUN_TEST(test_get_aiModel);
  RUN_TEST(test_setup_SD);
  RUN_TEST(test_output_inferencing_settings);
  RUN_TEST(test_wav_file);
  RUN_TEST(test_ai_model);

  return UNITY_END();
}

void app_main(void) { runUnityTests(); }
