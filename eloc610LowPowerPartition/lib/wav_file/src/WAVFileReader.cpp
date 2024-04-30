#include "esp_log.h"
#include "WAVFileReader.h"

static const char *TAG = "WAV";

WAVFileReader::WAVFileReader(FILE *fp)
{
    m_fp = fp;
    // read the WAV header
    fread(reinterpret_cast<void *>(&m_wav_header), sizeof(wav_header_t), 1, m_fp);
    // sanity check the bit depth
    if (m_wav_header.bit_depth != 16) {
        ESP_LOGE(TAG, "ERROR: bit depth %d is not supported\n", m_wav_header.bit_depth);
    }
    if (m_wav_header.num_channels != 1) {
        ESP_LOGE(TAG, "ERROR: channels %d is not supported\n", m_wav_header.num_channels);
    }
    // ESP_LOGI(TAG, "fmt_chunk_size=%d, audio_format=%d, num_channels=%d, sample_rate=%d, sample_alignment=%d, bit_depth=%d, data_bytes=%d",
    //    m_wav_header.fmt_chunk_size, m_wav_header.audio_format, m_wav_header.num_channels, m_wav_header.sample_rate, m_wav_header.sample_alignment, m_wav_header.bit_depth, m_wav_header.data_bytes);

    // ESP_LOGI(TAG, "ChunkSize = %d", m_wav_header.wav_size);

    // set the number of samples
    // ESP_LOGI(TAG, "Number of samples = %d", set_number_samples());

    ESP_LOGI(TAG, "num_channels=%d, sample_rate=%d, bit_depth=%d, number of samples=%d",
        m_wav_header.num_channels, m_wav_header.sample_rate, m_wav_header.bit_depth, set_number_samples());
}

uint32_t WAVFileReader::set_number_samples() {
    // Search audio file for "data" which occurs before the number of samples
    uint32_t u32 {0};

    // Should be at position 36
    if (fseek(m_fp, 36, SEEK_SET)) {
        ESP_LOGW(TAG, "Failed to seek to data");
        return 0;
    }

    do {
        // id == "data"
        if (fread(reinterpret_cast<void *>(&u32), sizeof(u32), 1, m_fp) == 0) {
            ESP_LOGW(TAG, "Failed to read in samples");
            return 0;
        }
        if (u32 == 0x61746164) {
            // reached "data"
            break;
        } else {
            // move forward 1 byte & try again
            // note: fread auto increments the file by number of bytes read
            // ESP_LOGI(TAG, "moving back 1 byte");
            fseek(m_fp, -3, SEEK_CUR);
        }
    } while (1);

    // read the number of samples
    if (fread(reinterpret_cast<void *>(&u32), sizeof(u32), 1, m_fp) == 0) {
        ESP_LOGW(TAG, "Failed to read in samples");
        return 0;
    } else {
        ESP_LOGV(TAG, "Subchunk2Size: %d", u32);
    }

    number_samples = u32 / (m_wav_header.num_channels * (m_wav_header.bit_depth / 8));

    return number_samples;
}

int WAVFileReader::read(int16_t *samples, int count)
{
    size_t read = fread(samples, sizeof(int16_t), count, m_fp);
    return read;
}
