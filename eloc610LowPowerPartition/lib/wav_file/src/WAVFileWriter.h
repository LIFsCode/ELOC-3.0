#pragma once

#include <stdio.h>
#include "WAVFile.h"

class WAVFileWriter
{
private:
  int m_file_size;

  FILE *m_fp;
  wav_header_t m_header;
  /* TODO: check if it is better to have this functions as member of wav_header_t *
    * however this leave wav_header_t as non POD struct.  */
  void setSample_rate (int sample_rate);
  void setChannelCount(int channel_count);

public:
  WAVFileWriter(FILE *fp, int sample_rate, int ch_count =1);
  void start();
  void write(int16_t *samples, int count);
  void finish();
};
