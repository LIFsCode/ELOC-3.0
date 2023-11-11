

#include "esp_err.h"
#include "esp_log.h"
#include <rom/ets_sys.h>
#include "esp_pm.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "I2SSampler.h"
#include "I2SMEMSSampler.h"
// #include  "periph_ctrl.h"
// #include "I2SOutput.h"
#include "SDCardSDIO.h"

#include "SDCard.h"
#include "SPIFFS.h"
#include <FFat.h>
#include <ff.h>
#include "WAVFileWriter.h"
// #include "WAVFileReader.h"
#include "config.h"
#include <string.h>

#include "esp_partition.h"
#include "esp_ota_ops.h"

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "esp_sleep.h"
#include "rtc_wdt.h"
#include <driver/rtc_io.h>
// #include "soc/efuse_reg.h"

/** Arduino libraries*/
#include "ESP32Time.h"
#include "BluetoothSerial.h"
/** Arduino libraries END*/

#include "version.h"

#include "utils/ffsutils.h"
#include "lis3dh.h"
#include "Battery.hpp"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "BluetoothServer.hpp"
#include "FirmwareUpdate.hpp"
#include "PerfMonitor.hpp"

// TODO: Remove this..
// #define EDGE_IMPULSE_ENABLED

#ifdef EDGE_IMPULSE_ENABLED
// If your target is limited in memory remove this macro to save 10K RAM
// But if you do results in errors: '.... insn does not satisfy its constraints'
#define EIDSP_QUANTIZE_FILTERBANK 0

#include "trumpet_trimmed_inferencing.h"
#include "test_samples.h"
#include "ei_inference.h"

#endif

static const char *TAG = "main";

bool gMountedSDCard = false;

/**  
 *  @deprecated ??
*/ 
bool gRecording = true;

/**
 * @brief Should inference be run on sound samples? 
 * @todo Set from Bluetooth / config file
 */
bool ai_run_enable = true;      

// int32_t *graw_samples;

// int gRawSampleBlockSize = 1000;
// int gSampleBlockSize = 16000;            // must be factor of 1000   in samples
// int gBufferLen = I2S_DMA_BUFFER_LEN;     // in samples
// int gBufferCount = I2S_DMA_BUFFER_COUNT; // so 6*4000 = 24k buf len
// always keep these in same order

uint64_t gStartupTime; // gets read in at startup to set system time.

char gSessionFolder[65];

// timing
int64_t gTotalUPTimeSinceReboot = esp_timer_get_time(); // esp_timer_get_time returns 64-bit time since startup, in microseconds.
int64_t gTotalRecordTimeSinceReboot = 0;

/**  
 *  @deprecated ??
*/ 
int64_t gSessionRecordTime = 0;

int gMinutesWaitUntilDeepSleep = 60; // change to 1 or 2 for testing

// session stuff
String gSessionIdentifier = "";

// String gBluetoothMAC="";
String gFirmwareVersion = VERSION;

ESP32Time timeObject;
// WebServer server(80);
// bool updateFinished=false;
bool gWillUpdate = false;
float gFreeSpaceGB = 0.0;
uint32_t gFreeSpaceKB = 0;

// String gTimeDifferenceCode; //see getTimeDifferenceCode() below

#ifdef USE_SPI_VERSION
    SDCard *sd_card = nullptr;
#endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO *sd_card = nullptr;
#endif

I2SMEMSSampler *input = nullptr;
WAVFileWriter *wav_writer = nullptr;
QueueHandle_t rec_req_evt_queue = nullptr;

void writeSettings(String settings);
void doDeepSleep();
void setTime(long epoch, int ms);

// idf-wav-sdcard/lib/sd_card/src/SDCard.cpp   m_host.max_freq_khz = 18000;
// https://github.com/atomic14/esp32_sdcard_audio/commit/000691f9fca074c69e5bb4fdf39635ccc5f993d4#diff-6410806aa37281ef7c073550a4065903dafce607a78dc0d4cbc72cf50bac3439

extern "C"
{
    void app_main(void);
}

void resetPeripherals()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    if(0){
        periph_module_reset(PERIPH_I2S0_MODULE);
        periph_module_reset(PERIPH_BT_MODULE);
        periph_module_reset(PERIPH_WIFI_BT_COMMON_MODULE);
        periph_module_reset(PERIPH_BT_MODULE);
        periph_module_reset(PERIPH_BT_BASEBAND_MODULE);
        periph_module_reset(PERIPH_BT_LC_MODULE);
        //periph_module_reset(PERIPH_UART0_MODULE); //this used by debugger?
        periph_module_reset(PERIPH_UART1_MODULE);   //this was it! bug stops bluetooth
                                                    //from re-broadcasting in other partition on  https://github.com/espressif/esp-idf/issues/9971
        periph_module_reset(PERIPH_UART2_MODULE);   //just in case
    }
}

