/**
 * @brief File holding build related configurations & definitions
 * @copyright (c) Owen O'Hehir
 * @warning Do not include any other files as compiler uses
 *          relative path from other files pulling in this file,
 *          build will break as a result
*/

#pragma once

// List of supported boards
#define ELOC_3_0 1
// End of supported boards

// Default board if not defined in platformio.ini
#ifndef BOARD
#warning "No BOARD defined in platformio.ini, using default"
#define BOARD ELOC_3_0
#endif

///////////////////////////////// Board specific configurations ////////////////////////////

#if BOARD == ELOC_3_0

        #define BLUETOOTH_CLASSIC

        #define VERSION "ELOC_V1.3"

        #define STATUS_LED          GPIO_NUM_4
        #define BATTERY_LED         GPIO_NUM_4
        #define GPIO_BUTTON         GPIO_NUM_0
        #define VOLTAGE_PIN         GPIO_NUM_34
        #define BAT_ADC             ADC1_CHANNEL_6

        // I2S Config
        #define I2S_MIC_LEFT_RIGHT_CLOCK    GPIO_NUM_5
        #define I2S_MIC_SERIAL_CLOCK        GPIO_NUM_18
        #define I2S_MIC_SERIAL_DATA         GPIO_NUM_19

        // I2S Mic type
        #define I2S_TDK_INVENSENSE_ICS_43434

        // sdcard (unused, as SDIO is fixed to its Pins)
        #define PIN_NUM_MISO    GPIO_NUM_2
        #define PIN_NUM_CLK     GPIO_NUM_14
        #define PIN_NUM_MOSI    GPIO_NUM_15
        #define PIN_NUM_CS      GPIO_NUM_14

        // Lora
        #define PIN_LORA_MISO GPIO_NUM_32
        #define PIN_LORA_CLK GPIO_NUM_33
        #define PIN_LORA_MOSI GPIO_NUM_26
        #define PIN_LORA_CS GPIO_NUM_27
        #define PIN_LORA_DIO1 GPIO_NUM_21
        #define PIN_LORA_RST GPIO_NUM_25
        #define PIN_LORA_BUSY GPIO_NUM_35

        // i2c config
        // LIS3DH Accelerometer & PCA9557 expander
        #define USE_I2C
        #define I2C_SPEED_HZ 100000

        #define I2C_PORT    I2C_NUM_0
        #define I2C_SDA_PIN GPIO_NUM_23
        #define I2C_SCL_PIN GPIO_NUM_22

        /* LIS3DH Config*/
        #define LIS3DH_INT_PIN GPIO_NUM_12
        #define INTRUDER_DETECTION_THRSH 5  // 0 for disabling

        /* Buzzer Config */
        #define BUZZER_PIN GPIO_NUM_13

#endif  // BOARD

/////////////////////////////////// Time Configuration ///////////////////////////////////

#define TIMEZONE_OFFSET    7

/////////////////////////////////// Interrupt Configuration //////////////////////////////

/** Interrupt definitions, lower levels are lower priorities */
#define GPIO_INTR_PRIO ESP_INTR_FLAG_LEVEL1
#define I2S_INTR_PIRO ESP_INTR_FLAG_LEVEL2

/////////////////////////////////// I2S & Sound Configuration /////////////////////////////

#define NUMBER_OF_MIC_CHANNELS 1
#define I2S_DMA_BUFFER_COUNT 8
#define I2S_DMA_BUFFER_LEN   1024
// WARNING: This value will be overridden by '.config' on SD card or SPIFFS
#define I2S_DEFAULT_SAMPLE_RATE 16000
#define I2S_DEFAULT_CHANNEL_FORMAT_RIGHT         // or I2S_DEFAULT_CHANNEL_FORMAT_RIGHT

