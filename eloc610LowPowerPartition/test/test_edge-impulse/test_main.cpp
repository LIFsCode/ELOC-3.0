/**
 * @file test_main.cpp
 * @author Owen O'Hehir
 * @brief Test Edge Impulse code
 * @version 0.1
 * @date 2023-09-27
 *
 * 
 */

#include <Arduino.h>
#include <unity.h>
// #include "config.h"   // for I2S_DEFAULT_SAMPLE_RATE

#define EIDSP_QUANTIZE_FILTERBANK 0

#include <trumpet_inferencing.h>
#include <test_samples.h>

// /** Audio buffers, pointers and selectors */
typedef struct
{
  int16_t *buffer;
  uint8_t buf_ready;
  uint32_t buf_count;
  uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
static bool record_status = true;

/**
 * Get raw audio signal data
 */
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
   // ESP_LOGI(TAG,"microphone_audio_signal_get_data()");
   numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);

    return 0;
}


void setUp(void)
{
}

void tearDown(void)
{
  // clean stuff up here
}


void test_sample_length(void)
{
  // TEST_ASSERT_EQUAL(EI_CLASSIFIER_RAW_SAMPLE_COUNT, TEST_SAMPLE_LENGTH);
}

void test_trumpet(void)
{
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = {0};

    inference.buffer = (int16_t *)heap_caps_malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (!inference.buffer)
    {
      // Skip the rest of the test
      return;
    }

    // Artificially fill buffer with test data
    for (auto i = 0; i < TEST_SAMPLE_LENGTH; i++)
    {
      inference.buffer[i] = trumpet_test[i];
    }

    // Mark buffer as ready
    inference.buf_count = 0;
    inference.buf_ready = 1;

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK)
    {
      return;
    }

    TEST_ASSERT_FLOAT_WITHIN(0.05, 0.95, result.classification[1].value);

    // Free buffer
    if (inference.buffer)
      heap_caps_free(inference.buffer);
  }

  extern "C"
  {
    void app_main(void);
  }

  void app_main()
  {
    initArduino();
    // NOTE!!! Wait for >2 secs
    // if board doesn't support software reset via Serial.DTR/RTS
    delay(2000);

    UNITY_BEGIN(); // IMPORTANT LINE!
    
    RUN_TEST(test_sample_length);
    RUN_TEST(test_trumpet);
    
    UNITY_END(); // stop unit testing
  }