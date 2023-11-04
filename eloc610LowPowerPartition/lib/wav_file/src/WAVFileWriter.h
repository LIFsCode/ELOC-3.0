#pragma once

#include <stdio.h>
#include <esp_heap_caps.h>
#include "WAVFile.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
class WAVFileWriter
{
private:
  u_int32_t m_file_size;              // Size of wav file in bytes
  FILE *m_fp = NULL;                  // pointer to wav file
  wav_header_t m_header;              // struct of wav header
  int m_sample_rate = 16000;          // I2S sample rate, reasonable default
  bool enable_wav_file_write = true;  // Continue to write to wav file while true

  SemaphoreHandle_t xSemaphore_m_fp_access;       // Semaphore to protect file writes & closure
  
  /* TODO: check if it is better to have this functions as member of wav_header_t *
    * however this leave wav_header_t as non POD struct.  */
  void setSample_rate (int sample_rate);
  void setChannelCount(int channel_count);

  /**
   * @brief Start a thread of the method write()
   * @note  Called by wrapper method, not directly
   *        Runs until enable_wav_file_write == false
  */
  void start_write_thread();

public:
  /**
   * Use a double buffer system
   * Active buffer -> fill with data
   * Inactive buffer -> write to file
   * These are public to alow access from I2SMEMSSampler (or make friend class?)
   * @param buf_select which buffer is active?
   * @param buf_count index of next element to write to
   * @param buffers pointer array to 2 buffers, 1 active, 1 inactive
   */
  size_t buf_select = 0;
  int buf_ready = 0;

  /**
   * @note In order to optimize write times the buffer size
   *       should be a multiple of 512 bytes (SD card block size)
   */
  size_t buffer_size = (int(16000/512)) * 512; 
  size_t buf_count;
  signed short *buffers[2];

  /**
   * @brief Construct a new WAVFileWriter object
   * @param fp file pointer to write to (already created)
   * @param sample_rate I2S sample rate
   * @param buffer_time Buffer size required (seconds) 
   * @param ch_count number of channels
   */
  WAVFileWriter(FILE *fp, int sample_rate, int buffer_time, int ch_count = 1);

  /**
   * @brief Destroy the WAVFileWriter object
   */
  ~WAVFileWriter();

  /**
   * @brief Get the current file size
   * @return u_int32_t file size in bytes
   */
  u_int32_t get_file_size_bytes() { return m_file_size; }

  /**
   * @brief Get the current file size
   * @note Uses sample rate to convert to seconds
   * @return u_int32_t file size in seconds
   */
  u_int32_t get_file_size_sec() { return m_file_size / (sizeof(int16_t) * m_sample_rate); }

  /**
   * @brief Set current file size to 0
   */
  void zero_file_size() { m_file_size = 0; }

  /**
   * @brief Setter for enable_wav_file_write
   * @note Used to halt thread that continuously saves sound to wav file
   * @param value bool value
  */
  void set_enable_wav_file_write(bool value) {enable_wav_file_write = value;}

  /**
   * @brief Check if file ready to save
   * @return true if ready to save
   */
  int check_if_ready_to_save() {return buf_ready;}

  /**
   * @brief Called when a buffer is full
   * @note This will swap the buffers and set buf_ready
   * 
   * @return true success
   * @return false 
   */
  bool buffer_is_full();

  /**
   * @brief Swap buffers
   * @deprecated Note required? Implemented directly in I2SMEMSSampler::read()
   */
  void swap_buffers();

  /**
   * @brief Write samples to file
   */
  void write();

  /**
   * @brief Create header and write to file
   * @note This will reset the buf_count to 0
   * @return true success
   */
  bool finish();

  /**
   * @brief Wrapper to start a task in a thread
   * @note Must be static for FreeRTOS
   * @param _this pointer to `this`
  */
  static void start_wav_writer_wrapper(void * _this);

  /**
   * @brief Wrapper to start thread to write out to wav file
   * @note This will start a thread that runs until enable_wav_file_write == false
  */
  int start_wav_write_task();
};
