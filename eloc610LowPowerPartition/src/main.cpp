

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
//#include  "periph_ctrl.h"
//#include "I2SOutput.h"
#include "SDCardSDIO.h"

#include "SDCard.h"
#include "SPIFFS.h"
#include <FFat.h>
#include <ff.h>
#include "WAVFileWriter.h"
//#include "WAVFileReader.h"
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
//#include "soc/efuse_reg.h"

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
#include "ElocStatus.hpp"
#include "utils/logging.hpp"
#include "Commands/BluetoothServer.hpp"
#include "FirmwareUpdate.hpp"
#include "PerfMonitor.hpp"


// TODO: Remove this..
// #define EDGE_IMPULSE_ENABLED

#ifdef EDGE_IMPULSE_ENABLED

    #include "EdgeImpulse.hpp"              // This file includes trumpet_inferencing.h
    #include "edge-impulse-sdk/dsp/numpy_types.h"
    #include "test_samples.h"

    EdgeImpulse *edgeImpulse = nullptr;

    // BUGME: this is rather crappy encapsulation.. signal_t requires non class function pointers
    //       but all EdgeImpulse stuff got encapsulated within a class, which does not match
    int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
       return edgeImpulse->microphone_audio_signal_get_data(offset, length, out_ptr);
    }

#endif

static const char *TAG = "main";

/**
 * @brief Should inference be run on sound samples? 
 * @todo Set from Bluetooth / config file
 */
bool ai_run_enable = true;

/**
 * @brief The size of the buffer for I2SMEMSampler to store the sound samples
 *        Appears to be a magic number!
 *        This value is used in both continuous & non-continuous inferencing examples from Edge Impulse
 *        Happens to be twice the dma_buf_len of 512
 */
const uint32_t sample_buffer_size = sizeof (signed short) * 1024;

uint64_t gStartupTime; // gets read in at startup to set system time.

char gSessionFolder[65];

int gMinutesWaitUntilDeepSleep = 60; // change to 1 or 2 for testing

ESP32Time timeObject;
// WebServer server(80);
// bool updateFinished=false;
bool gWillUpdate = false;
float gFreeSpaceGB = 0.0;
uint32_t gFreeSpaceKB = 0;

// String gTimeDifferenceCode; //see getTimeDifferenceCode() below


I2SMEMSSampler *input = nullptr;
WAVFileWriter *wav_writer = nullptr;
QueueHandle_t rec_req_evt_queue = nullptr;  // wav recording queue
QueueHandle_t rec_ai_evt_queue = nullptr;   // AI inference queue
TaskHandle_t i2s_TaskHandler = nullptr;     // Task handler from I2S to wav writer & AI inference

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


/**
 * @brief Receive button presses & set sound recording state
 * @todo Long button press should change AI detection state
 * @note Terminal output from this function seems to cause watchdog to timeout
 * @param args 
 */
static void IRAM_ATTR buttonISR(void *args)
{
    if (wav_writer == nullptr) {
        return;
    }
        
    if(wav_writer->get_mode() == WAVFileWriter::Mode::disabled){
        wav_writer->set_mode(WAVFileWriter::Mode::continuous);
    } else {
        wav_writer->set_mode(WAVFileWriter::Mode::disabled);
    }

    /**
     * Any point in send this command via the queue instead?
     */

    //xQueueSendFromISR(rec_req_evt_queue, &mode, (TickType_t)0);
}

