#include <limits>
#include "esp_log.h"
#include "SDCardSDIO.h"
#include "WAVFileWriter.h"
#include "SDCardSDIO.h"

extern SDCardSDIO sd_card;

static const char *TAG = "WAVFileWriter";

/**
 * @note Ideally recording time would be retrieved with esp_timer_get_time()
 * but found to be inaccurate. Hence, using the ESP32Time.
*/
extern ESP32Time timeObject;
extern SDCardSDIO sd_card;

WAVFileWriter::WAVFileWriter()
{
  ESP_LOGV(TAG, "Func: %s", __func__);

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
  #endif

    return false;
  }

  buffers[0][0] = {0};
  buffers[1][0] = {0};

  return true;
}

bool WAVFileWriter::write_wav_header() {
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

/**
 * Creates filename with the format:
 * /sdcard/eloc/not_set1706206080042/not_set1706206080042_2024-01-25_18_09_01.wav
 */
String WAVFileWriter::createFilename() {
  String fname = "/sdcard/eloc/";
  fname += gSessionIdentifier;
  fname += "/";
  fname += gSessionIdentifier;
  fname += "_";
  fname += timeObject.getDateTimeFilename();
  fname += ".wav";
  ESP_LOGI(TAG, "Filename: %s", fname.c_str());
  return fname;
}

bool WAVFileWriter::open_file() {
    m_fp = nullptr;

    if (sd_card.isMounted() == false) {
        ESP_LOGE(TAG, "SD Card is not mounted");
        return false;
    } else {
        float sdCardFreeSpaceGB = sd_card.freeSpaceGB();

        if (sdCardFreeSpaceGB < 0.1) {
            ESP_LOGE(TAG, "SD Card is full");
            return false;
        }
    }

    auto fname = createFilename();
    m_fp = fopen(fname.c_str(), "wb");

    if (m_fp == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return false;
    } else {
        if (write_wav_header() == false) {
            ESP_LOGE(TAG, "Failed to set file handle");
            return false;
        }
        enable_wav_file_write = true;
    }

    return true;
}

void WAVFileWriter::write() {
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

void WAVFileWriter::start_write_thread() {
  ESP_LOGV(TAG, "Func: %s", __func__);

  static auto old_secs_written = 0;
  static uint32_t slowestWriteSpeed  = std::numeric_limits<uint32_t>::max();  // set to max
  static int64_t longestWriteMs  = std::numeric_limits<uint32_t>::max();      // set to max

  if (m_fp != nullptr) {
    ESP_LOGE(TAG, "File pointer is not NULL");
    enable_wav_file_write = false;
  } else if (open_file() == false) {
    ESP_LOGE(TAG, "Failed to open file for writing");
    enable_wav_file_write = false;
  }

  while (enable_wav_file_write) {
    if (xTaskNotifyWait(
                          0 /* ulBitsToClearOnEntry */,
                          0 /* ulBitsToClearOnExit */,
                          NULL /* pulNotificationValue */,
                          portMAX_DELAY /* xTicksToWait*/) == pdTRUE) {
      if (this->buf_ready) {
        int64_t start_time = esp_timer_get_time();
        if (m_fp == nullptr) {
          ESP_LOGE(TAG, "enable_wav_file_write enabled & file pointer == nullptr");
        } else {
          this->write();
        }
        int64_t end_time = esp_timer_get_time();
        int64_t writeDurationMs =  (end_time - start_time)/1000;
        // gives the speed in KByte/s (size in Byte, time in ms)
        uint32_t speed = sizeof(int16_t) * buffer_size_in_samples / writeDurationMs;
        if (speed < slowestWriteSpeed) slowestWriteSpeed = speed;
        if ( writeDurationMs > longestWriteMs) longestWriteMs = writeDurationMs;

        // Recalculate to avoid rounding errors
        recording_time_file_sec = m_file_size / (sizeof(int16_t) * m_sample_rate);
        recordingTimeSinceLastStarted_sec = timeObject.getEpoch() - recordingStartTime_sec;

        // Limit output to once every 5 secs
        if (recording_time_file_sec % 5 == 0 && recording_time_file_sec != old_secs_written) {
          ESP_LOGI(TAG, "WAV file size bytes: %u, secs: %u, WritePerf: %d KB/s, WriteTime: %lld ms, WorstCase: %d KB/s, %lld ms", m_file_size, recording_time_file_sec, speed, writeDurationMs, slowestWriteSpeed, longestWriteMs);
          old_secs_written = recording_time_file_sec;
        }

        /**
         * Have we reached the required file size OR has
         * recording been disabled & now needs to be stopped??
         */
        if ((m_file_size >= (m_sample_rate * secondsPerFile * sizeof(int16_t)) || mode == Mode::disabled)) {
          // Won't be saving to this file anymore..
          enable_wav_file_write = false;
          this->finish();
          old_secs_written = 0;
        }

        // Need to open new file for recording?
        if (m_fp == nullptr && mode == Mode::continuous) {
          if (open_file() == false) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            enable_wav_file_write = false;
          }
        }
      }
    }  // if (xTaskNotifyWait())
  }

  // Update total recording time now to avoid rounding errors
  recording_time_total_sec += timeObject.getEpoch() - recordingStartTime_sec;
  recordingTimeSinceLastStarted_sec = 0;
  wav_recording_in_progress = false;
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

  // TODO: Need error checking on the following
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
  reinterpret_cast<WAVFileWriter *>(_this)->start_write_thread();
}

int WAVFileWriter::start_wav_write_task(int secondsPerFile)
{
  ESP_LOGV(TAG, "Func: %s, secondsPerFile = %d", __func__, secondsPerFile);

  wav_recording_in_progress = true;
  recordingStartTime_sec = timeObject.getEpoch();

  if (secondsPerFile <= 0) {
    ESP_LOGE(TAG, "secondsPerFile must be > 0");
    return -1;
  }

  this->secondsPerFile = secondsPerFile;

  int ret = xTaskCreatePinnedToCore(this->start_wav_writer_wrapper, "wav_writer",
                                    1024 * 4, this, TASK_PRIO_WAV, &i2s_TaskHandler, TASK_WAV_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create wav file writer task");
    wav_recording_in_progress = false;
    return -1;
  }

  return 0;
}
