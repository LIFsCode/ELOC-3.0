

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
#include "config.h"
#include <string.h>

#include "esp_partition.h"
#include "esp_ota_ops.h"

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
#include "utils/time_utils.hpp"
#include "ElocProcessFactory.hpp"


static const char *TAG = "main";


bool gMountedSDCard = false;

/**
 * @brief The size of the buffer for I2SMEMSampler to store the sound samples
 *        Appears to be a magic number!
 *        This value is used in both continuous & non-continuous inferencing examples from Edge Impulse
 *        Happens to be twice the dma_buf_len of 512
 */
const uint32_t sample_buffer_size = sizeof (signed short) * 1024;

uint64_t gStartupTime;  // gets read in at startup to set system time.

int gMinutesWaitUntilDeepSleep = 60;  // change to 1 or 2 for testing

ESP32Time timeObject;
// WebServer server(80);
// bool updateFinished=false;
bool gWillUpdate = false;
float gFreeSpaceGB = 0.0;
uint32_t gFreeSpaceKB = 0;

// String gTimeDifferenceCode; //see getTimeDifferenceCode() below

#ifdef USE_SPI_VERSION
    SDCard sd_card;
#endif

#ifdef USE_SDIO_VERSION
    SDCardSDIO sd_card;
#endif

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

    if (0) {
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

    if (0) {
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

void resetESP32()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    ESP_LOGI(TAG, "Resetting..");
    resetPeripherals();
    // esp_task_wdt_init(30, false); //bump it up to 30 econds doesn't work.
    rtc_wdt_protect_off();  // Disable RTC WDT write protection

    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_RTC);
    rtc_wdt_set_time(RTC_WDT_STAGE0, 500);
    rtc_wdt_enable();     // Start the RTC WDT timer
    rtc_wdt_protect_on();  // Enable RTC WDT write protection

    // ESP_LOGI(TAG, "starting deep sleep");
    // delay(50);
    // esp_deep_sleep_start();
    // ESP_LOGI(TAG, "deep sleep should never make it here");

    delay(1000);  // should never get here
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

/**
 * @brief Receive button presses & set sound recording state
 * @todo Long button press should change AI detection state
 * @note Terminal output from this function seems to cause watchdog to timeout
 * @param args
 */
static void IRAM_ATTR buttonISR(void *args)
{
    RecState new_mode;
    if (elocProcessing.getState() != RecState::recordOff_detectOff) {
        new_mode = RecState::recordOn_detectOff;
    }
    else {
        new_mode = RecState::recordOff_detectOff;
    }
    elocProcessing.queueNewModeISR(new_mode);
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
    for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "\tfound partition '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, part->label, part->address, part->size);
    }

    // Release the partition iterator to release memory allocated for it
    esp_partition_iterator_release(it);

    ESP_LOGI(TAG, "Currently running partitions: ");
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "\t '%s' at offset 0x%" PRIx32 " with size 0x%" PRIx32, running->label, running->address, running->size);

    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }
}

/**
 * @brief Set initial time
 * @note If time is not set the getLocalTime() will stuck for 5 ms due to invalid timestamp
 * @todo: Move to time_utils.cpp
*/
void initTime()
{
    struct tm tm;
    strptime(BUILDDATE, "%b %d %Y %H:%M:%S %Y", &tm);
    time_t timeSinceEpoch = mktime(&tm);
    timeObject.setTime(timeSinceEpoch);
    ESP_LOGI(TAG, "Setting initial time to build date: %s", BUILDDATE);
}

void doDeepSleep()
{
    esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0);  // try commenting this out

    // printf("Going to sleep now");
    delay(2000);
    esp_deep_sleep_start();  // change to deep?

    // printf("OK button was pressed.waking up");
    delay(2000);
    esp_restart();
}

