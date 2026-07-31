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

        #define VERSION "ELOC-P_V1.58"

        #define STATUS_LED          GPIO_NUM_4
        #define BATTERY_LED         GPIO_NUM_4
        #define GPIO_BUTTON         GPIO_NUM_0
        #define VOLTAGE_PIN         GPIO_NUM_34
        #define BAT_ADC             ADC1_CHANNEL_6

        // I2S Config
        #define I2S_MIC_LEFT_RIGHT_CLOCK    GPIO_NUM_5
        #define I2S_MIC_SERIAL_CLOCK        GPIO_NUM_18
        #define I2S_MIC_SERIAL_DATA         GPIO_NUM_19

        // I2S Mic type - Select one of the following:
        #define I2S_TDK_INVENSENSE_ICS_43434
        //#define I2S_PUI_DMM_4026_B_I2S_R

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

        /* GPS Config (ATGM336H GNSS module)
         * VCC is gated by a P-channel high-side MOSFET (AO3401A) driven from IO expander IO5.
         * The gate is pulled to +3V3 by R12, so it is ACTIVE-LOW: IO5 LOW = GPS ON, IO5 HIGH = GPS OFF
         * (use ELOC_IOEXP::setGpsPower()). NMEA is read over UART; GPIO4 was the vestigial STATUS/BATTERY
         * LED pin (the real LEDs are on the PCA9557 IO expander), so it is reused as TX. */
        #define USE_GPS
        #define GPS_UART_PORT       UART_NUM_1
        #define GPS_UART_BAUD       9600                // ATGM336H default
        #define PIN_GPS_RX          GPIO_NUM_36         // ESP32 RX  <- GPS TX (NMEA), input-only pin
        #define PIN_GPS_TX          GPIO_NUM_4          // ESP32 TX  -> GPS RX
        #define GPS_LOG_INTERVAL_S  30                  // status print cadence to serial
        // On a duty-cycle timer wake the boot path blocks up to this long for the clock to be
        // corrected from GPS UTC. VCC is gated off in deep sleep, but the module's VBAT is tied to
        // +3V3, so it keeps its RTC + ephemeris/almanac alive across the wake — every wake is a
        // warm/hot start and a fix normally lands within a few seconds. This wait happens before the
        // awake timer is reset, so it does not shorten the inference window, but it does keep the
        // device awake (and burning power) longer each cycle. The value below is a generous ceiling
        // for a poor-sky-view fix, not the expected wait. Set to 0 to start GPS on wake without
        // blocking (time is then corrected opportunistically if a fix lands within the awake window).
        // NOTE: this trim ceiling assumes there is something to warm-start FROM. Until the deployment
        // has had its first fix, GPS_FIRST_FIX_TIMEOUT_S is used instead (see below).
        #define GPS_TIME_SYNC_TIMEOUT_S  30
        // How often the GPS time is re-acquired. The ESP32 RTC keeps accurate time through deep sleep
        // and while awake (and VBAT keeps the GPS module's clock + almanac alive continuously), so the
        // GPS only needs to correct drift roughly once per this interval. It governs BOTH power paths:
        //   - Duty-cycle timer wake: a wake skips powering the GPS entirely if the last successful sync
        //     was less than this long ago (turns the cold-start wait into a ~once-per-interval cost).
        //   - Continuous / 24/7 recording (no deep sleep): the GPS is run in bursts (see
        //     manageGpsWhileAwake in main.cpp) — powered up only long enough to grab a fresh fix, then
        //     gated off until this interval elapses. The ATGM336H draws nearly as much as the ESP32 at
        //     16 kHz, so leaving it on would roughly double idle current.
        // Set to 0 to disable both optimisations (re-sync every wake / keep GPS powered continuously).
        #define GPS_RESYNC_INTERVAL_S    3600    // re-acquire every 1 h (warm-start cadence in the field)
        // Acquisition ceiling while the deployment has never had a GPS fix (rtc_duty_cycle
        // .lastGpsSyncS == 0). With no previous fix there is no ephemeris and no almanac in the
        // module's VBAT-backed RAM, so every attempt is a true COLD start — the receiver has to
        // decode the 50 bps nav message, which needs tens of seconds of continuous tracking and far
        // longer under canopy. The short GPS_TIME_SYNC_TIMEOUT_S trim ceiling can never complete
        // that, so a unit commissioned into duty cycle before its first fix would keep restarting a
        // cold start it never finishes. Applies to BOTH the blocking wake wait (app_main) and the
        // awake-window burst (manageGpsWhileAwake); both revert to GPS_TIME_SYNC_TIMEOUT_S the moment
        // the first fix lands.
        // NOT keyed on "the clock is unset": the app's setTime at commissioning advances the clock,
        // which used to mask exactly the case this exists for.
        #define GPS_FIRST_FIX_TIMEOUT_S  180     // up to 3 min for a cold first fix
        // Cap on how many duty-cycle wakes may pay the patient blocking wait above. Without a bound,
        // a unit with no sky view at all (indoors on the bench, or a bad install) would hold itself
        // awake GPS_FIRST_FIX_TIMEOUT_S longer on EVERY cycle forever, wrecking the duty cycle it was
        // configured for. ~10 wakes ≈ 30 min of cumulative cold-start search; if that fails, more of
        // the same will not help, so fall back to the cheap trim cadence and keep retrying there.
        // Counted with rtc_duty_cycle.bootCount (timer wakes only). 0 disables the patient wake wait.
        #define GPS_FIRST_FIX_PATIENT_WAKES  10

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

