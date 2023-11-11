#ifndef __WAVFILEWRITER_H__
#define __WAVFILEWRITER_H__

#pragma once

#include <stdio.h>
#include <esp_heap_caps.h>
#include "WAVFile.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
class WAVFileWriter
{
  public:
  enum class Mode { disabled = 0, single = 1, continuous = 2 }; 

private:
  u_int32_t m_file_size;              // Size of wav file in bytes
  FILE *m_fp = nullptr;               // pointer to wav file
  wav_header_t m_header;              // struct of wav header
  int m_sample_rate = 16000;          // I2S sample rate, reasonable default
  bool enable_wav_file_write = true;  // Continue to write to wav file while true
  int secondsPerFile = 60;            // Seconds per file to write
  Mode mode = Mode::disabled;         // Mode of operation

  /**
   * @deprecated ??
   */
  void setSample_rate (int sample_rate);
   /**
   * @deprecated ??
   */
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
   * These are public to alow access from I2SMEMSSampler 
   * TODO: Consider changing to private & make I2MEMSSampler a friend class?
   * @param buf_select which buffer is active?
   * @param buf_count index of next element to write to
   * @param buf_ready is the inactive buffer ready to write out to SD card?
   */
  size_t buf_select = 0;
  size_t buf_count;
  int buf_ready = 0;

  /**
   * @param buffer_size size of each buffer. Determined by sample rate & buffer_time required
   * @param buffers pointer array to 2 buffers, 1 active, 1 inactive
   * @note In order to optimize write times the buffer size
   *       should be a multiple of 512 bytes (SD card block size)
   */
  size_t buffer_size = (int(16000/512)) * 512; 
  signed short *buffers[2];

  /**
   * @brief Construct a new WAVFileWriter object
   * @param sample_rate I2S sample rate
   * @param buffer_time Buffer size required (seconds) 
   * @param ch_count number of channels
   */
  WAVFileWriter(int sample_rate, int buffer_time, int ch_count = 1);

  /**
   * @brief Register a file to write to & write header
   * @param fp file pointer to write to (already created)
   * @param secondsPerFile seconds per file to write
   * @return true success
   */
  bool set_file_handle(FILE *fp, int secondsPerFile);

  /**
   * @brief Check if file handle is set
   * @note m_fp is a FILE struct
   * @return true if file handle is set
   */
  bool is_file_handle_set() { return m_fp != nullptr; }

/**
 * @brief Get the mode object
 * @return enum Mode 
 */
  enum Mode get_mode() { return mode; }

  /**
   * @brief Set the mode object
   * @param value enum Mode
   */
  void set_mode(enum Mode value) { mode = value; }

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
   * @deprecated Not required?
   * @return true success
   * @return false 
   */
  bool buffer_is_full();

  /**
   * @brief Swap buffers
   * @deprecated Not required? Implemented directly in I2SMEMSSampler::read()
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

#endif // __WAVFILEWRITER_H__