#include "esp_log.h"
#include "WAVFileWriter.h"

static const char *TAG = "WAVFileWriter";

WAVFileWriter::WAVFileWriter(FILE *fp, int sample_rate, int buffer_time, int ch_count /*=1*/)
{
  ESP_LOGV(TAG, "Func: %s", __func__);
  
  m_fp = fp;
  m_sample_rate = sample_rate;

  setChannelCount(ch_count);
  setSample_rate(sample_rate);
  m_header.sample_rate = sample_rate;
  // write out the header - we'll fill in some of the blanks later
  fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  m_file_size = sizeof(wav_header_t);
  
  buf_ready = 0;
  buf_select = 0;
  buf_count = 0;
  buffer_size = sample_rate * buffer_time;

  buffers[0] = (int16_t *)heap_caps_malloc(buffer_size * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  if (buffers[0] == NULL)
  {
      ESP_LOGE(TAG, "Failed to allocate %d bytes for wav file buffer[0]", buffer_size * sizeof(int16_t));
  }

  buffers[1] = (int16_t *)heap_caps_malloc(buffer_size * sizeof(int16_t), MALLOC_CAP_SPIRAM);

  if (buffers[1] == NULL)
  {
      ESP_LOGE(TAG, "Failed to allocate %d bytes for wav file buffer[1]", buffer_size * sizeof(int16_t));
      heap_caps_free(buffers[0]);
  }

  buffers[0][0] = {0};
  buffers[1][0] = {0};
}

WAVFileWriter::~WAVFileWriter(){
  ESP_LOGV(TAG, "Func: %s", __func__);

  if (m_fp != NULL)
  {
      fclose(m_fp);
  }

  if (buffers[0] != NULL)
  {
      heap_caps_free(buffers[0]);
  }
  
  if (buffers[1] != NULL)
  {
      heap_caps_free(buffers[1]);
  }
}

bool WAVFileWriter::buffer_is_full(){
  
  this->swap_buffers();
  buf_ready = 1;

  return true;
}

void WAVFileWriter::swap_buffers(){
  // TODO: Need thread mutex??
  // swap buffers
  if (++buf_select > 1)
    buf_select = 0;
  
  buf_count = 0;

  ESP_LOGI(TAG, "buffer_active = %d", buf_select);

}

void WAVFileWriter::write()
{
  auto buffer_inactive = buf_select ? 0 : 1;
  
  ESP_LOGV(TAG, "Writing wav file size: %d", m_file_size);
  
  if (m_fp == NULL){
    ESP_LOGE(TAG, "File pointer is NULL");
    return;
  }

  fwrite(buffers[buffer_inactive], sizeof(int16_t), buffer_size, m_fp);
  m_file_size += sizeof(int16_t) * buffer_size;

  // Don't swap buffers here, I2MEMSSampler::read() to do it
  buf_ready = 0;
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

  m_file_size = 0;
  m_fp = NULL;

  // Don't swap buffers?? Could be filling

  return true;
}

void WAVFileWriter::setSample_rate (int sample_rate) {
  m_header.sample_rate = sample_rate;
  m_header.byte_rate = m_header.sample_rate * m_header.num_channels * (m_header.bit_depth / 8);
}

void WAVFileWriter::setChannelCount(int channel_count) {
    m_header.num_channels = channel_count;
    m_header.byte_rate = m_header.sample_rate*2*channel_count;      // Number of bytes per second. sample_rate * num_channels * Bytes Per Sample
    m_header.sample_alignment = channel_count*(m_header.bit_depth / 8); // num_channels * Bytes Per Sample
}