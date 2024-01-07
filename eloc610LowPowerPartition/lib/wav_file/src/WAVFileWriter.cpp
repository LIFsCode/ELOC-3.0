

#include "esp_log.h"
#include "WAVFileWriter.h"

static const char *TAG = "WAVFileWriter";

WAVFileWriter::WAVFileWriter()
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  m_fp = nullptr;

  buf_ready = 0;
  buf_select = 0;
  buf_count = 0;
}

bool WAVFileWriter::initialize(int sample_rate, int buffer_time, int ch_count /*=1*/)
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  m_sample_rate = sample_rate;
  setChannelCount(ch_count);
  setSample_rate(sample_rate);
  m_header.sample_rate = sample_rate;

  #ifdef WAV_BUFFER_IN_PSRAM
    // Make buffer size a multiple of 512
    buffer_size_in_samples = int((sample_rate * buffer_time) / 512) * 512;
    ESP_LOGI(TAG, "Allocating 2 buffers of %d bytes in PSRAM", buffer_size_in_samples);
    buffers[0] = (int16_t *)heap_caps_malloc(buffer_size_in_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  #else
    ESP_LOGI(TAG, "Allocating 2 buffers of %d bytes in RAM", wav_static_buffer_size);
    buffer_size_in_samples = wav_static_buffer_size;
    buffers[0] = wav_static_buffers[0];
  #endif

  if (buffers[0] == NULL) {
    ESP_LOGE(TAG, "Failed to allocate wav file buffer[0]");
    return false;
  }

  #ifdef WAV_BUFFER_IN_PSRAM
    buffers[1] = (int16_t *)heap_caps_malloc(buffer_size_in_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  #else
    buffers[1] = wav_static_buffers[1];
  #endif

  if (buffers[1] == NULL) {
    ESP_LOGE(TAG, "Failed to allocate wav file buffer[1]");

  #ifdef WAV_BUFFER_IN_PSRAM
      heap_caps_free(buffers[0]);
  #else
      free(buffers[0]);
  #endif

    return false;
  }

  buffers[0][0] = {0};
  buffers[1][0] = {0};

  return true;
}

bool WAVFileWriter::set_file_handle(FILE *fp)
{
  m_fp = fp;

  if (m_fp == nullptr) {
    ESP_LOGE(TAG, "File pointer is NULL");
    return false;
  }

  /**
   * @note Here a header is written to the file, with some fields left blank
   *       Could possibly modify this to write fully correct header here, knowing
   *       required seconds per file, sample rate, etc. However, this would
   *       probably writing only a partial buffer at the last write, and losing
   *       the rest of the buffer sound. Probably not ideal.
   */

  auto ret = fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  m_file_size = sizeof(wav_header_t);

  if (ret == 0)
    return false;
  else
    return true;
}

WAVFileWriter::~WAVFileWriter()
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  if (m_fp != nullptr) {
    fclose(m_fp);
  }

  if (buffers[0] != NULL) {
#ifdef WAV_BUFFER_IN_PSRAM
    heap_caps_free(buffers[0]);
#else
    free(buffers[0]);
#endif
  }

  if (buffers[1] != NULL) {
#ifdef WAV_BUFFER_IN_PSRAM
    heap_caps_free(buffers[1]);
#else
    free(buffers[1]);
#endif
  }
}

bool WAVFileWriter::buffer_is_full()
{
  this->swap_buffers();
  buf_ready = 1;

  return true;
}

void WAVFileWriter::swap_buffers()
{
  // swap buffers
  if (++buf_select > 1)
    buf_select = 0;

  buf_count = 0;

  ESP_LOGI(TAG, "buffer_active = %d", buf_select);
}

