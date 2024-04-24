#pragma once

#include "WAVFile.h"
#include <stdint.h>
#include <stdio.h>

class WAVFileReader
{
 private:
    wav_header_t m_wav_header;
    uint32_t number_samples {0};
    FILE *m_fp = NULL;

 public:
    explicit WAVFileReader(FILE *fp);
    int bit_depth() { return m_wav_header.bit_depth; }
    int num_channels() { return m_wav_header.num_channels; }
    int sample_rate() { return m_wav_header.sample_rate; }
    uint32_t get_number_samples() { return number_samples; }

    /**
     * @brief Stores the number of samples in the WAV file
     * @note this function has not been rigorously tested
     * @return uint32_t number of samples
     */
    uint32_t set_number_samples();

    int read(int16_t *samples, int count);
};
