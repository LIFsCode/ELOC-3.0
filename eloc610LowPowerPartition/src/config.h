#pragma once

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>
#include <driver/adc.h>
#include "lis3dh_types.h"

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
         * The following are for the ELOC 3.2 board
         * 
         */

        // Uses TDK/ INVENSENSE ICS-43434 mic
        // https://invensense.tdk.com/download-pdf/ics-43434-datasheet
        // " The output data word length is 24 bits per channel. The default data format is I2S
        // (twos complement), MSB-first. In this format, the MSB of each word is delayed by one
        // SCK cycle from the start of each half-frame"
        #define I2S_BITS_PER_SAMPLE 24        
        #define I2S_SAMPLE_RATE_MIN 4000                // Not certain from datasheet 
        #define I2S_SAMPLE_RATE_MAX 51600               // Not certain from datasheet

        #define I2S_DEFAULT_BIT_SHIFT 11                // Default bit shift for this mic
        #define I2S_DEFAULT_PORT I2S_NUM_0              // Default port for this mic

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
        extern lis3dh_config_t lis3dh_config;
        extern lis3dh_int_click_config_t lis3dh_click_config;

        /* Buzzer Config */
        #define BUZZER_PIN GPIO_NUM_13


#endif

// undefine to skip performance monitor
#define USE_PERF_MONITOR

// i2s config for reading from of I2S
extern i2s_config_t i2s_mic_Config;
// i2s microphone pins
extern i2s_pin_config_t i2s_mic_pins;

#endif // _CONFIG_H_