void printRevision()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    if(0){
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        printf("\nThis is ESP32 chip with %d CPU cores, WiFi%s%s, ",
                chip_info.cores,
                (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
                (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
        printf("silicon revision %d, ", chip_info.revision);
        printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    }
}

void delay(int ms)
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    vTaskDelay(pdMS_TO_TICKS(ms));
}

void testInput()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    for (uint32_t i = 1000; i < 34000; i = i + 2000)
    {
        i2s_mic_Config.sample_rate = i;
        i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL;

        input = new I2SMEMSSampler(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);
        input->start();
        delay(100);
        ESP_LOGI(TAG, "Clockrate: %f", i2s_get_clk(I2S_NUM_0));
        input->stop();
        delay(100);
    }

    delete input;
    input = nullptr;
    delay(100);
}

void resetESP32()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    ESP_LOGI(TAG, "Resetting..");
    resetPeripherals();
    // esp_task_wdt_init(30, false); //bump it up to 30 econds doesn't work.
    rtc_wdt_protect_off(); // Disable RTC WDT write protection

    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
    rtc_wdt_set_time(RTC_WDT_STAGE0, 500);
    rtc_wdt_enable();     // Start the RTC WDT timer
    rtc_wdt_protect_on(); // Enable RTC WDT write protection

    // ESP_LOGI(TAG, "starting deep sleep");
    // delay(50);
    // esp_deep_sleep_start();
    // ESP_LOGI(TAG, "deep sleep should never make it here");

    delay(1000); // should never get here
}

void printMemory()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    ESP_LOGI(TAG, "\n\n\n\n");
    ESP_LOGI(TAG, "Total Free mem default %d", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Largest Block default %d", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    ESP_LOGI(TAG, "Total Free mem DMA %d", heap_caps_get_free_size(MALLOC_CAP_DMA));
    ESP_LOGI(TAG, "Largest Block DMA %d", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

    ESP_LOGI(TAG, "Total Free mem IRAM %d", heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT));
    ESP_LOGI(TAG, "Largest Block IRAM %d", heap_caps_get_largest_free_block(MALLOC_CAP_IRAM_8BIT));

    ESP_LOGI(TAG, "Total Free mem EXEC %d", heap_caps_get_free_size(MALLOC_CAP_EXEC));
    ESP_LOGI(TAG, "Largest Block EXEC %d", heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));

    // heap_caps_dump_all();
    ESP_LOGI(TAG, "\n\n\n\n");
}

int64_t getSystemTimeMS()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    time_us = time_us / 1000;
    return (time_us);
}

static void IRAM_ATTR buttonISR(void *args)
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    // return;
    // ESP_LOGI(TAG, "button pressed");
    // delay(5000);
    rec_req_t rec_req = gRecording ? REC_REQ_STOP : REC_REQ_START;
    // ets_printf("button pressed");
    xQueueSendFromISR(rec_req_evt_queue, &rec_req, NULL);
    // detachInterrupt(GPIO_BUTTON);
}

static void LEDflashError()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    ESP_LOGI(TAG, "-----fast flash------------");
    for (auto i = 0; i < 10; i++)
    {
        gpio_set_level(STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(40));
        gpio_set_level(STATUS_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

/**
 * Iterate through partitions & output to console
*/
void printPartitionInfo()
{
    ESP_LOGV(TAG, "Func: %s", __func__);
    
    // will always set boot partition back to partition0 except if it

    esp_partition_iterator_t it;

    ESP_LOGI(TAG, "Iterating through app partitions...");
    it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);

    // Loop through all matching partitions, in this case, all with the type 'data' until partition with desired
    // label is found. Verify if its the same instance as the one found before.
    for (; it != NULL; it = esp_partition_next(it))
    {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "\tfound partition '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, part->label, part->address, part->size);
    }

    // Release the partition iterator to release memory allocated for it
    esp_partition_iterator_release(it);

    ESP_LOGI(TAG, "Currently running partitions: ");
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "\t '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, running->label, running->address, running->size);
    
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK)
    {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }
}


long getTimeFromTimeObjectMS()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    return (timeObject.getEpoch() * 1000L + timeObject.getMillis());
}

/**
 * Purpose ??
*/
void time()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = ((int64_t)tv_now.tv_sec * 1000000L) + (int64_t)tv_now.tv_usec;
    time_us = time_us / 1000;
}

/**
 * Purpose ??
*/
String uint64ToString(uint64_t input)
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    String result = "";
    uint8_t base = 10;

    do
    {
        char c = input % base;
        input /= base;

        if (c < 10)
            c += '0';
        else
            c += 'A' - 10;
        result = c + result;
    } while (input);
    
    return result;
}

