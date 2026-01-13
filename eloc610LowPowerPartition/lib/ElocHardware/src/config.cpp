#include "config.h"

// i2s config for reading from I2S
i2s_config_t i2s_mic_Config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_DEFAULT_SAMPLE_RATE,  // fails when hardcoded to 22050
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,

    #ifdef I2S_DEFAULT_CHANNEL_FORMAT_LEFT
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    #else
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    #endif

    #ifdef I2S_PUI_DMM_4026_B_I2S_R
        // DMM-4026-B-I2S-R uses MSB-aligned (Left-justified) format
        // According to the timing diagram, data appears immediately after WCLK setup
        // (same BCLK edge), NOT one BCLK cycle later as in Philips I2S standard.
        // TSWCLK (setup time) -> BCLK falls -> TDV (18ns) -> data valid
        // This timing corresponds to I2S_COMM_FORMAT_STAND_MSB, not I2S_COMM_FORMAT_STAND_I2S
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    #else
        // Standard I2S Philips format for ICS-43434 and other microphones
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    #endif

    .intr_alloc_flags = I2S_INTR_PIRO,
    .dma_buf_count = I2S_DMA_BUFFER_COUNT,  //  so 2000 sample  buffer at 16khz sr gives us 125ms to do our writing
    .dma_buf_len = I2S_DMA_BUFFER_LEN,      //  8 buffers gives us half  second
    
    #ifdef I2S_PUI_DMM_4026_B_I2S_R
        // DMM-4026-B-I2S-R has specific clock requirements
        // BCLK range: 3.072-12.288 MHz, Input clock: 2.048-4.096 MHz
        .use_apll = true,                   // Use APLL for better clock precision
    #else
        // Standard settings for ICS-43434 and other mics
        .use_apll = true,                   //  the only thing that works with LowPower/APLL is 16khz 12khz??
    #endif
    
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0,
    
    #ifdef I2S_PUI_DMM_4026_B_I2S_R
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,  // 256x sample rate for proper BCLK generation
    #else
        .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,   // I2S_MCLK_MULTIPLE_DEFAULT= 0,       /*!< Default value. mclk = sample_rate * 256 */
    #endif
    
    .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT
    };

// i2s microphone pins
i2s_pin_config_t i2s_mic_pins = {

   .mck_io_num = I2S_PIN_NO_CHANGE,  //  tbg removed new api?
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA
};

/* LIS3DH Config*/
lis3dh_config_t lis3dh_config = {
    .scale = lis3dh_scale_2_g,     //use finest scale (max. ±2g full scale)
    .data_rate = lis3dh_odr_400,   // sampling rate in Hz (affects power consumption, see datasheet 3.2.1)
    .resolution = lis3dh_high_res, // resolution (8/10/12 bit)
    .xEn = true,                   // enable x axis
    .yEn = true,                   // enable y axis
    .zEn = true,                   // enable z axis
};
lis3dh_int_click_config_t lis3dh_click_config = {
    /* NOTE: if double interrupt is used single clicks will be ignored (must be detected manually)*/
    .x_single = false,   // x-axis single tap interrupt enabled
    .x_double = false,   // x-axis double tap interrupt enabled
    .y_single = false,   // y-axis single tap interrupt enabled
    .y_double = false,   // y-axis double tap interrupt enabled
    .z_single = false,   // z-axis single tap interrupt enabled
    .z_double = true,    // z-axis double tap interrupt enabled
    .threshold = 30,     /* threshold which is used by the system to start the click-detection procedure */
    .latch = false,      /* true:  the interrupt is kept high until CLICK_SRC (39h) is read
                          * false: the interrupt is kept high for the duration of the latency window.  */
    .time_limit   = 10,  /* define the maximum time interval that can elapse between the start of
                          * the click-detection procedure (the acceleration on the selected channel exceeds the
                          * programmed threshold) and when the acceleration falls back below the threshold. */
    .time_latency = 20,  /* define the time interval that starts after the first click detection where
                          * the click-detection procedure is disabled, in cases where the device is configured for
                          * double-click detection */
    .time_window  = 255, /* define the maximum interval of time that can elapse after the end of the
                          * latency interval in which the click-detection procedure can start, in cases where the device
                          * is configured for double-click detection. */
};