#ifdef I2S_PUI_DMM_4026_B_I2S_R
        // Uses PUI Audio DMM-4026-B-I2S-R mic
        // https://www.puiaudio.com/media/SpecSheet/DMM-4026-B-I2S-R.pdf
        // "I2S 24-bit data size with 18-bit precision, 32-bit word size"
        // "Each microphone outputs 24-bit data with 18-bit precision. Six bits are null (0) value."
        #define I2S_BITS_PER_SAMPLE 24
        #define I2S_SAMPLE_RATE_MIN 8000                // Based on BCLK 2.048-4.096 MHz range
        #define I2S_SAMPLE_RATE_MAX 48000               // Based on BCLK 3.072-12.288 MHz range
        
        /**
         * @deprecated?
         * I2S_DEFAULT_BIT_SHIFT replaced by combination of I2S_BITS_PER_SAMPLE & I2S_DEFAULT_VOLUME
         */
        #define I2S_DEFAULT_BIT_SHIFT 14                // Default bit shift for this mic
        #define I2S_DEFAULT_PORT I2S_NUM_0              // Default port for this mic
        
        /**
         * @note Default volume shift for this mic. DMM-4026-B-I2S-R has 18-bit precision in 24-bit word
         *       The 6 null bits need to be accounted for in processing
         *       Sensitivity: -26±1 dB (same as ICS-43434) but different internal format
         *       Set default volume to account for 18-bit vs 24-bit precision difference
         *       Adjusted to -3 to match ICS-43434 behavior initially
         */
        #define I2S_DEFAULT_VOLUME -3
        
        /**
         * @note DMM-4026-B-I2S-R specific parameters
         *       - Requires 32-bit word size for mono operation
         *       - 24-bit data with only 18-bit precision (6 bits are null)
         *       - BCLK range: 3.072-12.288 MHz
         *       - Input clock: 2.048-4.096 MHz
         */
        #define I2S_DMM_4026_PRECISION_BITS 18          // Actual precision bits
        #define I2S_DMM_4026_NULL_BITS 6                // Number of null bits in 24-bit word
        
        /**
         * @note DC offset removal filter configuration
         *       The DMM-4026-B-I2S-R may not have internal AC coupling, causing DC offset
         *       This filter removes the DC component using exponential moving average
         */
        #define I2S_DMM_4026_ENABLE_DC_FILTER           // Enable DC offset removal
        #define I2S_DMM_4026_DC_FILTER_ALPHA 0.02f      // Filter coefficient (more aggressive for better centering)
#endif  // I2S_PUI_DMM_4026_B_I2S_R

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

// Periodically log internal + PSRAM heap stats (min free, largest block, fragmentation)
// in the main loop. Used to verify heap headroom with BT + LoRa + AI running.
#define ENABLE_HEAP_MONITOR


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