#ifdef I2S_TDK_INVENSENSE_ICS_43434
        // Uses TDK/ INVENSENSE ICS-43434 mic
        // https://invensense.tdk.com/download-pdf/ics-43434-datasheet
        // " The output data word length is 24 bits per channel. The default data format is I2S
        // (twos complement), MSB-first. In this format, the MSB of each word is delayed by one
        // SCK cycle from the start of each half-frame"
        #define I2S_BITS_PER_SAMPLE 24
        #define I2S_SAMPLE_RATE_MIN 4000                // Not certain from datasheet
        #define I2S_SAMPLE_RATE_MAX 51600               // Not certain from datasheet

        /**
         * @deprecated?
         * I2S_DEFAULT_BIT_SHIFT replaced by combination of I2S_BITS_PER_SAMPLE & I2S_DEFAULT_VOLUME
         */
        #define I2S_DEFAULT_BIT_SHIFT 14                // Default bit shift for this mic
        #define I2S_DEFAULT_PORT I2S_NUM_0              // Default port for this mic
        /**
         * @note Default volume shift for this mic. -ve value decrease volume, +ve increase. 0 neutral
         *       e.g. value of +1 doubles volume, value of -1 halves volume change in steps of 1
         *       Set a default as -3 which matches volume of SPH0645 in previous code
        */
        #define I2S_DEFAULT_VOLUME -4
#endif  // I2S_TDK_INVENSENSE_ICS_43434

#ifdef I2S_SPH0645
        // Uses SPH0645 mic
        // https://cdn-shop.adafruit.com/product-files/3421/i2S+Datasheet.PDF
        #define I2S_BITS_PER_SAMPLE 24
        #define I2S_SAMPLE_RATE_MIN 16000
        #define I2S_SAMPLE_RATE_MAX 64000
        #define I2S_DEFAULT_BIT_SHIFT 14
        #define I2S_DEFAULT_PORT I2S_NUM_0
        #define I2S_DEFAULT_VOLUME -3
#endif  // I2S_SPH0645

/**
 * @brief Enable/ disable automatic gain feature in @file I2SMEMSSampler.cpp
 *        This feature adjust volume for optimum performance
 * @note: Currently causes distortion on SD card recording!
 * @todo: fix
 */
// #define ENABLE_AUTOMATIC_GAIN_ADJUSTMENT

/**
 * @brief Use Teleplot extension to visualize waveform in @file I2SMEMSampler.cpp
 * @link https://marketplace.visualstudio.com/items?itemName=alexnesnes.teleplot
 *
 * @note: This is a debug feature and will cause significant serial output!
 */
//  #define VISUALIZE_WAVEFORM

/////////////////////////////////// Performance Monitor ///////////////////////////////////
// undefine to skip performance monitor
#define USE_PERF_MONITOR

/////////////////////////////////// AI Related configurations ///////////////////////////////////
/**
 * @note A value threshold of 0.8 is used to determine if target sound has been detected
 * @todo Make this configurable via Bluetooth?
 */
#define AI_RESULT_THRESHOLD 0.85

/**
 * @brief Enable continuous inference in the AI model
 *        The difference between continuous & non-continuous appears to be
 *        'Performance calibration' in the Edge Impulse project
*/
// #define AI_CONTINUOUS_INFERENCE

/**
 * @brief Enable CPU frequency increase during AI processing
 *        This is to speed up the AI processing & enable more complex models
 * @note: This will increase power consumption
 */

#define AI_INCREASE_CPU_FREQ

/////////////////////////////////// Bluetooth Related configurations ///////////////////////////////////

#define MAX_COMMANDS 32

/////////////////////////////////// Memory Related configurations ///////////////////////////////////

// Place buffers in PSRAM/ SPI RAM? Otherwise stored in RAM
// Storing in PSRAM appears to be significantly slower & power hungry

// #define I2S_BUFFER_IN_PSRAM
// #define WAV_BUFFER_IN_PSRAM
 #define EI_BUFFER_IN_PSRAM


/////////////////////////////////// Thread Related configurations ///////////////////////////////////

#define TASK_PRIO_WAV 8
#define TASK_PRIO_AI 7
#define TASK_PRIO_I2S 10
#define TASK_PRIO_CMD 1
#define TASK_PRIO_UART_TEST 2

// define specific CPU Cores for critical tasks
// setting tasks fixed to a core, makes sure the AI will have a separate core as it will be the most
// time consuming operation
// TODO: could be extended to the other tasks as well (main task is fixed via menuconfig)
#define TASK_I2S_CORE 0
#define TASK_WAV_CORE 0
#define TASK_AI_CORE 1
#define TASK_UART_TEST_CORE 0

/////////////////////////////////// Test UART configurations ///////////////////////////////////
/**
 * This UART is used to inject commands to the device
 * For test purposes only
*/

// #define ENABLE_TEST_UART
// #define ENABLE_UART_WAKE_FROM_SLEEP

/////////////////////////////////// Configurations checks ///////////////////////////////////

