#include <Arduino.h>
#include <unity.h>

#define EIDSP_QUANTIZE_FILTERBANK 0

#include "trumpet_inferencing.h"
#include "test_samples.h"

/** Audio buffers, pointers and selectors */
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

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
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

void test_led_builtin_pin_number(void)
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

  // Free buffer
  if (inference.buffer)
    heap_caps_free(inference.buffer);
}

void test_led_state_high(void)
{
  // digitalWrite(LED_BUILTIN, HIGH);
  // TEST_ASSERT_EQUAL(HIGH, digitalRead(LED_BUILTIN));
}

void test_led_state_low(void)
{
  // digitalWrite(LED_BUILTIN, LOW);
  // TEST_ASSERT_EQUAL(LOW, digitalRead(LED_BUILTIN));
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

  // pinMode(LED_BUILTIN, OUTPUT);

  UNITY_BEGIN(); // IMPORTANT LINE!
  RUN_TEST(test_led_builtin_pin_number);

  uint8_t i = 0;
  uint8_t max_blinks = 5;

  while (1)
  {
    if (i < max_blinks)
    {
      RUN_TEST(test_led_state_high);
      delay(500);
      RUN_TEST(test_led_state_low);
      delay(500);
      i++;
    }
    else if (i == max_blinks)
    {
      UNITY_END(); // stop unit testing
    }
  }
}