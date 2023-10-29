#include "esp_log.h"
#include "WAVFileWriter.h"

static const char *TAG = "WAVFileWriter";

WAVFileWriter::WAVFileWriter(FILE *fp, int sample_rate)
{
  m_fp = fp;
  m_header.setSample_rate(sample_rate);
  // write out the header - we'll fill in some of the blanks later
  fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  m_file_size = sizeof(wav_header_t);

  buf_select = 0;
  buf_count = 0;
  buffers[0][0] = {0};
  buffers[1][0] = {0};
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
  
  ESP_LOGI(TAG, "Writing wav file size: %d", m_file_size);
  
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
    
  this->swap_buffers();

  return true;
}