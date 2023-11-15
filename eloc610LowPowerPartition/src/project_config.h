/**
 * @brief File holding build related configurations & definitions
 * @warning Do not include any other files as compiler uses
 *          relative path from other files pulling in this file,
 *          build will break as a result
*/

#pragma once

#ifndef _PROJECT_CONFIG_H_
#define _PROJECT_CONFIG_H_

/**
 * @brief A suggested method of implementing a board dependent build configuration
 *        The basic idea is that a board is defined in platformio.ini file as a build flag e.g.
 *        build_flags =
                -DBOARD=WROVER_KIT

          & the resulting peripherals are defined, with any associated settings, in this file. e.g:
 * 
 *
 * Default board would be the latest, this gets build if no BOARD is defined in platform.ini
 * #ifndef BOARD
 *  #define BOARD ELOC_3_2
 * #endif
 * 
 * #ifdef ELOC_3_2
 *  define all pins here...
 * 
 *  define all peripherals here..eg
 *  #define I2S_MIC_ICS-43434
 * 
 * #endif // ELOC_3_2
 * 
 * 
 * #ifdef ELOC_3_1
 *  #define I2S_MIC_SPH0645
 *  etc..
 * #endif // ELOC_3_1
 * 
 * #ifdef WROVER_KIT
 *  etc..
 * #endif // WROVER_KIT
 * 
 * 
 * Here define all peripheral settings, e.g.
 * #ifdef I2S_MIC_ICS-43434
 *  #define I2S_BITS_PER_SAMPLE 24        
 *  #define I2S_SAMPLE_RATE_MIN 4000
 *  #define I2S_SAMPLE_RATE_MAX 51600
 *  #define I2S_DEFAULT_VOLUME 4
 * #endif // I2S_MIC_ICS-43434
 * 
 * 
 * 
 */



//#define USE_SPI_VERSION
#define USE_SDIO_VERSION

#define NUMBER_OF_CHANNELS 1 
#define TIMEZONE_OFFSET    7

#ifdef USE_SPI_VERSION
        #define VERSION "eloc610SPILowPower06Apr2023a"
     
        #define STATUS_LED  GPIO_NUM_33      
        #define BATTERY_LED  GPIO_NUM_25     
        #define GPIO_BUTTON GPIO_NUM_0
        #define OTHER_GPIO_BUTTON GPIO_NUM_21
        #define VOLTAGE_PIN GPIO_NUM_34
        
        #define PIN_NUM_MISO GPIO_NUM_19
        #define PIN_NUM_CLK GPIO_NUM_18
        #define PIN_NUM_MOSI GPIO_NUM_23
        #define PIN_NUM_CS GPIO_NUM_5
        
        #define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_12    
        #define I2S_MIC_SERIAL_DATA GPIO_NUM_27         
        #define I2S_MIC_SERIAL_CLOCK GPIO_NUM_14     
#endif


#ifdef USE_SDIO_VERSION
        #define VERSION "ELOC_V0.1"
     
        #define STATUS_LED  GPIO_NUM_4      
        #define BATTERY_LED  GPIO_NUM_4     
        #define GPIO_BUTTON GPIO_NUM_0
        #define VOLTAGE_PIN GPIO_NUM_34
        #define BAT_ADC ADC1_CHANNEL_6

        /** Interrupt definitions
         *  lower levels are lower priorities
        */
        #define GPIO_INTR_PRIO ESP_INTR_FLAG_LEVEL1
        #define I2S_INTR_PIRO ESP_INTR_FLAG_LEVEL2
        
  
        // I2S Config
        #define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_5
        #define I2S_MIC_SERIAL_CLOCK GPIO_NUM_18     
        #define I2S_MIC_SERIAL_DATA   GPIO_NUM_19        
        // TODO: check if we want these configurable
        #define I2S_DMA_BUFFER_COUNT 18
        #define I2S_DMA_BUFFER_LEN   1000
        // WARNING: This value will be overridden by '.config' on SD card or SPIFFS
        #define I2S_DEFAULT_SAMPLE_RATE 16000

        /**
         * Start ELOC 3.2 board defintions
         */

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
         * I2S_DEFAULT_BIT_SHIFT replaced combination of I2S_BITS_PER_SAMPLE & I2S_DEFAULT_VOLUME
         */
        #define I2S_DEFAULT_BIT_SHIFT 14                // Default bit shift for this mic
        #define I2S_DEFAULT_PORT I2S_NUM_0              // Default port for this mic
        #define I2S_DEFAULT_VOLUME 4                    // Default volume shift for this mic. Possible values are 2 (lowest), 4, 8, 16 (highest)
        
        /** 
         * @brief Enable/ disable automatic gain feature in @file I2SMEMSSampler.cpp
         *        This feature adjust volume for optimum performance
         */ 
        // #define ENABLE_AUTOMATIC_GAIN_ADJUSTMENT

        /**
         * @brief Use Teleplot extension to visualize waveform in @file I2SMEMSampler.cpp
         * @link https://marketplace.visualstudio.com/items?itemName=alexnesnes.teleplot
         * 
         * @note: This is a debug feature and will cause significant serial output!
         * 
         */              
        //#define VISUALIZE_WAVEFORM      

        /**
         * End ELOC 3.2 board definitions
         */

        // sdcard (unused, as SDIO is fixed to its Pins)
        #define PIN_NUM_MISO GPIO_NUM_2
        #define PIN_NUM_CLK GPIO_NUM_14
        #define PIN_NUM_MOSI GPIO_NUM_15
        #define PIN_NUM_CS GPIO_NUM_14
  
        // i2c config
        #define USE_I2C

        #define I2C_PORT I2C_NUM_0
        #define I2C_SDA_PIN GPIO_NUM_23
        #define I2C_SCL_PIN GPIO_NUM_22
        #define I2C_SPEED_HZ 100000

        /* LIS3DH Config*/
        #define LIS3DH_INT_PIN GPIO_NUM_12

        /* Buzzer Config */
        #define BUZZER_PIN GPIO_NUM_13

#endif

// undefine to skip performance monitor
#define USE_PERF_MONITOR


//////////////////////////////////////////////////// AI Related configurations ////////////////////////////////////////////////////
/**
 * @note A value threshold of 0.8 is used to determine if target sound has been detected
 * @todo Make this configurable via Bluetooth?
 */
#define AI_RESULT_THRESHOLD 0.8

/**
 * @brief Enable continuous inference in the AI model
 *        The difference between continuous & non-continuous appears to be
 *        'Performance calibration' in the Edge Impulse project
*/
// #define AI_CONTINUOUS_INFERENCE

#endif // _PROJECT_CONFIG_H_