/**
 * Set device time?
*/
void setTime(long epoch, int ms)
{
    ESP_LOGV(TAG, "Func: %s", __func__);
    
    /*

    setTime(atol(seconds.c_str())+(TIMEZONE_OFFSET*60L*60L),  (atol(milliseconds.c_str()))*1000    );
    //timeObject.setTime(atol(seconds.c_str()),  (atol(milliseconds.c_str()))*1000    );
    // timestamps coming in from android are always GMT (minus 7 hrs)
    // if I not add timezone then timeobject is off
    // so timeobject does not seem to be adding timezone to system time.
    // timestamps are in gmt+0, so timestamp convrters

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t time_us = (     (int64_t)tv_now.tv_sec      * 1000000L) + (int64_t)tv_now.tv_usec;
    time_us=time_us/1000;
    */

    struct timeval tv;
    tv.tv_sec = epoch; // epoch time (seconds)
    tv.tv_usec = ms;   // microseconds
    settimeofday(&tv, NULL);
}

/**
 * @brief Set initial time
 * @note If time is not set the getLocalTime() will stuck for 5 ms due to invalid timestamp
*/
void initTime()
{
    struct tm tm;
    strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
    time_t timeSinceEpoch = mktime(&tm);
    timeObject.setTime(timeSinceEpoch);
    ESP_LOGI(TAG, "Setting initial time to build date: %s", BUILDDATE);
}

/**
 * @brief Create folder on SD card
*/
bool createSessionFolder()
{
    String fname;
    // TODO: check if another session identifier based on ISO time for mat would be more helpful
    gSessionIdentifier = getDeviceInfo().nodeName + uint64ToString(getSystemTimeMS());
    fname = "/sdcard/eloc/" + gSessionIdentifier;
    ESP_LOGI(TAG, "Creating session folder %s", fname.c_str());
    mkdir(fname.c_str(), 0777);

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Starting session with this config:\n: %s", cfg.c_str());
    fname += "/" + gSessionIdentifier + ".config";
    FILE *f = fopen(fname.c_str(), "w+");
    if (f == nullptr)
    {
        ESP_LOGE(TAG, "Failed to open config file for storage %s!", fname.c_str());
        return false;
    }
    fprintf(f, "%s", cfg.c_str());
    fclose(f);
    return true;
}

/**
 * @brief Create empty wav file on SD card (for use by wav writer)
 * @todo Move to WAVFileWriter class
*/
bool createFilename(char *fname, size_t size)
{
    char timeStr[64] = {};
    tm timeinfo = timeObject.getTimeStruct();
    strftime(timeStr, sizeof(timeStr), "%F_%H_%M_%S", &timeinfo);
    fname[0] = '\0';
    int n = snprintf(fname, size, "/sdcard/eloc/%s/%s_%s.wav", gSessionIdentifier.c_str(), gSessionIdentifier.c_str(), timeStr);
    if ((n < 0) || (n > size))
    {
        ESP_LOGE(TAG, "filename buffer too small");
        return false;
    }
    ESP_LOGI(TAG, "filename %s", fname);
    return true;
}

/**
 * @brief Get wall clock time & date
 * @return String
*/
String getProperDateTime()
{

    String year = String(timeObject.getYear());
    String month = String(timeObject.getMonth());
    String day = String(timeObject.getDay());
    String hour = String(timeObject.getHour(true));
    String minute = String(timeObject.getMinute());
    String second = String(timeObject.getSecond());
    // String millis = String(timeObject.getMillis());
    if (month.length() == 1)
        month = "0" + month;
    if (day.length() == 1)
        day = "0" + day;
    if (hour.length() == 1)
        hour = "0" + hour;
    if (minute.length() == 1)
        minute = "0" + minute;
    if (second.length() == 1)
        second = "0" + second;

    return (year + "-" + month + "-" + day + " " + hour + ":" + minute + ":" + second);
}

void doDeepSleep()
{
    esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0); // try commenting this out

    // printf("Going to sleep now");
    delay(2000);
    esp_deep_sleep_start(); // change to deep?

    // printf("OK button was pressed.waking up");
    delay(2000);
    esp_restart();
}