bool mountSDCard()
{
    gMountedSDCard = false;
#ifdef USE_SPI_VERSION
    ESP_LOGI(TAG, "TRYING to mount SDCArd, SPI ");
    sd_card.init("/sdcard", PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
#endif

#ifdef USE_SDIO_VERSION

        ESP_LOGI(TAG, "TRYING to mount SDCArd, SDIO ");
        sd_card.init("/sdcard");
        if (sd_card.isMounted()) {
            ESP_LOGI(TAG, "SD card mounted ");
            const char* ELOC_FOLDER = "/sdcard/eloc";
            if (!ffsutil::folderExists(ELOC_FOLDER)) {
                ESP_LOGI(TAG, "%s does not exist, creating empty folder", ELOC_FOLDER);
                mkdir(ELOC_FOLDER, 0777);
            }
            float freeSpace = sd_card.freeSpaceGB();
            float totalSpace = sd_card.getCapacityMB()/1024;
            ESP_LOGV(TAG, "SD card %f / %f GB free", freeSpace, totalSpace);
        }
    #endif
    gMountedSDCard = sd_card.isMounted();
    return gMountedSDCard;
}

esp_err_t checkSDCard() {
    if (!gMountedSDCard) {
        // in case SD card is not yet mounted, mount it now, e.g. not inserted durint boot
        if (!mountSDCard()) {
            return ESP_ERR_NOT_FOUND;
        }
    }
    // getupdated free space
    float freeSpaceGB = sd_card.freeSpaceGB();
    if ((freeSpaceGB > 0.0) && (freeSpaceGB < 0.5)) {
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        //  btwrite("SD Card full. Cannot record");
        //  btwrite("!!!!!!!!!!!!!!!!!!!!!");
        ESP_LOGE(TAG, "SD card is full, Free space %.3f GB", freeSpaceGB);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void app_main(void) {
    ESP_LOGI(TAG, "\nSETUP--start\n");
    initArduino();
    ESP_LOGI(TAG, "initArduino done");

    printPartitionInfo();  // So if reboots, always boot into the bluetooth partition


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
    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT);
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

    gpio_set_direction(GPIO_BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(GPIO_BUTTON, GPIO_INTR_POSEDGE);
    gpio_sleep_sel_dis(GPIO_BUTTON);

#endif

    gpio_set_level(STATUS_LED, 0);
    gpio_set_level(BATTERY_LED, 0);

    ESP_LOGI(TAG, "Setting up HW System...");
    ElocSystem::GetInstance();

    if (!SPIFFS.begin(true, "/spiffs")) {
        ESP_LOGI(TAG, "An Error has occurred while mounting SPIFFS");
        // return;
    }
    mountSDCard();

    if (0) {
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

    // Setup persistent logging only if SD card is mounted
    if (sd_card.isMounted()) {
        const logConfig_t& cfg = getConfig().logConfig;
        esp_err_t err = Logging::init(cfg.logToSdCard, cfg.filename, cfg.maxFiles, cfg.maxFileSize);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize logging subsystem with %s", esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "\n"
                  "------------------------- ELOC Recorder -------------------------\n"
                  "-- VERSION: %s\n"
                  "-----------------------------------------------------------------\n",
             VERSIONTAG);

    ESP_LOGI(TAG, "Setting up Battery...");
    Battery::GetInstance();

    // check if a firmware update is triggered via SD card
    checkForFirmwareUpdateFile();

    ESP_ERROR_CHECK(gpio_install_isr_service(GPIO_INTR_PRIO));

    ESP_LOGI(TAG, "Creating Bluetooth  task...");
    if (esp_err_t err = BluetoothServerSetup(false)) {
        ESP_LOGI(TAG, "BluetoothServerSetup failed with %s", esp_err_to_name(err));
    }

#ifdef USE_PERF_MONITOR
    ESP_LOGI(TAG, "Creating Performance Monitor task...");
    if (esp_err_t err = PerfMonitor::setup()) {
        ESP_LOGI(TAG, "Performance Monitor failed with %s", esp_err_to_name(err));
    }
#endif


    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    // ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    /** Setup Power Management */
    ElocSystem::GetInstance().pm_configure();

    elocProcessing.init();

    auto loopCnt = 0;


    while (true) {
        // Check the queues for new commanded mode and change mode if necessary
        if (elocProcessing.checkQueueAndChangeMode(pdMS_TO_TICKS(500))) {
            
        }
        if ((loopCnt++ % 10) == 0) {
            Battery::GetInstance().updateVoltage();  // only updates actual as often as set in the config
            ESP_LOGI(TAG, "Battery: Voltage: %.3fV, %.0f%% SoC, Temp %d °C",
            Battery::GetInstance().getVoltage(), Battery::GetInstance().getSoC(), ElocSystem::GetInstance().getTemperaure());

            // Display memory usage
            if (1) {
                multi_heap_info_t heapInfo;
                heap_caps_get_info(&heapInfo, MALLOC_CAP_INTERNAL);
                ESP_LOGI(TAG, "Heap: Min=%d, free=%d (%d%%), largestFreeBlock=%d, fragmentation=%d%%",
                    heapInfo.minimum_free_bytes, heapInfo.total_free_bytes,
                    100 - (heapInfo.total_free_bytes*100) / heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                    heapInfo.largest_free_block,
                    100 - (heapInfo.largest_free_block*100) / heapInfo.total_free_bytes);
                heap_caps_get_info(&heapInfo, MALLOC_CAP_SPIRAM);
                ESP_LOGI(TAG, "PSRAM Heap: Min=%d, free=%d (%d%%), largestFreeBlock=%d, fragmentation=%d%%",
                    heapInfo.minimum_free_bytes, heapInfo.total_free_bytes,
                    100 - (heapInfo.total_free_bytes*100) / heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                    heapInfo.largest_free_block,
                    100 - (heapInfo.largest_free_block*100) / heapInfo.total_free_bytes);
            }
        }


        // Don't forget the watchdog
        delay(1);
    }  // end while(true)

    // Should never get here
    elocProcessing.getInput().uninstall();
    elocProcessing.getWavWriter().finish();

    if (sd_card.isMounted()) {
        sd_card.~SDCardSDIO();
    }

    ESP_LOGI(TAG, "app_main done");

    // TODO: Trigger reset here & then restart??
}

#ifdef EDGE_IMPULSE_ENABLED
    #if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
    #error "Invalid model for current sensor."
    #endif
#endif
