#include "config.h"
extern int gBufferLen,gBufferCount;
extern uint32_t gSampleRate;
extern bool gUseAPLL;


// i2s config for reading from I2S
i2s_config_t i2s_mic_Config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = gSampleRate, //fails when hardcoded to 22050
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    #ifdef USE_SPI_VERSION
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, //bug! should be left? for 5.2.0 eave this for both spi and sdio
    #endif
    #ifdef USE_SDIO_VERSION
       .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, //bug! should be left? for 5.2.0 eave this for both spi and sdio
    #endif

    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = gBufferCount,  //so 2000 sample  buffer at 16khz sr gives us 125ms to do our writing
    .dma_buf_len = gBufferLen,  // 8 buffers gives us half  second
    .use_apll = gUseAPLL, //not getting set. gUseAPLL, //the only thing that works with LowPower/APLL is 16khz 12khz??
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0,
    //.mclk_multiple =I2S_MCLK_MULTIPLE_DEFAULT,   // I2S_MCLK_MULTIPLE_DEFAULT= 0,       /*!< Default value. mclk = sample_rate * 256 */
    //.bits_per_chan=I2S_BITS_PER_CHAN_DEFAULT
    };

// i2s microphone pins
i2s_pin_config_t i2s_mic_pins = {
  
   .mck_io_num = I2S_PIN_NO_CHANGE,  //tbg removed new api?
  
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA
};