bool mountSDCard()
{
    gMountedSDCard = false;
#ifdef USE_SPI_VERSION
    ESP_LOGI(TAG, "TRYING to mount SDCArd, SPI ");
    sd_card = new SDCard("/sdcard", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
#endif

#ifdef USE_SDIO_VERSION

    ESP_LOGI(TAG, "TRYING to mount SDCArd, SDIO ");
    sd_card = new SDCardSDIO("/sdcard");
    if (gMountedSDCard)
    {
        ESP_LOGI(TAG, "SD card mounted ");
        const char *ELOC_FOLDER = "/sdcard/eloc";
        if (!ffsutil::folderExists(ELOC_FOLDER))
        {
            ESP_LOGI(TAG, "%s does not exist, creating empty folder", ELOC_FOLDER);
            mkdir(ELOC_FOLDER, 0777);
        }
    }

#endif
    return gMountedSDCard;
}

void freeSpace()
{
    FATFS *fs;
    uint32_t fre_clust, fre_sect, tot_sect;
    /* Get volume information and free clusters of drive 0 */
    auto res = f_getfree("0:", &fre_clust, &fs);
    /* Get total sectors and free sectors */
    tot_sect = (fs->n_fatent - 2) * fs->csize;
    fre_sect = fre_clust * fs->csize;
    gFreeSpaceGB = float((float)fre_sect / 1048576.0 / 2.0);
    gFreeSpaceKB = fre_sect / 2;
    
    /* Print the free space (assuming 512 bytes/sector) */
    ESP_LOGI(TAG, "SD card total space: %10u KiB , %10u KiB available.", tot_sect / 2, fre_sect / 2);
    ESP_LOGI(TAG, "SD card free space: %2.1f GB, %u KB", gFreeSpaceGB, gFreeSpaceKB);
}

/**
 * @brief Checks if SD card is mounted and has sufficient free space (> 0.5 GB)
 * 
 * @return esp_err_t 
 */
esp_err_t checkSDCard()
{

    if (!gMountedSDCard)
    {
        // in case SD card is not yet mounted, mount it now, e.g. not inserted during boot
        if (!mountSDCard())
        {
            return ESP_ERR_NOT_FOUND;
        }
    }
    // getupdated free space
    freeSpace();
    if ((gFreeSpaceGB > 0.0) && (gFreeSpaceGB < 0.5))
    {
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        //  btwrite("SD Card full. Cannot record");
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "SD card is full, Free space %.3f GB", gFreeSpaceGB);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void saveStatusToSD()
{
    String sendstring;

    sendstring = sendstring + "Session ID:  " + gSessionIdentifier + "\n";

    sendstring = sendstring + "Session Start Time:  " + String(timeObject.getYear()) + "-" + String(timeObject.getMonth()) + "-" +
                 String(timeObject.getDay()) + " " + String(timeObject.getHour(true)) + ":" + String(timeObject.getMinute()) + ":" +
                 String(timeObject.getSecond()) + "\n";

    sendstring = sendstring + "Firmware Version:  " + gFirmwareVersion + "\n"; // firmware

    sendstring = sendstring + "File Header:  " + getDeviceInfo().location + "\n"; // file header

    sendstring = sendstring + "Bluetooth on when Record?:   " + (getConfig().bluetoothEnableDuringRecord ? "on" : "off") + "\n";

    sendstring = sendstring + "Sample Rate:  " + String(getMicInfo().MicSampleRate) + "\n";
    sendstring = sendstring + "Seconds Per File:  " + String(getConfig().secondsPerFile) + "\n";

    // sendstring=sendstring+   "Voltage Offset:  " +String(gVoltageOffset)                  + "\n" ;
    sendstring = sendstring + "Mic Type:  " + getMicInfo().MicType + "\n";
    sendstring = sendstring + "SD Card Free GB:   " + String(gFreeSpaceGB) + "\n";
    sendstring = sendstring + "Mic Gain:  " + String(getMicInfo().MicBitShift) + "\n";
    sendstring = sendstring + "GPS Location:  " + getDeviceInfo().locationCode + "\n";
    sendstring = sendstring + "GPS Accuracy:  " + getDeviceInfo().locationAccuracy + " m\n";

    // sendstring=sendstring+ "\n\n";

    FILE *fp;
    String temp = "/sdcard/eloc/" + gSessionIdentifier + "/" + "config_" + gSessionIdentifier + ".txt";
    fp = fopen(temp.c_str(), "wb");
    // fwrite()
    // String temp=
    // String temp="/sdcard/eloc/test.txt";
    // File file = SD.open(temp.c_str(), FILE_WRITE);
    // file.print(sendstring);
    fputs(sendstring.c_str(), fp);
    fclose(fp);
}

/**
 * @brief Get the configuration of the I2S microphone
 * @note Possible configuration sources are:
 *         1. '.config' file on SD card
 *         2. '.config' file on SPIFFS
 *         3. Setting in src/config.h
 * TODO: Confirm the priority of the configuration sources??
 * @return i2s_config_t
 */
i2s_config_t getI2sConfig()
{
    i2s_mic_Config.sample_rate = getMicInfo().MicSampleRate;
    i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL; // not getting set. getConfig().MicUseAPLL, //the only thing that works with LowPower/APLL is 16khz 12khz??
    if (i2s_mic_Config.sample_rate == 0)
    {
        ESP_LOGI(TAG, "Resetting invalid sample rate to default = %d", I2S_DEFAULT_SAMPLE_RATE);
        i2s_mic_Config.sample_rate = I2S_DEFAULT_SAMPLE_RATE;
    }

    ESP_LOGI(TAG, "Sample rate = %d", i2s_mic_Config.sample_rate);
    return i2s_mic_Config;
}

void start_sound_recording(FILE *fp){
    /**
     * @note The file pointer is set to nullptr in the wav_writer class
     *       but this is not reflected in the fp variable here
     *       hence the need to use 'is_file_handle_set()' getter
     */
    fp = nullptr;

    char file_name[100];

    createFilename(file_name, sizeof(file_name));
    fp = fopen(file_name, "wb");

    if (fp == nullptr)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    else
    {
        if (wav_writer->set_file_handle(fp) == false){
            ESP_LOGE(TAG, "Failed to set file handle");
            return;
        }
        wav_writer->set_enable_wav_file_write(true);
        // Start thread to continuously write to wav file & when sufficient data is collected finish the file
        wav_writer->start_wav_write_task(getConfig().secondsPerFile);
    }

}

#ifdef EDGE_IMPULSE_ENABLED

/** Audio buffers, pointers and selectors */

static inference_t inference;
// @deprecated sampleBuffer
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static int print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
static bool ei_running_status = false;

bool inference_result_file_SD_available = false;

// The following functions enable continuous audio sampling and inferencing
// https://docs.edgeimpulse.com/docs/tutorials/advanced-inferencing/continuous-audio-sampling

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffers[0] = (int16_t *)heap_caps_malloc(n_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (inference.buffers[0] == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for inference buffer", n_samples * sizeof(int16_t));
        return false;
    }

    inference.buffers[1] = (int16_t *)heap_caps_malloc(n_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);

    if (inference.buffers[1] == NULL)
    {
        ei_free(inference.buffers[0]);
        return false;
    }

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;
  
    // Microphone should have already been setup
    if (input == nullptr)
    {
        ESP_LOGE(TAG, "%s - I2SMEMSSampler == nullptr", __func__);
        ei_running_status = false;
    }
    else
    {
        input->register_ei_inference(&inference, EI_CLASSIFIER_FREQUENCY);
        ei_running_status = true;
    }

    input->start_read_task(sample_buffer_size/ sizeof(signed short));

    return true;
}

/**
 * @brief  Wait on new data.
 *         Blocking function.
 *         Unblocked by audio_inference_callback() setting inference.buf_ready
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void)
{
  ESP_LOGV(TAG, "Func: %s", __func__);

  bool ret = true;

  // TODO: Expect this to be set as loading from another point?
  // if (inference.buf_ready == 1)
  // {
  //   ESP_LOGE(TAG, "Error sample buffer overrun. Decrease the number of slices per model window "
  //       "(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW)");
  //   ret = false;
  // }

  while (inference.buf_ready == 0)
  {
    // NOTE: Trying to write audio out here seems to leads to poor audio performance?
    // if(wav_writer->buf_ready == 1){
    //   wav_writer->write();
    // }
    // else
    // {
      // Service watchdog
      delay(1);
    //}
  }

  inference.buf_ready = 0;
  
  return true;
}

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    // ESP_LOGI(TAG,"microphone_audio_signal_get_data()");
   numpy::int16_to_float(&inference.buffers[inference.buf_select ^ 1][offset], out_ptr, length);

    return 0;
}


/**
 * @brief Check if file exists to record inference result from Edge Impulse
 * @note If the file doesn't exits it will be created with the following details:
 *          EI Project ID, 186372
 *          EI Project owner, EDsteve
 *          EI Project name, trumpet
 *          EI Project deploy version, 2
 * @return 0 on success, -1 on fail
 */
int create_inference_result_file_SD(String f_name)
{

    if (checkSDCard() != ESP_OK)
    {
        // Abandon
        return -1;
    }

    FILE *fp = fopen(f_name.c_str(), "r");

    if (fp)
    {
        ESP_LOGI(TAG, "%s exists on SD card", f_name.c_str());
        fclose(fp);
        inference_result_file_SD_available = true;
        return 0;
    }

    String file_string;

    fp = fopen(f_name.c_str(), "wb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    // Possible other details to include in file
    if (0)
    {
        file_string += "EI Project ID, ";
        file_string += EI_CLASSIFIER_PROJECT_ID;
        file_string += "\nEI Project owner, ";
        file_string += EI_CLASSIFIER_PROJECT_OWNER;
        file_string += "\nEI Project name, ";
        file_string += EI_CLASSIFIER_PROJECT_NAME;
        file_string += "\nEI Project deploy version, ";
        file_string += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
    }

    // Column headers
    file_string += "\n\nYear-Month-Day Hour:Min:Sec";

    for (auto i = 0; i < EI_CLASSIFIER_NN_OUTPUT_COUNT; i++)
    {
        file_string += " ,";
        file_string += ei_classifier_inferencing_categories[i];
    }

    file_string += "\n";

    fputs(file_string.c_str(), fp);
    fclose(fp);

    inference_result_file_SD_available = true;

    return 0;
}

/**
 * @brief This function accepts a string, prepends date & time & appends to a csv file
 * @param file_string string in csv format, e.g. 0.94, 0.06
 * @return 0 on success, -1 on fail
 */
int save_inference_result_SD(String f_name, String results_string)
{

    FILE *fp = fopen(f_name.c_str(), FILE_APPEND);

    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    String file_string;

    file_string += getProperDateTime() + " " + results_string;

    fputs(file_string.c_str(), fp);
    fclose(fp);

    return 0;
}

#endif

void app_main(void)
{

    ESP_LOGI(TAG, "\nSETUP--start\n");
    initArduino();
    ESP_LOGI(TAG, "initArduino done");

    printPartitionInfo(); // so if reboots, always boot into the bluetooth partition

    ESP_LOGI(TAG, "\n"
                  "------------------------- ELOC Recorder -------------------------\n"
                  "-- VERSION: %s\n"
                  "-----------------------------------------------------------------\n",
             VERSIONTAG);

#ifdef EDGE_IMPULSE_ENABLED
    ESP_LOGI(TAG, "Edge Impulse framework enabled");
#endif

    initTime();

    printRevision();

    resetPeripherals();

#ifdef USE_SPI_VERSION
    gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(BATTERY_LED, GPIO_MODE_OUTPUT);

    gpio_set_direction(PIN_NUM_MISO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);

    gpio_set_direction(PIN_NUM_CLK, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_CLK, GPIO_PULLUP_ONLY);

    gpio_set_direction(PIN_NUM_MOSI, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_NUM_CS, GPIO_PULLUP_ONLY);

    // new
    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT); //
    gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
    // end new
    gpio_sleep_sel_dis(GPIO_BUTTON);
    gpio_sleep_sel_dis(OTHER_GPIO_BUTTON);
    gpio_sleep_sel_dis(PIN_NUM_MISO);
    gpio_sleep_sel_dis(PIN_NUM_CLK);
    gpio_sleep_sel_dis(PIN_NUM_MOSI);
    gpio_sleep_sel_dis(PIN_NUM_CS);
    gpio_sleep_sel_dis(I2S_MIC_SERIAL_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_LEFT_RIGHT_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_SERIAL_DATA);

    gpio_set_intr_type(OTHER_GPIO_BUTTON, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);

#endif

#ifdef USE_SDIO_VERSION
    gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(BATTERY_LED, GPIO_MODE_OUTPUT);

    gpio_sleep_sel_dis(I2S_MIC_SERIAL_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_LEFT_RIGHT_CLOCK);
    gpio_sleep_sel_dis(I2S_MIC_SERIAL_DATA);

    gpio_sleep_sel_dis(PIN_NUM_MISO);
    gpio_sleep_sel_dis(PIN_NUM_CLK);
    gpio_sleep_sel_dis(PIN_NUM_MOSI);
    gpio_sleep_sel_dis(PIN_NUM_CS);

    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT); //
    gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);
    gpio_sleep_sel_dis(GPIO_BUTTON);

#endif

    gpio_set_level(STATUS_LED, 0);
    gpio_set_level(BATTERY_LED, 0);

    ESP_LOGI(TAG, "Setting up HW System...");
    ElocSystem::GetInstance();

    ESP_LOGI(TAG, "Setting up Battery...");
    Battery::GetInstance();

    if (!SPIFFS.begin(true, "/spiffs"))
    {
        ESP_LOGI(TAG, "An Error has occurred while mounting SPIFFS");
        // return;
    }
    mountSDCard();
    freeSpace();

    if (0)
    {
        auto psram_size = esp_spiram_get_size();
        if (psram_size == 0)
            ESP_LOGW(TAG, "Error: SPI RAM (PSRAM) Not found");
        else
            ESP_LOGI(TAG, "Available SPI RAM (PSRAM): %d bytes, %d MBit", psram_size, (psram_size / 131072));
    }

    // print some file system info
    ESP_LOGI(TAG, "File system loaded: ");
    ffsutil::printListDir("/spiffs");
    ffsutil::printListDir("/sdcard");
    ffsutil::printListDir("/sdcard/eloc");

    readConfig();

    // check if a firmware update is triggered via SD card
    checkForFirmwareUpdateFile();

    rec_req_evt_queue = xQueueCreate(10, sizeof(rec_req_t));
    xQueueReset(rec_req_evt_queue);
    ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));

    ESP_LOGI(TAG, "Creating Bluetooth  task...");
    if (esp_err_t err = BluetoothServerSetup(false))
    {
        ESP_LOGI(TAG, "BluetoothServerSetup failed with %s", esp_err_to_name(err));
    }