static void LEDflashError() {
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

void time() {

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
    //TODO: check if another session identifier based on ISO time for mat would be more helpful
    gSessionIdentifier = getDeviceInfo().fileHeader + uint64ToString(getSystemTimeMS());
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


    // TODO: set this as not recording in WAVFileWriter.cpp::finish()
    // gRecording=RecState::RECORDING;
    
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

bool inference_result_file_SD_available = false;

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

    if (ElocSystem::GetInstance().checkSDCard() != ESP_OK)
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
        ei_impulse_result_t results = { 0 };
        file_string += " ,";
        file_string += results.classification[i].label;

        // FIXME:
        // Need to create a wrapper to access this
        //file_string += ei_classifier_inferencing_categories[i];
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

    #ifdef AI_CONTINUOUS_INFERENCE
        ESP_LOGI(TAG, "Continuous inference enabled");
    #else
        ESP_LOGI(TAG, "Continuous inference disabled");
    #endif
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

    // TODO: BUGME: Crash when writing to wav & log file simultaneously?

    //setup persistent logging only if SD card is mounted
    if (ElocSystem::GetInstance().isSDCardMounted()) {
        const logConfig_t& cfg= getConfig().logConfig;
        esp_err_t err = Logging::init(cfg.logToSdCard, cfg.filename, cfg.maxFiles, cfg.maxFileSize);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initilize logging subsystem with %s", esp_err_to_name(err));
        }
    }

    // check if a firmware update is triggered via SD card
    checkForFirmwareUpdateFile();

    // Queue for recording requests
    rec_req_evt_queue = xQueueCreate(10, sizeof(rec_req_t));
    xQueueReset(rec_req_evt_queue);

    // Queue for AI requests
    rec_ai_evt_queue = xQueueCreate(10, sizeof(bool));
    xQueueReset(rec_ai_evt_queue);


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

    // Check if file exists to record results
    if (ElocSystem::GetInstance().isSDCardMounted() == true)
    {
        create_inference_result_file_SD(ei_results_filename);
    }

    edgeImpulse = new EdgeImpulse(I2S_DEFAULT_SAMPLE_RATE);   
    edgeImpulse->output_inferencing_settings();

    // TODO: Set some flag if this fails??
    edgeImpulse->buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);

    if (1){
        // Run stored audio samples through the model to test it
        // Use non-continous process for this
        ESP_LOGI(TAG, "Testing model against pre-recorded sample data...");

        static_assert((EI_CLASSIFIER_RAW_SAMPLE_COUNT <= TEST_SAMPLE_LENGTH), "TEST_SAMPLE_LENGTH must be at least equal to EI_CLASSIFIER_RAW_SAMPLE_COUNT");

        if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH){
            ESP_LOGI(TAG, "TEST_SAMPLE length is greater than the Edge Impulse model length, applying downsampling");
        }

        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = {0};

        // Artifically fill buffer with test data
        auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        auto skip_current = ei_skip_rate;   // Make sure to fill first sample, then start skipping if needed

        for (auto i = 0; i < test_array_size; i++){

            ESP_LOGI(TAG, "Running test category: %s", test_array_categories[i]);

            for (auto test_sample_count = 0, inference_buffer_count = 0; (test_sample_count < TEST_SAMPLE_LENGTH) && 
                    (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT); test_sample_count++)
            {
                if(skip_current >= ei_skip_rate){
                    edgeImpulse->inference.buffers[0][inference_buffer_count++] = test_array[i][test_sample_count];
                    skip_current = 1;
                }
                else{
                    skip_current++;
                }
            }

            // Mark buffer as ready
            edgeImpulse->inference.buf_select = 1;   // Mark active buffer as inference.buffers[1], inference run on inactive buffer
            edgeImpulse->inference.buf_count = 0;
            edgeImpulse->inference.buf_ready = 1;

            EI_IMPULSE_ERROR r = edgeImpulse->run_classifier(&signal, &result);
            if (r != EI_IMPULSE_OK) 
            {
                ESP_LOGW(TAG,"ERR: Failed to run classifier (%d)", r);
                return;
            }

            // print the predictions
            ESP_LOGI(TAG, "Test model predictions:");
            ESP_LOGI(TAG, "    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                    result.timing.dsp, result.timing.classification, result.timing.anomaly);
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
            {
                ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                if (strcmp(result.classification[ix].label, "trumpet") == 0 && strcmp(test_array_categories[i], "trumpet") == 0)
                {
                    if (result.classification[ix].value < AI_RESULT_THRESHOLD)
                    {
                        ESP_LOGW(TAG, "Test of trumpet sample appears to be poor, check model!");
                    }
                }
            }
        }

        // Free buffers as the buffer size for continouus & non-continous differs
        edgeImpulse->free_buffers();
    }

    #ifdef AI_CONTINUOUS_INFERENCE
        edgeImpulse->buffers_setup(EI_CLASSIFIER_SLICE_SIZE);
        auto print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);
    #else
        edgeImpulse->buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
    #endif // AI_CONTINUOUS_INFERENCE
       
    #endif // EDGE_IMPULSE_ENABLED

    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    // ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    /** Setup Power Management */
    ElocSystem::GetInstance().pm_configure();

    if (getConfig().testI2SClockInput)
        testInput();

    /**
     * Get the configuration of the I2S microphone & start it
     * @warning TODO: Check requirements if the configuration changes after this point? Restart required?
     */
    getI2sConfig();

    /**
     * @note Using MicUseTimingFix == true or false doesn't seem to effect ICS-43434 mic
     */
    input = new I2SMEMSSampler(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);

    if (input == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create I2SMEMSSampler");
        return;
    }
    else{
        input->start();

        // Zero DMA buffer, prevents popping sound on start
        input->zero_dma_buffer(I2S_DEFAULT_PORT);              

        if (ElocSystem::GetInstance().checkSDCard() == ESP_OK){
            // create a new wave file wav_writer & make sure sample rate is up to date
            wav_writer = new WAVFileWriter((int32_t)(i2s_get_clk(I2S_DEFAULT_PORT)), 2, NUMBER_OF_CHANNELS);
            // Block until properly registered otherwise will get error later
            while (input->register_wavFileWriter(wav_writer) == false){
                ESP_LOGW(TAG, "Waiting for WAVFileWriter to register");
                delay(5);
            }

            //Populate gSessionIdentifier for wav filename
            // TODO: Should only be created on first recording
            createSessionFolder();

            // TODO: DEBUG - should be set from Bluetooth config
            wav_writer->set_mode(WAVFileWriter::Mode::disabled);
        }
        else
        {
            ESP_LOGE(TAG, "SD card not mounted, cannot create WAVFileWriter");
            wav_writer = nullptr;
            #ifdef EDGE_IMPULSE_ENABLED
                save_ai_results_to_sd = false;
            #endif
        }

        #ifdef EDGE_IMPULSE_ENABLED
            #ifdef AI_CONTINUOUS_INFERENCE
                // Init static vars
                edgeImpulse->run_classifier_init();
            #endif // AI_CONTINUOUS_INFERENCE
            
            input->register_ei_inference(&edgeImpulse->getInference(), EI_CLASSIFIER_FREQUENCY);
            edgeImpulse->set_status(edgeImpulse->Status::running);
        #endif
        
        input->start_read_task(sample_buffer_size/ sizeof(signed short));
    }

    // wav file pointer
    // TODO: Move to wav_writer class
    FILE *fp = nullptr;

    auto loopCnt = 0;

    // TODO: Define conditions on which device shuld exit this while() loop
    // e.g. SD card full or I2S error?

    // This might be redundant, set directly in ElocCommands.cpp
    auto new_mode =  WAVFileWriter::Mode::disabled;
    
    while (true)
    {
        if (xQueueReceive(rec_req_evt_queue, &new_mode, pdMS_TO_TICKS(500))) {
            if(new_mode == WAVFileWriter::Mode::continuous)
            {
                ESP_LOGI(TAG,"wav writer mode = continuous");
                wav_writer->set_mode(new_mode);
            }
            else if(new_mode == WAVFileWriter::Mode::single)
            {
                ESP_LOGI(TAG,"wav writer mode = single");
                wav_writer->set_mode(new_mode);
            }
            else if(new_mode == WAVFileWriter::Mode::disabled){
                ESP_LOGI(TAG,"wav writer mode = disabled");
                wav_writer->set_mode(new_mode);
            }
            else{
                ESP_LOGE(TAG,"wav writer mode = unknown");
            }
        }

        // TODO: Check this is valid, rebased from master
        if ((loopCnt++ % 10) == 0) {
            ESP_LOGI(TAG, "Battery: Voltage: %.3fV, %.0f%% SoC, Temp %d °C", 
            Battery::GetInstance().getVoltage(), Battery::GetInstance().getSoC(), ElocSystem::GetInstance().getTemperaure());
            
            // Display memory usage
            ESP_LOGI(TAG, "Min free heap since boot = %d bytes", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
            ESP_LOGI(TAG, "Min free PSRAM since boot = %d bytes", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
        }

        // Start a new recording?
        if (wav_writer != nullptr &&
            wav_writer->is_file_handle_set() == false && 
            wav_writer->get_mode() == WAVFileWriter::Mode::continuous &&
            ElocSystem::GetInstance().checkSDCard() == ESP_OK)
        {   
            start_sound_recording(fp);
        }

#ifdef EDGE_IMPULSE_ENABLED

        if (xQueueReceive(rec_ai_evt_queue, &ai_run_enable, pdMS_TO_TICKS(500))) {
            ESP_LOGI(TAG, "Received AI run enable = %d", ai_run_enable);
        }            

        // Try to restart if not running
        if (edgeImpulse->get_status() == edgeImpulse->Status::not_running)
        {       
                auto buf_setup_result = false;

                #ifdef AI_CONTINUOUS_INFERENCE
                    buf_setup_result = edgeImpulse->buffers_setup(EI_CLASSIFIER_SLICE_SIZE);
                #else
                    buf_setup_result = edgeImpulse->buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
                #endif // AI_CONTINUOUS_INFERENCE

            if (buf_setup_result == false)
            {
                ESP_LOGE(TAG, "ERR: inference_stetup failed, restarting...");
                delay(500);
                edgeImpulse->set_status(edgeImpulse->Status::not_running);
            }
        }

        if (ai_run_enable == true && 
            edgeImpulse->get_status() == edgeImpulse->Status::running)
        {
            bool m = edgeImpulse->microphone_inference_record();
            // Blocking function - unblocks when buffer is full
            if (!m)
            {
                ESP_LOGE(TAG, "ERR: Failed to record audio...");
                continue;
            }

            auto startCounter = cpu_hal_get_cycle_count(); 

            ei::signal_t signal;
            
            #ifdef AI_CONTINUOUS_INFERENCE
                signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
            #else
                signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
            #endif // AI_CONTINUOUS_INFERENCE

            signal.get_data = &microphone_audio_signal_get_data;
            ei_impulse_result_t result = {0};

            #ifdef AI_CONTINUOUS_INFERENCE
                EI_IMPULSE_ERROR r = edgeImpulse->run_classifier_continuous(&signal, &result);
            #else
                EI_IMPULSE_ERROR r = edgeImpulse->run_classifier(&signal, &result);
            #endif // AI_CONTINUOUS_INFERENCE

            ESP_LOGI(TAG, "Cycles taken to run inference = %d", (cpu_hal_get_cycle_count() - startCounter)); 
           
            if (r != EI_IMPULSE_OK)
            {
                ESP_LOGE(TAG, "ERR: Failed to run classifier (%d)", r);
                edgeImpulse->set_status(edgeImpulse->Status::not_running);
                continue;
            }

            #ifdef AI_CONTINUOUS_INFERENCE
                if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW))
            #else
                // Non-continous, always print
                if (1)
            #endif // AI_CONTINUOUS_INFERENCE
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
                            if (wav_writer != nullptr &&
                                wav_writer->is_file_handle_set() == false && 
                                wav_writer->get_mode() == WAVFileWriter::Mode::single &&
                                ElocSystem::GetInstance().checkSDCard() == ESP_OK)
                            {   
                                start_sound_recording(fp);
                            }
                        }
                    }

                    file_str += "\n";
                    // Save results to file
                    // TODO: Only save results & wav file if classification value exceeds a threshold?
                    if (save_ai_results_to_sd == true && ElocSystem::GetInstance().checkSDCard() == ESP_OK)
                    {
                        save_inference_result_SD(ei_results_filename, file_str);
                    }              

                #if EI_CLASSIFIER_HAS_ANOMALY == 1
                    ESP_LOGI(TAG, "    anomaly score: %f", result.anomaly);
                #endif // EI_CLASSIFIER_HAS_ANOMALY

                #ifdef AI_CONTINUOUS_INFERENCE
                    print_results = 0;
                #endif // AI_CONTINUOUS_INFERENCE

                }
        }// end while(ei_running_status == true)
#else
        // Delay longer if not EI enabled
        delay(300);

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

    ESP_LOGI(TAG, "app_main done");

    // TODO: Trigger reset here & then restart??
}

#ifdef EDGE_IMPULSE_ENABLED
    #if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
    #error "Invalid model for current sensor."
    #endif
#endif
