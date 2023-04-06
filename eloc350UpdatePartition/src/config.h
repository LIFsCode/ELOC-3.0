#include <freertos/FreeRTOS.h>
#include <driver/i2s.h>



//#define USE_SPI_VERSION
#define USE_SDIO_VERSION

#define NUMBER_OF_CHANNELS 1 
#define TIMEZONE_OFFSET    7

#ifdef USE_SPI_VERSION
        #define VERSION "elocSPILowPower18Mar2023a"
     
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
        #define VERSION "elocSDIOLowPower18Mar2023A"
     
        #define STATUS_LED  GPIO_NUM_4      
        #define BATTERY_LED  GPIO_NUM_4     
        #define GPIO_BUTTON GPIO_NUM_0
        #define OTHER_GPIO_BUTTON GPIO_NUM_0
        #define VOLTAGE_PIN GPIO_NUM_34
        
  
        
        #define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_5
        #define I2S_MIC_SERIAL_CLOCK GPIO_NUM_18     
        #define I2S_MIC_SERIAL_DATA   GPIO_NUM_19        
  


#endif



// i2s config for reading from of I2S
extern i2s_config_t i2s_mic_Config;
// i2s microphone pins
extern i2s_pin_config_t i2s_mic_pins;