#ifdef USE_PERF_MONITOR
    ESP_LOGI(TAG, "Creating Performance Monitor task...");
    if (esp_err_t err = PerfMonitor::setup())
    {
        ESP_LOGI(TAG, "Performance Monitor failed with %s", esp_err_to_name(err));
    }
#endif

#ifdef EDGE_IMPULSE_ENABLED

    auto save_ai_results_to_sd = true;

    String ei_results_filename = "/sdcard/eloc/";
    ei_results_filename += "EI-results-ID-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_ID;
    ei_results_filename += "-DEPLOY-VER-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
    ei_results_filename += ".csv";

    if (1)
    {
        ESP_LOGI(TAG, "EI results filename: %s", ei_results_filename.c_str());
    }

    // summary of inferencing settings (from model_metadata.h)
    ESP_LOGI(TAG, "Edge Impulse Inferencing settings:");
    ESP_LOGI(TAG, "Interval: %f ms.", (float)EI_CLASSIFIER_INTERVAL_MS);
    ESP_LOGI(TAG, "Frame size: %d", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ESP_LOGI(TAG, "Sample length: %d ms.", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ESP_LOGI(TAG, "No. of classes: %d", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    // Check if file exists to record results
    if (gMountedSDCard == true)
    {
        create_inference_result_file_SD(ei_results_filename);
    }

    if (1)
    {
        // Run stored audio samples through the model to test it
        ESP_LOGI(TAG, "Testing model against pre-recorded sample data...");

        static_assert((EI_CLASSIFIER_RAW_SAMPLE_COUNT <= TEST_SAMPLE_LENGTH), "TEST_SAMPLE_LENGTH must be at least equal to EI_CLASSIFIER_RAW_SAMPLE_COUNT");

        if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH){
            ESP_LOGI(TAG, "TEST_SAMPLE length is greater than the Edge Impulse model length, applying downsampling");
        }

        signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = {0};

        inference.buffers[0] = (int16_t *)heap_caps_malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t), MALLOC_CAP_SPIRAM);

        if (!inference.buffers[0])
        {
            ESP_LOGW(TAG, "ERR: Failed to allocate buffer for signal");
            // Skip the rest of the test
            return;
        }

        // Artifically fill buffer with test data
        // WARNING: Limit to buffer size
        auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        auto skip_current = ei_skip_rate;   // Make sure to fill first sample, then start skipping if needed

        for (auto test_sample_count = 0, inference_buffer_count = 0; (test_sample_count < TEST_SAMPLE_LENGTH) && 
                (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT); test_sample_count++)
        {
            if(skip_current >= ei_skip_rate){
                inference.buffers[0][inference_buffer_count++] = trumpet_test[test_sample_count];
                skip_current = 1;
            }
            else{
                skip_current++;
            }
        }

        // Mark buffer as ready
        inference.buf_select = 1;   // Mark active buffer as inference.buffers[1], inference run on inactive buffer
        inference.buf_count = 0;
        inference.buf_ready = 1;

        EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
        if (r != EI_IMPULSE_OK)
        {
            ESP_LOGW(TAG, "ERR: Failed to run classifier (%d)\n", r);
            return;
        }

        // print the predictions
        ESP_LOGI(TAG, "Test model predictions:");
        ESP_LOGI(TAG, "    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                 result.timing.dsp, result.timing.classification, result.timing.anomaly);
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
        {
            ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);
        }

        // Reset & free buffers
        inference.buf_select = 0;
        inference.buf_ready = 0;
        if (inference.buffers[0])
            heap_caps_free(inference.buffers[0]);
    }

