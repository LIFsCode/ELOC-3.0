
#include "esp_log.h"
#include "WAVFileWriter.h"


static const char *TAG = "WAVFileWriter";

WAVFileWriter::WAVFileWriter(int sample_rate, int buffer_time, int ch_count /*=1*/)
{
  ESP_LOGV(TAG, "Func: %s", __func__);
  
  m_fp = nullptr;
  m_sample_rate = sample_rate;

  setChannelCount(ch_count);
  setSample_rate(sample_rate);
  m_header.sample_rate = sample_rate;
  
  buf_ready = 0;
  buf_select = 0;
  buf_count = 0;

  // Make buffer size a multiple of 512
  buffer_size = int((sample_rate * buffer_time) / 512) * 512;
  ESP_LOGI(TAG, "buffer_size = %d", buffer_size);

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

bool WAVFileWriter::set_file_handle(FILE *fp, int secondsPerFile){
  
  m_fp = fp;
  this->secondsPerFile = secondsPerFile;

  if(m_fp == nullptr){
    ESP_LOGE(TAG, "File pointer is NULL");
    return false;
  }

  // Know exactly how much space to allocate for the file
  // auto required_pdm_data_size = m_sample_rate * secondsPerFile * sizeof(int16_t);
  // m_header.data_bytes = m_sample_rate * secondsPerFile * sizeof(int16_t);
  // m_header.wav_size = m_file_size - 8;
  auto ret = fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  m_file_size = sizeof(wav_header_t);

  if (ret == 0)
    return false;
  else
    return true;
}

WAVFileWriter::~WAVFileWriter(){
  ESP_LOGV(TAG, "Func: %s", __func__);

  if (m_fp != nullptr)
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
    
  if (m_fp == nullptr){
    ESP_LOGE(TAG, "File pointer is NULL");
    return;
  }

  fwrite(buffers[buffer_inactive], sizeof(int16_t), buffer_size, m_fp);

  m_file_size += sizeof(int16_t) * buffer_size;

  // Don't swap buffers here, I2MEMSSampler::read() to do it
  buf_ready = 0;

  ESP_LOGI(TAG, "WAV file size bytes: %d, secs: %d", m_file_size, (m_file_size / (sizeof(int16_t) * m_sample_rate)));

}

void WAVFileWriter::start_write_thread()
{
    while(enable_wav_file_write){
      
      if (this->buf_ready){
        if (m_fp == nullptr){
          ESP_LOGE(TAG, "enable_wav_file_write enabled & file pointer == nullptr");
        }
        else{
          this->write();
        }

        if (m_file_size >= (m_sample_rate * secondsPerFile * sizeof(int16_t))){
          // Won't be saving to this file anymore..
          enable_wav_file_write = false;
          this->finish();
        }
      }
      else
        // TODO: Change this to wait for a msg from I2SMEMSSampler thread
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);

}

bool WAVFileWriter::finish()
{
  // Have to consider the case where file has reached its  
  // max size & other buffer is being filled
  ESP_LOGI(TAG, "Finishing wav file size: %d", m_file_size);
  // now fill in the header with the correct information and write it again
  // m_header.data_bytes = m_file_size - sizeof(wav_header_t);
  // m_header.wav_size = m_file_size - 8;
  // fseek(m_fp, 0, SEEK_SET);
  // fwrite(&m_header, sizeof(wav_header_t), 1, m_fp);
  fclose(m_fp);

  m_file_size = 0;
  m_fp = nullptr;

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

void WAVFileWriter::start_wav_writer_wrapper(void * _this){

  ((WAVFileWriter*)_this)->start_write_thread();

}

int WAVFileWriter::start_wav_write_task(){

  int ret = xTaskCreate(this->start_wav_writer_wrapper, "Wav file writer", 1024 * 2, this, 8, NULL);

  return ret;
}