void WAVFileWriter::write()
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  auto buffer_inactive = buf_select ? 0 : 1;

  if (m_fp == nullptr) {
    ESP_LOGE(TAG, "File pointer is NULL");
    return;
  }

  fwrite(buffers[buffer_inactive], sizeof(int16_t), buffer_size_in_samples, m_fp);

  m_file_size += sizeof(int16_t) * buffer_size_in_samples;

  // Don't swap buffers here, let I2MEMSSampler::read() to do it
  buf_ready = 0;
}

void WAVFileWriter::start_write_thread()
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  static auto old_secs_written = 0;

  while (enable_wav_file_write) {
    if (xTaskNotifyWait(
                          0 /* ulBitsToClearOnEntry */,
                          0 /* ulBitsToClearOnExit */,
                          NULL /* pulNotificationValue */,
                          portMAX_DELAY /* xTicksToWait*/) == pdTRUE) {
      if (this->buf_ready) {
        if (m_fp == nullptr) {
          ESP_LOGE(TAG, "enable_wav_file_write enabled & file pointer == nullptr");
        } else {
          this->write();
        }

        // Recalculate every time to avoid rounding errors
        recording_time_file_sec = m_file_size / (sizeof(int16_t) * m_sample_rate);

        // Limit output to 1 log per second
        if (recording_time_file_sec > old_secs_written) {
          ESP_LOGI(TAG, "WAV file size bytes: %u, secs: %u", m_file_size, recording_time_file_sec);
          old_secs_written = recording_time_file_sec;
        }

        /**
         * Have we reached the required file size OR has
         * recording been disabled & now needs to be stopped??
         */
        if ((m_file_size >= (m_sample_rate * secondsPerFile * sizeof(int16_t)) || mode == Mode::disabled)) {
          // Won't be saving to this file anymore..
          enable_wav_file_write = false;

          recording_time_total_sec += recording_time_file_sec;

          this->finish();
          old_secs_written = 0;
        }
      }
    }  // if (xTaskNotifyWait())
  }

  vTaskDelete(NULL);
}

bool WAVFileWriter::finish()
{
  // Have to consider the case where file has reached its
  // max size & other buffer is being filled
  ESP_LOGI(TAG, "Finishing wav file size: %d", m_file_size);
  // now fill in the header with the correct information and write it again
  m_header.data_bytes = m_file_size - sizeof(wav_header_t);
  m_header.wav_size = m_file_size - 8;
  fseek(m_fp, 0, SEEK_SET);
  fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  fclose(m_fp);

  m_file_size = 0;
  recording_time_file_sec = 0;
  m_fp = nullptr;

  // Don't swap buffers?? Could be filling

  return true;
}

void WAVFileWriter::setSample_rate(int sample_rate)
{
  m_header.sample_rate = sample_rate;
  m_header.byte_rate = m_header.sample_rate * m_header.num_channels * (m_header.bit_depth / 8);
}

void WAVFileWriter::setChannelCount(int channel_count)
{
  m_header.num_channels = channel_count;
  // Number of bytes per second = sample_rate * num_channels * Bytes Per Sample
  m_header.byte_rate = m_header.sample_rate * 2 * channel_count;
  //                           num_channels * Bytes Per Sample
  m_header.sample_alignment = channel_count * (m_header.bit_depth / 8);
}

void WAVFileWriter::start_wav_writer_wrapper(void *_this)
{
  ((WAVFileWriter *)_this)->start_write_thread();
}

int WAVFileWriter::start_wav_write_task(int secondsPerFile)
{
  ESP_LOGV(TAG, "Func: %s, secondsPerFile = %d", __func__, secondsPerFile);

  if (secondsPerFile <= 0) {
    ESP_LOGE(TAG, "secondsPerFile must be > 0");
    return -1;
  }

  this->secondsPerFile = secondsPerFile;

  int ret = xTaskCreate(this->start_wav_writer_wrapper, "wav_file_writer", 1024 * 4, this, TASK_PRIO_WAV, &i2s_TaskHandler);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create wav file writer task");
    return -1;
  }

  return 0;
}