#endif

    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    // ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    /** Setup Power Management */
    esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = getConfig().cpuMaxFrequencyMHZ,
        .min_freq_mhz = getConfig().cpuMinFrequencyMHZ,
        .light_sleep_enable = getConfig().cpuEnableLightSleep};
    esp_pm_configure(&cfg);
    // calling gpio_sleep_sel_dis must be done after esp_pm_configure() otherwise they will get overwritten
    // interrupt must be enabled during sleep otherwise they might continously trigger (missing pullup)
    // or won't work at all
    gpio_sleep_sel_dis(GPIO_BUTTON);
    gpio_sleep_sel_dis(LIS3DH_INT_PIN);
    // now setup GPIOs as wakeup source. This is required to wake up the recorder in tickless idle
    // BUGME: this seems to be a bug in ESP IDF: https://github.com/espressif/arduino-esp32/issues/7158
    //       gpio_wakeup_enable does not correctly switch to rtc_gpio_wakeup_enable() for RTC GPIOs.
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(LIS3DH_INT_PIN, GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(GPIO_BUTTON, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

    if (getConfig().testI2SClockInput)
        testInput();

    ESP_LOGI(TAG, "waiting for button or bluetooth");
    ESP_LOGI(TAG, "voltage is %.3f", Battery::GetInstance().getVoltage());

    /**
     * Get the configuration of the I2S microphone & start it
     * @warning TODO: Check requirements if the configuration changes after this point? Restart required?
     */
    getI2sConfig();

    input = new I2SMEMSSampler(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);
    input->start();
    // Zero DMA buffer, prevents popping sound on start
    input->zero_dma_buffer(I2S_DEFAULT_PORT);              

    // create a new wave file wav_writer & make sure sample rate is up to date
    wav_writer = new WAVFileWriter((int32_t)(i2s_get_clk(I2S_DEFAULT_PORT)), 2, NUMBER_OF_CHANNELS);
    // Block until properly registered otherwise will get error later
    while (input->register_wavFileWriter(wav_writer) == false){
        ESP_LOGW(TAG, "Waiting for WAVFileWriter to register");
        delay(5);
    }

    // wav file pointer
    // TODO: Move to wav_writer class
    FILE *fp = nullptr;
    //Populate gSessionIdentifier for wav filename
    createSessionFolder();

    rec_req_t rec_req = REC_REQ_NONE;

    // TODO: Define conditions on which device shuld exit this while() loop
    // e.g. SD card full or I2S error?

    // DEBUG
    wav_writer->set_mode(WAVFileWriter::Mode::continuous);

    while (true)
    {
        // Start a new recording?
        if (wav_writer->is_file_handle_set() == false && 
            wav_writer->get_mode() == WAVFileWriter::Mode::continuous &&
            checkSDCard() == ESP_OK)
        {   
            start_sound_recording(fp);
        }

#ifdef EDGE_IMPULSE_ENABLED
        // Try to restart if not running
        if (ei_running_status == false){
            if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false)
            {
                // ESP_LOGE(TAG, "ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
                ESP_LOGE(TAG, "ERR: microphone_inference_start failed, restarting...");
                delay(500);
                microphone_inference_end();
            }
        }

        if (ai_run_enable == true && ei_running_status == true)
        {
            bool m = microphone_inference_record();
            // Blocking function - unblocks when buffer is full
            if (!m)
            {
                ESP_LOGE(TAG, "ERR: Failed to record audio...");
                continue;
            }

            signal_t signal;
            signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
            signal.get_data = &microphone_audio_signal_get_data;
            ei_impulse_result_t result = {0};

            // If changing to non-continuous ensure to use:
            // EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
            EI_IMPULSE_ERROR r = run_classifier_continuous(&signal, &result, debug_nn);
            if (r != EI_IMPULSE_OK)
            {
                ESP_LOGE(TAG, "ERR: Failed to run classifier (%d)", r);
                ei_running_status = false;
                continue;
            }

            //if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW))
            // Output every result instead of every EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW
            if (++print_results >= 1)
            {
                // print the predictions
                ESP_LOGI(TAG, "Predictions ");
                ESP_LOGI(TAG, "(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                         result.timing.dsp, result.timing.classification, result.timing.anomaly);

                String file_str;

                for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
                {
                    ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                    // Build string to save to inference results file
                    file_str += ", ";
                    file_str += result.classification[ix].value;

                    if ((strcmp(result.classification[ix].label, "background") != 0) && 
                        result.classification[ix].value > AI_RESULT_THRESHOLD)
                    {
                        // Start recording??
                        if (wav_writer->is_file_handle_set() == false && 
                            wav_writer->get_mode() == WAVFileWriter::Mode::single &&
                            checkSDCard() == ESP_OK)
                        {   
                            start_sound_recording(fp);
                        }

                    }
                }

                file_str += "\n";
                // Save results to file
                // TODO: Only save results & wav file if classification value exceeds a threshold?
                if (save_ai_results_to_sd == true && checkSDCard() == ESP_OK)
                {
                    save_inference_result_SD(ei_results_filename, file_str);
                }              

            #if EI_CLASSIFIER_HAS_ANOMALY == 1
                ESP_LOGI(TAG, "    anomaly score: %f", result.anomaly);
            #endif
                print_results = 0;
            }
        }// end while(ei_running_status == true)

#endif // EDGE_IMPULSE_ENABLED

        // Don't forget the watchdog
        delay(1);

    } // end while(true)

    // Should never get here
    if (input != nullptr){
        input->stop();
        delete input;
        input = nullptr;
    }
    
    if (wav_writer != nullptr){
        wav_writer->finish();
        delete wav_writer;
        wav_writer = nullptr;
    }

    if (sd_card != nullptr){
        delete sd_card;
        sd_card = nullptr;
    }

    ESP_LOGI(TAG, "app_main done");

    // TODO: Trigger reset here & then restart??
}

#ifdef EDGE_IMPULSE_ENABLED
    #if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
    #error "Invalid model for current sensor."
    #endif
#endif