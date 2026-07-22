

#include "esp_err.h"
#include "esp_log.h"
#include "esp32/clk.h"
#include <rom/ets_sys.h>
#include "esp_pm.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "I2SMEMSSampler.h"
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
#include <driver/uart.h>
//#include "soc/efuse_reg.h"

/** Arduino libraries*/
#include "ESP32Time.h"
#include "BluetoothSerial.h"
#include "esp_bt.h"
/** Arduino libraries END*/

#include "version.h"

#include "ffsutils.h"
#include "lis3dh.h"
#include "Battery.hpp"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "ElocStatus.hpp"
#include "ElocLora.hpp"
#include "logging.hpp"
#include "BluetoothServer.hpp"
#include "FirmwareUpdate.hpp"
#include "PerfMonitor.hpp"

#ifdef USE_GPS
    #include "ElocGPS.hpp"
#endif

#ifdef ENABLE_TEST_UART
    #include "uart_eloc.h"
#endif

static const char *TAG = "main";

#ifdef EDGE_IMPULSE_ENABLED

    #include "EdgeImpulse.hpp"              // This file includes trumpet_inferencing.h
    #include "edge-impulse-sdk/dsp/numpy_types.h"
    #include "test_samples.h"

    EdgeImpulse edgeImpulse(I2S_DEFAULT_SAMPLE_RATE);

    String ei_results_filename;

    // BUGME: this is rather crappy encapsulation.. signal_t requires non class function pointers
    //       but all EdgeImpulse stuff got encapsulated within a class, which does not match
    int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
       return edgeImpulse.microphone_audio_signal_get_data(offset, length, out_ptr);
    }

#endif

/**
 * @brief This bool is used to set the REQUESTED state of the AI inference
 *        The ACTUAL state is reflected by inference->status_running
 * @note Default is to be idle at startup
 */
bool ai_run_enable = false;

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
bool session_folder_created = false;


SDCardSDIO sd_card;

I2SMEMSSampler input;
WAVFileWriter wav_writer;
QueueHandle_t rec_req_evt_queue = nullptr;  // wav recording queue
QueueHandle_t rec_ai_evt_queue = nullptr;   // AI inference queue
TaskHandle_t i2s_TaskHandler = nullptr;     // Task handler from I2S to wav writer
TaskHandle_t ei_TaskHandler = nullptr;      // Task handler from I2S to AI inference TODO: Move to EdgeImpulse.cpp ??

void writeSettings(String settings);
void doDeepSleep();
// void setTime(long epoch, int ms); Now in ESP32Time

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

void testInput()
{
    ESP_LOGV(TAG, "Func: %s", __func__);

    // TODO: This is now in unit tests (test_target_audio_input). Should it be removed?

    for (uint32_t i = 1000; i < 34000; i = i + 2000) {
        i2s_mic_Config.sample_rate = i;
        i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL;

        input.init(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicVolume2_pwr);
        input.install_and_start();
        delay(100);
        ESP_LOGI(TAG, "Clockrate: %f", i2s_get_clk(I2S_NUM_0));
        input.uninstall();
        delay(100);
    }

    delay(100);
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
    if (wav_writer.get_mode() == WAVFileWriter::Mode::disabled) {
        wav_writer.set_mode(WAVFileWriter::Mode::continuous);
    } else {
        wav_writer.set_mode(WAVFileWriter::Mode::disabled);
    }

    /**
     * Any point in send this command via the queue instead?
     */

    // xQueueSendFromISR(rec_req_evt_queue, &mode, (TickType_t)0);
}

/**
 * @brief Move the console/log UART off the APB clock onto REF_TICK so ESP_LOG output stays
 *        readable while DFS is active.
 * @note  The console UART's baud divisor is derived from its clock source. With power
 *        management enabled the APB clock scales down to the configured CPU min frequency
 *        whenever no peripheral holds an APB lock (e.g. the recording-only profile drops it
 *        to ~10 MHz), which turns 115200-baud output into garbage. REF_TICK is a fixed 1 MHz
 *        reference that DFS does not touch — the same fix already used for the GPS UART
 *        (see ElocGPS::init()). Purely cosmetic (no serial console in the field) but keeps
 *        the logs usable for bench debugging in the low-power modes.
 */
static void configureConsoleUartRefTick()
{
#if defined(CONFIG_PM_ENABLE) && defined(CONFIG_ESP_CONSOLE_UART_NUM)
    const uart_port_t consolePort = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_REF_TICK,
    };
    if (esp_err_t err = uart_param_config(consolePort, &uart_config)) {
        ESP_LOGW(TAG, "Failed to switch console UART to REF_TICK: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Console UART%d clock set to REF_TICK (DFS-stable)", CONFIG_ESP_CONSOLE_UART_NUM);
    }
#endif
}

/**
 * @brief Keep the CPU frequency profile in sync with the active recording mode:
 *        any AI detection mode (recordOn_detectOn, recordOff_detectOn, recordOnEvent)
 *        runs at a fixed 240 MHz, recording-only (no AI, no LoRa) at min 10 / max 80 MHz,
 *        otherwise the configured frequencies apply.
 * @note  ElocSystem defers the actual switch while Bluetooth is up (the PLL relock would
 *        crash the BT radio), so this is re-called every main loop iteration — the request
 *        is a no-op once the profile is applied.
 */
static void updatePmProfileFromRecordingMode()
{
    ElocSystem::PmProfile profile = ElocSystem::PmProfile::CONFIG_DEFAULT;
    if (ai_run_enable) {
        profile = ElocSystem::PmProfile::AI_MAX_PERF;
    } else if (wav_writer.get_mode() != WAVFileWriter::Mode::disabled) {
        profile = ElocSystem::PmProfile::RECORDING_LOW_POWER;
    }
    ElocSystem::GetInstance().pm_requestProfile(profile);
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
 * @brief Create session folder on SD card & save config file
*/
bool createSessionFolder()
{
    String fname;
    //TODO: check if another session identifier based on ISO time for mat would be more helpful
    if (getDeviceInfo().fileHeader == "not_set") {
        // Omit
        gSessionIdentifier = timeObject.getSystemTimeMS_string();
    } else {
        gSessionIdentifier = getDeviceInfo().fileHeader + "_" + timeObject.getSystemTimeMS_string();
    }

    fname = String("/sdcard/eloc/") + gSessionIdentifier;
    ESP_LOGI(TAG, "Creating session folder %s", fname.c_str());
    mkdir(fname.c_str(), 0777);

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Starting session with this config:\n: %s", cfg.c_str());
    fname += "/" + gSessionIdentifier + ".config";
    FILE *f = fopen(fname.c_str(), "w+");

    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open config file for storage %s!", fname.c_str());
        return false;
    }

    fprintf(f, "%s", cfg.c_str());
    fclose(f);

    session_folder_created = true;

    // Persist session ID in RTC memory for duty cycle wake continuity
    strncpy(rtc_duty_cycle.sessionId, gSessionIdentifier.c_str(), sizeof(rtc_duty_cycle.sessionId) - 1);
    rtc_duty_cycle.sessionId[sizeof(rtc_duty_cycle.sessionId) - 1] = '\0';
    ESP_LOGI(TAG, "Session ID saved to RTC: %s", rtc_duty_cycle.sessionId);

    return true;
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

bool mountSDCard() {
    ESP_LOGI(TAG, "Trying to mount SDCard (SDIO)");
    sd_card.init("/sdcard");

    if (!sd_card.isMounted())
        return false;

    ESP_LOGI(TAG, "SD card mounted ");
    const char* ELOC_FOLDER = "/sdcard/eloc";

    if (!ffsutil::folderExists(ELOC_FOLDER)) {
        ESP_LOGI(TAG, "%s does not exist, creating empty folder", ELOC_FOLDER);
        mkdir(ELOC_FOLDER, 0777);
    }

    // Populate the cached free-space value now. freeSpaceGB()/checkSDCard() read this cached
    // value, which is otherwise only refreshed by the Bluetooth status task. On a duty-cycle
    // timer wake Bluetooth is skipped, so without this the cache stays 0 and the WAVFileWriter
    // would abort every recording with a false "SD Card is full".
    if (sd_card.update() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SD card free space after mount");
    }

    float freeSpace = sd_card.freeSpaceGB();
    float totalSpace = sd_card.getCapacityMB()/1024;
    ESP_LOGV(TAG, "SD card %f / %f GB free", freeSpace, totalSpace);
    return true;
}

/**
 * @brief Start the wav writer task
 * @attention This function presumes SD card check has already been done
 */
void start_sound_recording() {
    if (session_folder_created == false) {
        session_folder_created = createSessionFolder();
    }

    // Start thread to continuously write to wav file & when sufficient data is collected finish the file
    wav_writer.start_wav_write_task(getConfig().secondsPerFile);
}

#ifdef EDGE_IMPULSE_ENABLED

bool inference_result_file_SD_available = false;
auto save_ai_results_to_sd = true;
auto print_results = -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);

/**
 * @brief Create file to save inference results
 * @attention This function presumes SD card check has already been done
 * @note If the file doesn't exits it will be created with the following details:
 *          EI Project ID, 186372
 *          EI Project owner, EDsteve
 *          EI Project name, trumpet
 *          EI Project deploy version, 2
 * @return 0 on success, -1 on fail
 */
int create_inference_result_file_SD() {
    if (session_folder_created == false) {
        session_folder_created = createSessionFolder();
    }

    ei_results_filename = "/sdcard/eloc/";
    ei_results_filename += gSessionIdentifier;
    ei_results_filename += "/EI-results-ID-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_ID;
    ei_results_filename += "-DEPLOY-VER-";
    ei_results_filename += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
    ei_results_filename += ".csv";

    if (1) {
        ESP_LOGI(TAG, "EI results filename: %s", ei_results_filename.c_str());
    }

    FILE *fp_result = fopen(ei_results_filename.c_str(), "wb");
    if (!fp_result) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    String file_string;

    // Possible other details to include in file
    if (0) {
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
    file_string += "\n\nHour:Min:Sec Day, Month Date Year";

    for (auto i = 0; i < EI_CLASSIFIER_NN_OUTPUT_COUNT; i++) {
        file_string += " ,";
        file_string += edgeImpulse.get_ei_classifier_inferencing_categories(i);
    }

    file_string += "\n";

    fputs(file_string.c_str(), fp_result);
    fclose(fp_result);

    inference_result_file_SD_available = true;

    return 0;
}

/**
 * @brief This function accepts a string, prepends date & time & appends to a csv file
 * @param file_string string in csv format, e.g. 0.94, 0.06
 * @attention This function presumes SD card check has already been done
 * @return 0 on success, -1 on fail
 */
int save_inference_result_SD(String results_string) {
    if (inference_result_file_SD_available == false) {
        if (create_inference_result_file_SD() != 0)
            return -1;
    }

    FILE *fp_result = fopen(ei_results_filename.c_str(), FILE_APPEND);

    if (!fp_result) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return -1;
    }

    String file_string = timeObject.getTimeDate(false) + " " + results_string;

    fputs(file_string.c_str(), fp_result);
    fclose(fp_result);

    return 0;
}

/**
 * @brief This callback allows a thread created in EdgeImpulse to
 *        run the inference. Required due to namespace issues, static implementations etc..
 */
void ei_callback_func() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    // Get inference configuration at function start
    auto inferenceConfig = getInferenceConfig();
    float threshold = inferenceConfig.threshold / 100.0f; // Convert from 0-100 to 0.0-1.0

    if (ai_run_enable == true &&
        edgeImpulse.get_status() == EdgeImpulse::Status::running) {
        
        ESP_LOGV(TAG, "Running inference");
        bool m = edgeImpulse.microphone_inference_record();
        // Blocking function - unblocks when buffer is full
        if (!m) {
            ESP_LOGE(TAG, "ERR: Failed to record audio...");
            // Give up on this inference, come back next time
            return;
        }

        auto startCounter = cpu_hal_get_cycle_count();

        ei::signal_t signal;

        #ifdef AI_CONTINUOUS_INFERENCE
            signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
        #else
            signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        #endif  // AI_CONTINUOUS_INFERENCE

        signal.get_data = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = {};

        #ifdef AI_CONTINUOUS_INFERENCE
            EI_IMPULSE_ERROR r = edgeImpulse.run_classifier_continuous(&signal, &result);
        #else
            EI_IMPULSE_ERROR r = edgeImpulse.run_classifier(&signal, &result);
        #endif  // AI_CONTINUOUS_INFERENCE

        ESP_LOGI(TAG, "Cycles taken to run inference = %d", (cpu_hal_get_cycle_count() - startCounter));

        if (r != EI_IMPULSE_OK) {
            ESP_LOGE(TAG, "ERR: Failed to run classifier (%d)", r);
            // Give up on this inference, come back next time
            return;
        }

        auto target_sound_detected = false;

        #ifdef AI_CONTINUOUS_INFERENCE
            if (++print_results >= (EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW))  // NOLINT
        #else
            // Non-continuous, always print
            if (1)  // NOLINT
        #endif  //  AI_CONTINUOUS_INFERENCE
            {
                ESP_LOGI(TAG, "(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                        result.timing.dsp, result.timing.classification, result.timing.anomaly);

                String file_str;
                String detectedSounds = "";
                ei_impulse_result_classification_t detectedEvents[EI_CLASSIFIER_LABEL_COUNT];
                uint32_t numEventsDetected = 0;

                for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                    ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                    // Build string to save to inference results file
                    file_str += ", ";
                    file_str += result.classification[ix].value;

                    /**
                     * If target sound detected, check against new inference configuration
                     * Note: 'Target' sound is any sound that is not classified as 'background', 'other' or 'others'
                     */
                    
                    if ((strcmp(result.classification[ix].label, "background") != 0) &&
                        (strcmp(result.classification[ix].label, "other") != 0) &&
                        (strcmp(result.classification[ix].label, "others") != 0) &&
                        result.classification[ix].value > threshold) {
                        
                        ESP_LOGI(TAG, "Detection above threshold: %s (%.3f > %.3f)", 
                                result.classification[ix].label, result.classification[ix].value, threshold);
                        
                        // Always save detections above threshold to CSV and start recording (immediate actions)
                        detectedEvents[numEventsDetected] = result.classification[ix];
                        numEventsDetected++;
                        
                        // Start recording immediately for every detection
                        if (wav_writer.wav_recording_in_progress == false &&
                            wav_writer.get_mode() == WAVFileWriter::Mode::single &&
                            sd_card.checkSDCard() == ESP_OK) {
                            start_sound_recording();
                        }
                        
                        // Add detection to window and check if criteria are met for additional actions
                        uint32_t currentTime = timeObject.getEpoch();
                        edgeImpulse.addDetectionToWindow(currentTime);
                        
                        if (edgeImpulse.checkDetectionCriteria(currentTime)) {
                            ESP_LOGI(TAG, "Detection criteria met - triggering additional actions");
                            edgeImpulse.increment_detectedEvents();
                            target_sound_detected = true;
                            
                            // Clear detection window after successful detection
                            edgeImpulse.clearDetectionWindow();
                        }
                    }
                }
                
                // Always update event info for LoRa messaging if any detections occurred
                if (numEventsDetected > 0) {
                    edgeImpulse.updateEventInfo(detectedEvents, numEventsDetected);
                }

                // ESP_LOGI(TAG, "detectedEvents = %d", edgeImpulse.get_detectedEvents());

                file_str += "\n";
                // Save results to CSV if any detection above threshold occurred (not just LoRa criteria)
                if (save_ai_results_to_sd == true &&
                    sd_card.checkSDCard() == ESP_OK &&
                    numEventsDetected > 0) {
                    save_inference_result_SD(file_str);
                }

            #if EI_CLASSIFIER_HAS_ANOMALY == 1
                ESP_LOGI(TAG, "    anomaly score: %f", result.anomaly);
            #endif  // EI_CLASSIFIER_HAS_ANOMALY

            #ifdef AI_CONTINUOUS_INFERENCE
                print_results = 0;
            #endif  // AI_CONTINUOUS_INFERENCE
            }
    }  // ai_run_enable

    ESP_LOGV(TAG, "Inference complete");
}

#endif

/*******************************************************************************
 * Duty-Cycle Deep Sleep Functions
 ******************************************************************************/

/**
 * @brief Determine wake-up cause and set duty cycle state accordingly.
 *        Called FIRST in app_main() before any other initialization.
 *
 * Timer wake → fast boot path (skip BT, Battery, etc.)
 * Button wake or normal boot → normal operation (duty cycle disabled)
 */
void handleWakeUpCause() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wake-up cause: TIMER (duty cycle)");
            gIsTimerWake = true;
            // Validate and increment RTC state
            if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
                rtc_duty_cycle.bootCount++;
                ESP_LOGI(TAG, "Duty cycle boot #%u, total detections: %u",
                    rtc_duty_cycle.bootCount, rtc_duty_cycle.totalDetections);
            } else {
                ESP_LOGW(TAG, "RTC duty cycle state invalid, initializing");
                memset(&rtc_duty_cycle, 0, sizeof(rtc_duty_cycle));
                rtc_duty_cycle.magic = DUTY_CYCLE_RTC_MAGIC;
                rtc_duty_cycle.bootCount = 1;
            }
            gDutyCycleActivationTimeUS = esp_timer_get_time(); // Timer wake: start counting from boot
            gSleepCycleState = SLEEP_CYCLE_INFERENCE_ACTIVE;
            break;

        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "Wake-up cause: BUTTON (EXT0) - normal boot");
            gIsTimerWake = false;
            gSleepCycleState = SLEEP_CYCLE_DISABLED;
            break;

        default:
            ESP_LOGI(TAG, "Wake-up cause: %d (normal boot / power-on)", wakeup_reason);
            gIsTimerWake = false;
            gSleepCycleState = SLEEP_CYCLE_DISABLED;
            // Initialize RTC state on fresh boot if duty cycle is going to be enabled
            // (will be checked after config is loaded)
            break;
    }
}

/**
 * @brief Clean shutdown before entering cyclic deep sleep.
 *        Stops AI inference, stops I2S, saves RTC state, configures timer.
 */
void prepareCyclicDeepSleep() {
    ESP_LOGI(TAG, "Preparing for cyclic deep sleep...");
    gSleepCycleState = SLEEP_CYCLE_PREPARING;

    const dutyCycleConfig_t& dcCfg = getDutyCycleConfig();

    // Sync detection count to RTC before sleep
    #ifdef EDGE_IMPULSE_ENABLED
    {
        uint32_t thisWakeDetections = edgeImpulse.get_detectedEvents();
        rtc_duty_cycle.totalDetections += thisWakeDetections;
        ESP_LOGI(TAG, "This wake detections: %u, total across cycles: %u",
            thisWakeDetections, rtc_duty_cycle.totalDetections);
    }

    // Stop AI inference if running
    if (edgeImpulse.get_status() == EdgeImpulse::Status::running) {
        ESP_LOGI(TAG, "Stopping AI inference for sleep");
        ai_run_enable = false;
        edgeImpulse.set_status(EdgeImpulse::Status::not_running);
        delay(100); // Give inference thread time to stop
    }
    #endif

    // Cleanly close any open WAV file BEFORE stopping I2S. Setting the mode to disabled
    // makes the wav write task call finish() (rewrites the header, fclose) on its next
    // buffer notification — which only arrives while I2S is still running. This is a RAM-only
    // change; rtc_duty_cycle.recordMode keeps the real mode for restore on the next wake.
    if (wav_writer.wav_recording_in_progress) {
        ESP_LOGI(TAG, "Closing WAV file before sleep");
        wav_writer.set_mode(WAVFileWriter::Mode::disabled);
        int waited = 0;
        while (wav_writer.wav_recording_in_progress && waited < 3000) {
            delay(20);
            waited += 20;
        }
    }

    // Stop I2S if running
    if (input.is_i2s_installed_and_started()) {
        ESP_LOGI(TAG, "Stopping I2S for sleep");
        input.uninstall();
    }

    // Save LoRa session before sleep (already persisted in RTC by ElocLora)
    // The LoRaWAN session is saved after each uplink in ElocLora::parseResponse()

    // Turn off LEDs before sleep — IO expander retains register state during deep sleep,
    // so LEDs would stay ON if not explicitly turned off.
    // (The physical LEDs are on the IO expander; GPIO4 is now the GPS UART TX, so it is not driven here.)
    if (ElocSystem::GetInstance().hasIoExpander()) {
        ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_STATUS, false);
        ElocSystem::GetInstance().getIoExpander().setOutputBit(ELOC_IOEXP::LED_BATTERY, false);
        ESP_LOGI(TAG, "LEDs turned off for deep sleep");
    }

    // Configure timer wake-up
    uint64_t sleepTimeUS = static_cast<uint64_t>(dcCfg.sleepDurationS) * 1000000ULL;
    ESP_LOGI(TAG, "Setting deep sleep timer for %u seconds", dcCfg.sleepDurationS);
    esp_sleep_enable_timer_wakeup(sleepTimeUS);

    // Also allow button press to wake (for manual intervention / normal boot)
    // NOTE: a knock (LIS3DH EXT1) wake was tried here and reverted: it boots the full
    // BT+LoRa+GPS path, which together with AI runs out of internal heap (MFE/DSP
    // EIDSP_OUT_OF_MEM), and buzzer vibration false-triggered the knock sensor. Intruder
    // detection is therefore a continuous-operation (24/7) feature only — it is disabled
    // whenever dutyCycle.enable is set (see ElocSystem::notifyStatusRefresh).
    esp_sleep_enable_ext0_wakeup(GPIO_BUTTON, 0);

    ESP_LOGI(TAG, "Boot #%u complete. Total detections across cycles: %u. Entering sleep...",
        rtc_duty_cycle.bootCount, rtc_duty_cycle.totalDetections);
}

/**
 * @brief Enter deep sleep. Does not return.
 */
void enterCyclicDeepSleep() {
    gSleepCycleState = SLEEP_CYCLE_ENTERING_SLEEP;

#ifdef USE_GPS
    // Power the GPS down before sleeping: stops its task, removes the UART driver, drives the TX line
    // low (kills the RXD ESD-clamp back-feed) and switches the VCC MOSFET off (IO5 high). Without this
    // the module would stay powered through deep sleep, wasting active current.
    ElocGPS::GetInstance().deinit();

    // deinit() is a no-op when the GPS was never initialized this cycle — and on a TIMER WAKE it never
    // is (GPS init lives in the !gIsTimerWake block). So force the VCC rail off unconditionally here,
    // independent of ElocGPS state, to guarantee the MOSFET is off in sleep on every boot path.
    if (ElocSystem::GetInstance().hasIoExpander()) {
        ElocSystem::GetInstance().getIoExpander().setGpsPower(false);  // IO5 high -> MOSFET off
    }
#endif

    ESP_LOGI(TAG, "Entering deep sleep NOW");
    delay(50); // Allow log to flush
    esp_deep_sleep_start();
    // Never reached
}

/**
 * @brief Called each main loop iteration to manage the duty cycle state machine.
 *        Tracks awake duration and triggers sleep when time is up.
 */
void handleSleepCycleStateMachine() {
    if (gSleepCycleState != SLEEP_CYCLE_INFERENCE_ACTIVE) {
        return; // Not in duty cycle mode or already preparing to sleep
    }

    const dutyCycleConfig_t& dcCfg = getDutyCycleConfig();
    // Calculate how long since duty cycle was activated (not since boot)
    int64_t awakeTimeS = (esp_timer_get_time() - gDutyCycleActivationTimeUS) / 1000000LL;

    if (awakeTimeS >= static_cast<int64_t>(dcCfg.awakeDurationS)) {
        ESP_LOGI(TAG, "Awake duration (%lld s) reached limit (%u s), preparing to sleep",
            awakeTimeS, dcCfg.awakeDurationS);

        // Run LoRa loop one final time to send any pending messages
        ElocLora::GetInstance().ElocLoraLoop();

        prepareCyclicDeepSleep();
        enterCyclicDeepSleep();
        // Never returns
    }
}

/*******************************************************************************
 * End Duty-Cycle Functions
 ******************************************************************************/

#ifdef USE_GPS
/**
 * @brief Derive the local-display timezone from the current GPS longitude and apply it, unless the
 *        user has already set one from the app.
 *
 * UTC is always the source of truth: the RTC and every transmitted/stored epoch stay UTC. This only
 * shifts the local wall-clock used for human-readable timestamps and WAV/CSV filenames so a device
 * dropped in a new country self-localises without any field configuration.
 *
 * Precedence (highest first): app-set TZ persisted in RTC > this GPS-longitude offset > the
 * compile-time TIMEZONE_OFFSET already applied at boot. The derived value is the solar/meridian
 * offset round(longitude / 15); it deliberately ignores political borders and DST, which is fine for
 * the equatorial deployment sites (and the app override exists for the cases where it isn't).
 *
 * @return true once a GPS-derived offset has been applied (or an app override makes it moot — i.e.
 *         the caller can stop retrying); false while there is no fix yet.
 */
static bool applyGpsDerivedTimezone() {
    // App-set TZ wins — never override an explicit field-tech choice. Treat this as "done" so the
    // caller stops polling.
    if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC && rtc_duty_cycle.timezoneOffsetValid) {
        return true;
    }
    if (!ElocGPS::GetInstance().hasFix()) {
        return false;  // no position yet — try again later
    }

    double lng = ElocGPS::GetInstance().getLng();
    // Round longitude/15 to the nearest hour, away from zero. Longitude is bounded to +/-180, so the
    // result is always within setTimeZone()'s valid -12..+14 range.
    int offset = static_cast<int>((lng >= 0.0) ? (lng / 15.0 + 0.5) : (lng / 15.0 - 0.5));
    timeObject.setTimeZone(offset);

    // Persist the derived offset so a duty-cycle wake that skips GPS (clock still fresh) can restore
    // the right local TZ without a fix. App-set TZ still wins — it is checked first everywhere.
    if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
        rtc_duty_cycle.gpsTimezoneOffset = static_cast<int8_t>(offset);
        rtc_duty_cycle.gpsTimezoneValid  = true;
    }

    ESP_LOGI(TAG, "Timezone derived from GPS longitude %.4f -> UTC%+d (local %s)",
             lng, offset, timeObject.getDateTime().c_str());
    return true;
}

/**
 * @brief Called every main-loop iteration while the device is awake. Keeps the GPS-derived timezone
 *        and last-sync timestamp current, and — when GPS_RESYNC_INTERVAL_S > 0 — runs the GPS module
 *        in low-power BURSTS instead of leaving it powered.
 *
 * The ATGM336H draws nearly as much as the ESP32 itself at a 16 kHz sample rate, so for continuous
 * (non-duty-cycled) recording it is wasteful to keep it on. Instead we power it up only long enough to
 * grab one fresh UTC fix, then gate it off until GPS_RESYNC_INTERVAL_S has elapsed. VBAT keeps the
 * module's clock + almanac alive between bursts, so each re-acquisition is a fast warm start, and the
 * ESP32 RTC carries accurate time across the off period (its drift over an hour is negligible for
 * timestamping). Set GPS_RESYNC_INTERVAL_S to 0 to keep the GPS powered continuously (old behaviour).
 *
 * GPS here only TRIMS clock drift — it is not the device's only time source. The authoritative time is
 * set by the Android app (setTime over Bluetooth) at commissioning and held by the RTC, so a burst that
 * fails to get a fix (e.g. poor sky view) is harmless: we keep the current clock and simply retry next
 * interval. That is why there is no "block until the first fix" path here.
 *
 * The cadence uses the monotonic esp_timer (which restarts each boot), so it is independent of the
 * duty-cycle RTC state and behaves identically with or without an app setTime / duty-cycle config. It
 * cooperates with the boot-path GPS handling: a freshly-booted burst started there is "adopted" here,
 * and on a duty-cycle timer wake that deliberately skipped GPS the off-state gate stays closed for the
 * (short) awake window, so GPS is not re-powered.
 *
 * @param gpsTzApplied loop-scoped one-shot flag (see applyGpsDerivedTimezone); updated in place.
 */
static void manageGpsWhileAwake(bool& gpsTzApplied) {
    ElocGPS& gps = ElocGPS::GetInstance();

    // (1) Catch a fix that lands in the background: refresh the local TZ once and keep the persisted
    //     last-sync timestamp current (covers both the burst syncs below and continuous-mode drift
    //     re-syncs). lastUtcEpoch() survives a power-down, so this keeps persisting harmlessly.
    if (!gpsTzApplied) {
        gpsTzApplied = applyGpsDerivedTimezone();
    }
    const int64_t gpsEpoch = gps.lastUtcEpoch();
    if (gpsEpoch > 0 && rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
        rtc_duty_cycle.lastGpsSyncS = gpsEpoch;
        static int64_t prevStampedGpsEpoch = 0;
        if (gpsEpoch != prevStampedGpsEpoch) {           // a NEW GPS clock write happened
            rtc_duty_cycle.clockSource = CLOCK_SRC_GPS;  // last-writer-wins vs cmd_SetTime
            prevStampedGpsEpoch = gpsEpoch;
        }
        // Anchor the deployment-uptime clock if GPS is the first to supply a real time this
        // deployment (mirrors the app path in cmd_SetTime). See printStatus() Uptime[h].
        if (rtc_duty_cycle.firstBootEpochS == 0) {
            rtc_duty_cycle.firstBootEpochS = gpsEpoch;
        }
    }

    // Keep ElocLora's copy of the position current (used by the intruder alarm uplink).
    // Pushed rather than pulled: lib/gps already depends on lib/ElocHardware, so ElocLora
    // cannot include ElocGPS without creating an LDF dependency cycle.
    ElocLora::GetInstance().setGpsInfo(gps.hasFix(), gps.getLat(), gps.getLng(),
                                       gps.getFixAgeMs() / 1000);

    // Intruder alarm active: hold the GPS powered continuously so the LoRa alarm uplinks carry a
    // live, refreshing position while the device is being moved/carried away. Overrides the burst
    // power management below; when the alarm clears, the normal burst cadence resumes (a still-
    // powered GPS is adopted by the burst logic like a boot-path burst).
    if (ElocSystem::GetInstance().isIntruderDetected()) {
        if (!gps.isInitialized()) {
            ESP_LOGW(TAG, "Intruder alarm: powering GPS on for location tracking");
            gps.init();
        }
        return;
    }

    // BT client connected: hold GPS powered so getStatus carries live fix/HDOP for the app. On
    // disconnect the still-powered GPS is adopted by the burst logic below (powerOnUs==0 path), which
    // then powers it down at the end of the current burst. Unlike the intruder block above we throttle
    // the (re)init attempts — a repeatedly failing power-up must not busy-spin gps.init() every loop.
    if (ElocSystem::GetInstance().isBtClientConnected()) {
        if (!gps.isInitialized()) {
            static int64_t lastInitAttemptUs = 0;
            if (esp_timer_get_time() - lastInitAttemptUs > 10 * 1000000LL) {  // failed-init backoff
                ESP_LOGI(TAG, "BT client connected: powering GPS on for live status");
                lastInitAttemptUs = esp_timer_get_time();
                gps.init();
            }
        }
        return;
    }

    // (2) Burst power management — skipped entirely (GPS left powered) when the interval is disabled.
    if (GPS_RESYNC_INTERVAL_S <= 0) {
        return;
    }

    static int64_t powerOnUs    = 0;  // esp_timer time the current burst powered on (0 = GPS off/idle)
    static int64_t nextAttemptUs = 0; // esp_timer time the next burst may begin (0 = ASAP)
    static bool    seeded       = false;
    const int64_t nowUs      = esp_timer_get_time();
    const int64_t intervalUs = static_cast<int64_t>(GPS_RESYNC_INTERVAL_S) * 1000000LL;

    // One-time seed: if GPS is already off at the first entry, the boot path chose to skip it because
    // the RTC clock is still fresh from a recent GPS sync (a duty-cycle timer wake). Honour that across
    // into the awake cadence by deferring the first burst until the remaining freshness expires, rather
    // than re-powering GPS immediately. (Only the duty-cycle path sets these RTC fields; in the 24/7
    // case GPS is already powered here, so this branch is skipped and the cadence is purely monotonic.)
    if (!seeded) {
        seeded = true;
        if (!gps.isInitialized() && rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC &&
            rtc_duty_cycle.lastGpsSyncS > 0) {
            const int64_t ageS = timeObject.getEpoch() - rtc_duty_cycle.lastGpsSyncS;
            const int64_t remainingS = static_cast<int64_t>(GPS_RESYNC_INTERVAL_S) - ageS;
            if (remainingS > 0) {
                nextAttemptUs = nowUs + remainingS * 1000000LL;
            }
        }
    }

    if (gps.isInitialized()) {
        // Adopt a burst powered on elsewhere (the boot path) so its acquisition clock starts here.
        if (powerOnUs == 0) {
            powerOnUs = nowUs;
        }
        // Be patient on the FIRST fix only while the device still has no real clock (the RTC is at
        // firmware build-time). Once the app's setTime or any GPS fix has advanced the clock past
        // build-time, revert to the short trim ceiling. The +1 h margin covers the uptime that accrues
        // during a patient burst (the clock ticks up from build-time until it is actually set).
        const bool clockUnset = timeObject.getEpoch() < (timeObject.getBuildTimeSecs() + 3600);
        const int effectiveTimeoutS = clockUnset ? GPS_FIRST_FIX_TIMEOUT_S : GPS_TIME_SYNC_TIMEOUT_S;
        // End the burst on a live POSITION fix, not merely a time sync. UTC time is decoded a few
        // seconds before a full position solution, and the local timezone is derived from the fix
        // LONGITUDE — powering off on time alone leaves the TZ at its compile-time default. A fix
        // lingers as "valid" in the parser after a previous burst's power-down, so require a recent
        // update (< 3 s) to confirm it belongs to THIS burst. The clock itself is synced in the
        // background as soon as time is valid, so we never lose the time even if no fix follows.
        const bool liveFix = gps.hasLiveFix();  // single source of truth (see ElocGPS::hasLiveFix)
        const bool acquireTimedOut =
            (nowUs - powerOnUs) > static_cast<int64_t>(effectiveTimeoutS) * 1000000LL;
        if (liveFix || acquireTimedOut) {
            if (liveFix) {
                gpsTzApplied = applyGpsDerivedTimezone();  // longitude available -> derive local TZ
                ESP_LOGI(TAG, "GPS burst: fix acquired (epoch=%ld), powering GPS off for ~%d s",
                         static_cast<long>(gps.lastUtcEpoch()), GPS_RESYNC_INTERVAL_S);
            } else {
                ESP_LOGW(TAG, "GPS burst: no position fix within %d s, keeping clock; retry in ~%d s",
                         effectiveTimeoutS, GPS_RESYNC_INTERVAL_S);
            }
            gps.deinit();
            powerOnUs = 0;
            nextAttemptUs = nowUs + intervalUs;  // same cadence whether the burst succeeded or not
        }
    } else if (nowUs >= nextAttemptUs) {
        // GPS off and the interval has elapsed: power up for the next burst. On a fresh-clock duty-cycle
        // wake the boot path leaves GPS off and nowUs is small (esp_timer restarts each boot), so this
        // gate stays closed for the short awake window — GPS is correctly not re-powered that cycle.
        ESP_LOGI(TAG, "GPS burst: powering GPS on for time resync");
        if (gps.init() == ESP_OK) {
            powerOnUs = nowUs;
        } else {
            nextAttemptUs = nowUs + intervalUs;  // back off before retrying a failed power-up
        }
    }
}
#endif  // USE_GPS

void app_main(void) {
    ESP_LOGI(TAG, "\nSETUP--start\n");
    initArduino();
    ESP_LOGI(TAG, "initArduino done");

    // Determine wake-up cause FIRST — sets gIsTimerWake and gSleepCycleState
    handleWakeUpCause();

    printPartitionInfo();  // So if reboots, always boot into the bluetooth partition


#ifdef EDGE_IMPULSE_ENABLED
    ESP_LOGI(TAG, "Edge Impulse framework enabled");

    #ifdef AI_CONTINUOUS_INFERENCE
        ESP_LOGI(TAG, "Continuous inference enabled");
    #else
        ESP_LOGI(TAG, "Continuous inference disabled");
    #endif

#endif

    // On timer wake (duty cycle), the RTC hardware maintains accurate time during deep sleep.
    // Only set build time on fresh boot — on timer wake, just set the internal reference
    // without calling settimeofday() which would reset the correct RTC time.
    if (gIsTimerWake) {
        // RTC time is already correct from before sleep. Just set the build_time reference
        // so getUpTimeSecs() and other functions work correctly.
        timeObject.setBuildTimeOnly(__TIME_UNIX__);

        // Prefer the user's BT-set TZ persisted in RTC over the compile-time default.
        // Without this, CSV timestamps drift to TIMEZONE_OFFSET (e.g. +7) after the
        // first duty-cycle wake even if the Android app already set the device to +1.
        // TZ precedence: app-set (BT) > GPS-longitude-derived > compile-time default. Both persisted
        // variants survive deep sleep in RTC; the libc TZ env var itself does not, so it is re-applied
        // here every wake.
        int32_t tz = TIMEZONE_OFFSET;
        if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC &&
            rtc_duty_cycle.timezoneOffsetValid) {
            tz = rtc_duty_cycle.timezoneOffset;
            ESP_LOGI(TAG, "Timer wake: restoring user-set TZ offset %+ld from RTC", tz);
        } else if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC &&
                   rtc_duty_cycle.gpsTimezoneValid) {
            tz = rtc_duty_cycle.gpsTimezoneOffset;
            ESP_LOGI(TAG, "Timer wake: restoring GPS-derived TZ offset %+ld from RTC", tz);
        } else {
            ESP_LOGW(TAG, "Timer wake: no persisted TZ, falling back to compile-time %+d",
                     TIMEZONE_OFFSET);
        }
        timeObject.setTimeZone(tz);
        ESP_LOGI(TAG, "Timer wake: preserving RTC time (epoch=%ld)", timeObject.getEpoch());
    } else {
        timeObject.initBuildTime(__TIME_UNIX__, TIMEZONE_OFFSET);
    }

    printRevision();

    resetPeripherals();

    // NOTE: STATUS_LED/BATTERY_LED are driven via the PCA9557 IO expander on ELOC 3.0.
    // GPIO4 (the legacy LED pin define) is reused as the GPS UART TX, so it is not configured here.

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

    // Install GPIO ISR service early, before LoRa or other interrupt sources
    // This needs to be done before any gpio_isr_handler_add calls
    esp_err_t isr_err = gpio_install_isr_service(GPIO_INTR_PRIO);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_err));
        // Continue anyway, but interrupts may not work
    } else if (isr_err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "GPIO ISR service already installed");
    } else {
        ESP_LOGI(TAG, "GPIO ISR service installed successfully");
    }

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

    // Validate duty cycle config after loading
    validateDutyCycleConfig();

    // Move the console UART onto REF_TICK before any DFS frequency drop can garble it
    // (the low-power recording profile scales the APB clock down to ~10 MHz).
    configureConsoleUartRefTick();

    /** Setup Power Management.
     *  This must run BEFORE LoRa/Bluetooth start: switching to a CPU frequency with a
     *  different PLL (240 MHz needs the 480 MHz PLL, 80/160 the 320 MHz PLL, and the chip
     *  boots at CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ) relocks the BBPLL, which the BT radio
     *  is clocked from — doing that with BT running causes crashes/WDT resets.
     *
     *  On a duty-cycle timer wake the recording mode from before deep sleep is restored
     *  further down (wav mode + AI enable from RTC memory). Select the matching CPU
     *  frequency profile already here so it is applied while the radios are still down:
     *  AI modes run at a fixed 240 MHz, recording-only at min 10 / max 80 MHz. */
    bool pmProfileApplied = false;
    if (gIsTimerWake && getDutyCycleConfig().enable && rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
        if (rtc_duty_cycle.aiEnabled) {
            pmProfileApplied = (ElocSystem::GetInstance().pm_requestProfile(
                                    ElocSystem::PmProfile::AI_MAX_PERF) == ESP_OK);
        } else if (static_cast<WAVFileWriter::Mode>(rtc_duty_cycle.recordMode) !=
                   WAVFileWriter::Mode::disabled) {
            pmProfileApplied = (ElocSystem::GetInstance().pm_requestProfile(
                                    ElocSystem::PmProfile::RECORDING_LOW_POWER) == ESP_OK);
        }
    }
    if (!pmProfileApplied) {
        ElocSystem::GetInstance().pm_configure();
    }

    // On fresh boot with duty cycle enabled, initialize RTC state but do NOT
    // activate the sleep cycle. Duty cycle only activates when the user
    // explicitly enters recordOFF_detectON mode via BT command.
    if (!gIsTimerWake && getDutyCycleConfig().enable) {
        ESP_LOGI(TAG, "Fresh boot with duty cycle enabled - initializing RTC state (duty cycle will activate on recordOFF_detectON)");
        memset(&rtc_duty_cycle, 0, sizeof(rtc_duty_cycle));
        rtc_duty_cycle.magic = DUTY_CYCLE_RTC_MAGIC;
        rtc_duty_cycle.bootCount = 0;
        // gSleepCycleState remains SLEEP_CYCLE_DISABLED until user starts recordOFF_detectON mode
    }

    // On timer wake, restore session ID from RTC so all duty cycle wakes
    // share the same session folder and append to the same CSV file.
    if (gIsTimerWake && rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC
        && rtc_duty_cycle.sessionId[0] != '\0') {
        gSessionIdentifier = String(rtc_duty_cycle.sessionId);
        session_folder_created = true;  // Folder already exists from initial session
        ESP_LOGI(TAG, "Timer wake: restored session ID from RTC: %s", gSessionIdentifier.c_str());

        #ifdef EDGE_IMPULSE_ENABLED
        // Build the CSV filename so save_inference_result_SD() can append directly
        // without calling create_inference_result_file_SD() which would overwrite with headers
        ei_results_filename = "/sdcard/eloc/";
        ei_results_filename += gSessionIdentifier;
        ei_results_filename += "/EI-results-ID-";
        ei_results_filename += EI_CLASSIFIER_PROJECT_ID;
        ei_results_filename += "-DEPLOY-VER-";
        ei_results_filename += EI_CLASSIFIER_PROJECT_DEPLOY_VERSION;
        ei_results_filename += ".csv";
        inference_result_file_SD_available = true;  // Skip header re-creation, just append
        ESP_LOGI(TAG, "Timer wake: will append to existing CSV: %s", ei_results_filename.c_str());
        #endif
    }

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

    // Skip heavy subsystems on timer wake (duty cycle fast boot path)
    if (!gIsTimerWake) {
        ESP_LOGI(TAG, "Setting up Battery...");
        Battery::GetInstance();

        // check if a firmware update is triggered via SD card
        checkForFirmwareUpdateFile();
    } else {
        ESP_LOGI(TAG, "Timer wake: skipping Battery and FirmwareUpdate");
    }

    // Queue for recording requests
    rec_req_evt_queue = xQueueCreate(10, sizeof(rec_req_t));
    xQueueReset(rec_req_evt_queue);

    // Queue for AI requests
    rec_ai_evt_queue = xQueueCreate(10, sizeof(bool));
    xQueueReset(rec_ai_evt_queue);


    ESP_LOGI(TAG, "Setup LoraWAN");
    ElocLora::GetInstance();
    // GPIO ISR service is now installed earlier in the initialization sequence

    // Classic-BT-only build (CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY): return the never-used BLE
    // share of the BT controller DRAM reserve to the internal heap. Must run before
    // esp_bt_controller_init() (inside SerialBT.begin()); irreversible until reboot, which is
    // fine on both boot paths since BLE is never used. Frees ~30 KB internal DRAM that the
    // EI DSP (MFE) and Bluedroid SDP buffers otherwise fight over.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    ESP_LOGI(TAG, "Released BLE controller memory, free internal heap now %u bytes",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Skip Bluetooth, PerfMonitor, UART test on timer wake
    if (!gIsTimerWake) {
        ESP_LOGI(TAG, "Creating Bluetooth  task...");
        if (esp_err_t err = BluetoothServerSetup(false)) {
            ESP_LOGI(TAG, "BluetoothServerSetup failed with %s", esp_err_to_name(err));
        }

        // Post-boot self-test passed (SD mounted, config loaded, BT server started):
        // confirm this image so the bootloader's OTA rollback doesn't revert it.
        // The first boot after an update is always a full boot (esp_restart), never
        // a timer wake, so this path is guaranteed to run after every update.
        markRunningFirmwareValid();

    #ifdef USE_PERF_MONITOR
        ESP_LOGI(TAG, "Creating Performance Monitor task...");
        if (esp_err_t err = PerfMonitor::setup()) {
            ESP_LOGI(TAG, "Performance Monitor failed with %s", esp_err_to_name(err));
        }
    #endif

    #ifdef ENABLE_TEST_UART
        ESP_LOGI(TAG, "Creating UART task...");
        uart_eloc::UART_ELOC uart_test;
        uart_test.init(UART_NUM_0);
        uart_test.start_thread();
    #endif
    } else {
        ESP_LOGI(TAG, "Timer wake: skipping Bluetooth, PerfMonitor, UART");
    }

#ifdef USE_GPS
    // GPS runs on every boot path so the background task can correct the clock from GPS UTC and derive
    // the local timezone — EXCEPT a duty-cycle timer wake whose clock is still fresh from a recent GPS
    // sync. The ESP32 RTC keeps time through deep sleep and the GPS module's VBAT-backed clock stays
    // alive, so re-acquiring every short cycle is wasted energy; we skip powering the GPS until the
    // last sync is older than GPS_RESYNC_INTERVAL_S. When we do run it on a timer wake, we additionally
    // block briefly for the first fix so this cycle's detection / LoRa / CSV timestamps are accurate.
    // The wait sits before the awake-duration timer is reset (below), so it does not shorten the
    // inference window — but it does keep the device awake longer; see GPS_TIME_SYNC_TIMEOUT_S.
    bool gpsTzApplied = false;

    // Is the RTC clock still fresh enough that this wake can skip GPS entirely?
    bool gpsClockFresh = false;
    if (gIsTimerWake && GPS_RESYNC_INTERVAL_S > 0 &&
        rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC && rtc_duty_cycle.lastGpsSyncS > 0) {
        int64_t ageS = static_cast<int64_t>(timeObject.getEpoch()) - rtc_duty_cycle.lastGpsSyncS;
        if (ageS >= 0 && ageS < GPS_RESYNC_INTERVAL_S) {
            gpsClockFresh = true;
            gpsTzApplied = true;  // TZ already restored from RTC at boot; no fix needed this wake
            ESP_LOGI(TAG, "Timer wake: GPS time still fresh (synced %lld s ago, < %d s) — skipping GPS",
                     ageS, GPS_RESYNC_INTERVAL_S);
        }
    }

    if (!gpsClockFresh) {
        ESP_LOGI(TAG, "Setting up GPS...");
        ElocGPS::GetInstance().init();

        if (gIsTimerWake && GPS_TIME_SYNC_TIMEOUT_S > 0) {
            ESP_LOGI(TAG, "Timer wake: waiting up to %d s for GPS time sync (RTC epoch=%ld)...",
                     GPS_TIME_SYNC_TIMEOUT_S, timeObject.getEpoch());
            esp_err_t tsync = ElocGPS::GetInstance().waitForTimeSync(GPS_TIME_SYNC_TIMEOUT_S * 1000);
            if (tsync == ESP_OK) {
                ESP_LOGI(TAG, "Timer wake: GPS time acquired, RTC corrected (epoch=%ld)",
                         timeObject.getEpoch());
                gpsTzApplied = applyGpsDerivedTimezone();
                if (rtc_duty_cycle.magic == DUTY_CYCLE_RTC_MAGIC) {
                    rtc_duty_cycle.lastGpsSyncS = ElocGPS::GetInstance().lastUtcEpoch();
                }
            } else {
                ESP_LOGW(TAG, "Timer wake: GPS time sync did not complete (%s), keeping RTC time (epoch=%ld)",
                         esp_err_to_name(tsync), timeObject.getEpoch());
            }
        }
    }
#endif

#ifdef EDGE_IMPULSE_ENABLED

    auto s = edgeImpulse.get_aiModel();
    ESP_LOGI(TAG, "Edge impulse model version: %s", s.c_str());
    edgeImpulse.output_inferencing_settings();

    if (0) {
        edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        // TODO: This test now moved to unit test (test_target_edge-impulse) - could remove

        // Run stored audio samples through the model to test it
        // Use continuous process for this
        ESP_LOGI(TAG, "Testing model against pre-recorded sample data...");

        static_assert((EI_CLASSIFIER_RAW_SAMPLE_COUNT <= TEST_SAMPLE_LENGTH),
                       "TEST_SAMPLE_LENGTH must be at least equal to EI_CLASSIFIER_RAW_SAMPLE_COUNT");

        if (EI_CLASSIFIER_RAW_SAMPLE_COUNT < TEST_SAMPLE_LENGTH) {
            ESP_LOGI(TAG, "TEST_SAMPLE length is greater than the Edge Impulse model length, applying down-sampling");
        }

        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        signal.get_data = &microphone_audio_signal_get_data;
        ei_impulse_result_t result = {};

        // Artificially fill buffer with test data
        auto ei_skip_rate = TEST_SAMPLE_LENGTH / EI_CLASSIFIER_RAW_SAMPLE_COUNT;
        auto skip_current = ei_skip_rate;   // Make sure to fill first sample, then start skipping if needed

        for (auto i = 0; i < test_array_size; i++) {
            ESP_LOGI(TAG, "Running test category: %s", test_array_categories[i]);

            for (auto test_sample_count = 0, inference_buffer_count = 0; (test_sample_count < TEST_SAMPLE_LENGTH) &&
                    (inference_buffer_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT); test_sample_count++) {
                if (skip_current >= ei_skip_rate) {
                    edgeImpulse.inference.buffers[0][inference_buffer_count++] = test_array[i][test_sample_count];
                    skip_current = 1;
                } else {
                    skip_current++;
                }
            }

            // Mark buffer as ready
            // Mark active buffer as inference.buffers[1], inference run on inactive buffer
            edgeImpulse.inference.buf_select = 1;
            edgeImpulse.inference.buf_count = 0;
            edgeImpulse.inference.buf_ready = 1;

            EI_IMPULSE_ERROR r = edgeImpulse.run_classifier(&signal, &result);
            if (r != EI_IMPULSE_OK) {
                ESP_LOGW(TAG, "ERR: Failed to run classifier (%d)", r);
                return;
            }

            // print the predictions
            ESP_LOGI(TAG, "Test model predictions:");
            ESP_LOGI(TAG, "    (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
                    result.timing.dsp, result.timing.classification, result.timing.anomaly);
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                ESP_LOGI(TAG, "    %s: %f", result.classification[ix].label, result.classification[ix].value);

                if (strcmp(result.classification[ix].label, "trumpet") == 0 &&
                    strcmp(test_array_categories[i], "trumpet") == 0) {
                    if (result.classification[ix].value < AI_RESULT_THRESHOLD) {
                        ESP_LOGW(TAG, "Test of trumpet sample appears to be poor, check model!");
                    }
                }
            }
        }

        // Free buffers as the buffer size for continuous & non-continuous differs
        edgeImpulse.free_buffers();
    }

    #ifdef AI_CONTINUOUS_INFERENCE
        edgeImpulse.buffers_setup(EI_CLASSIFIER_SLICE_SIZE);
    #else
        edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
    #endif  // AI_CONTINUOUS_INFERENCE

    #endif  // EDGE_IMPULSE_ENABLED

    // setup button as interrupt
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)GPIO_BUTTON));
    // ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON, buttonISR, (void *)OTHER_GPIO_BUTTON));

    // Power management is configured early in setup() (right after readConfig), before
    // LoRa/Bluetooth start — see the comment there for why the order matters.

    if (getConfig().testI2SClockInput)
        testInput();

    input.init(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicVolume2_pwr);

    if (sd_card.checkSDCard() == ESP_OK) {
        // create a new wave file wav_writer & make sure sample rate is up to date
        if (wav_writer.initialize(i2s_mic_Config.sample_rate, 2, NUMBER_OF_MIC_CHANNELS) != true) {
            ESP_LOGE(TAG, "Failed to initialize WAVFileWriter");
        }

        // Block until properly registered otherwise will get error later
        while (input.register_wavFileWriter(&wav_writer) == false) {
            ESP_LOGW(TAG, "Waiting for WAVFileWriter to register");
            delay(5);
        }

        // On a duty-cycle timer wake, restore the recording mode that was active before sleep.
        // wav_writer.mode is plain RAM and defaults to disabled on every boot, so without this
        // the main loop's auto-start (mode==continuous) would never resume recording after a wake.
        if (gIsTimerWake && getDutyCycleConfig().enable) {
            wav_writer.set_mode(static_cast<WAVFileWriter::Mode>(rtc_duty_cycle.recordMode));
            ESP_LOGI(TAG, "Timer wake: restored wav mode = %s", wav_writer.get_mode_str());
        }
    } else {
        ESP_LOGE(TAG, "SD card not mounted, cannot create WAVFileWriter");
            wav_writer.set_mode(WAVFileWriter::Mode::disabled);  // Default is disabled anyway
        #ifdef EDGE_IMPULSE_ENABLED
            save_ai_results_to_sd = false;
        #endif
    }

    #ifdef EDGE_IMPULSE_ENABLED
        #ifdef AI_CONTINUOUS_INFERENCE
            // Init static vars
            edgeImpulse.run_classifier_init();
            edgeImpulse.buffers_setup(EI_CLASSIFIER_SLICE_SIZE);
        #else
            edgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        #endif  // AI_CONTINUOUS_INFERENCE

        input.register_ei_inference(&edgeImpulse.getInference(), EI_CLASSIFIER_FREQUENCY);
        // edgeImpulse.set_status(EdgeImpulse::Status::running);
        // edgeImpulse.start_ei_thread(ei_callback_func);

        // Auto-start AI on timer wake (duty cycle mode), but only if AI was enabled before
        // sleep. recordOn_detectOff duty-cycles audio recording with no AI, so don't spin it up.
        if (gIsTimerWake && getDutyCycleConfig().enable && rtc_duty_cycle.aiEnabled) {
            ESP_LOGI(TAG, "Duty cycle timer wake: auto-starting AI inference");
            ai_run_enable = true;
            bool enable = true;
            xQueueSend(rec_ai_evt_queue, &enable, (TickType_t)0);
        }
    #endif

    // For timer wake: reset the awake duration timer AFTER all initialization completes
    // so the full awakeDurationS is available for actual inference, not eaten by boot overhead
    if (gIsTimerWake) {
        gDutyCycleActivationTimeUS = esp_timer_get_time();
        ESP_LOGI(TAG, "Timer wake: awake timer reset after init (boot took %lld ms)",
            gDutyCycleActivationTimeUS / 1000);
    }

    auto loopCnt = 0;

    // This might be redundant, set directly in ElocCommands.cpp
    auto new_mode =  WAVFileWriter::Mode::disabled;

    while (true) {
        if (xQueueReceive(rec_req_evt_queue, &new_mode, pdMS_TO_TICKS(500))) {
            ESP_LOGI(TAG, "Received new wav writer mode");

            if (new_mode == WAVFileWriter::Mode::continuous) {
                ESP_LOGI(TAG, "wav writer mode = continuous");
                wav_writer.set_mode(new_mode);
            } else if (new_mode == WAVFileWriter::Mode::single) {
                ESP_LOGI(TAG, "wav writer mode = single");
                wav_writer.set_mode(new_mode);
            } else if (new_mode == WAVFileWriter::Mode::disabled) {
                ESP_LOGI(TAG, "wav writer mode = disabled");
                wav_writer.set_mode(new_mode);
            } else {
                ESP_LOGE(TAG, "wav writer mode = unknown");
            }
        }

        if ((loopCnt++ % 10) == 0) {
            Battery::GetInstance().updateVoltage();  // only updates actual as often as set in the config
            ESP_LOGI(TAG, "Battery: Voltage: %.3fV, %.0f%% SoC, Temp %d °C",
            Battery::GetInstance().getVoltage(), Battery::GetInstance().getSoC(), ElocSystem::GetInstance().getTemperaure());
            ESP_LOGI(TAG, "CPU clock: %d MHz", esp_clk_cpu_freq()/ 1000000);

            // Display memory usage
#ifdef ENABLE_HEAP_MONITOR
            {
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
#endif  // ENABLE_HEAP_MONITOR

            // Compare time sources
            if (0) {
                ESP_LOGI(TAG, "FreeRTOS xTaskGetTickCount = %u", xTaskGetTickCount());
                ESP_LOGI(TAG, "esp_timer_get_time = %lld", esp_timer_get_time());
                ESP_LOGI(TAG, "esp32Time.getUpTimeSecs = %llu", timeObject.getUpTimeSecs());
                ESP_LOGI(TAG, "esp32Time.getEpoch = %ld", timeObject.getEpoch());
                ESP_LOGI(TAG, "esp32Time.getTime = %s", timeObject.getTime().c_str());
            }
        }

        // Keep the CPU frequency in sync with the recording mode (AI = 240 MHz,
        // recording-only = 10/80 MHz). Deferred internally while Bluetooth is up.
        updatePmProfileFromRecordingMode();

        // Need to start I2S?
        // Note: Once started continues to run..
        if ((wav_writer.get_mode() != WAVFileWriter::Mode::disabled || ai_run_enable != false) &&
            input.is_i2s_installed_and_started() == false) {
            // Keep trying until successful
            if (input.install_and_start() == ESP_OK) {
                delay(300);
                input.zero_dma_buffer(I2S_DEFAULT_PORT);
                input.start_read_task(sample_buffer_size/ sizeof(signed short));
            }
        }

        // Start a new recording?
        if (wav_writer.wav_recording_in_progress == false &&
            wav_writer.get_mode() == WAVFileWriter::Mode::continuous &&
            sd_card.checkSDCard() == ESP_OK) {
            start_sound_recording();
        }

        #ifdef EDGE_IMPULSE_ENABLED

        // Check for deferred AI start (non-blocking)
        // When AI mode is enabled via BT, the actual thread start is deferred by a few seconds
        // to allow BT to serve follow-up commands (getStatus/getConfig) before the AI thread
        // consumes most CPU/memory resources.
        if (g_ai_start_pending && esp_timer_get_time() >= g_ai_deferred_start_time) {
            ESP_LOGI(TAG, "Deferred AI start timer expired, sending AI enable to queue");
            g_ai_start_pending = false;
            bool enable = true;
            xQueueSend(rec_ai_evt_queue, &enable, (TickType_t)0);
        }

        if (xQueueReceive(rec_ai_evt_queue, &ai_run_enable, pdMS_TO_TICKS(500))) {
            ESP_LOGI(TAG, "Received AI run enable = %d", ai_run_enable);
            auto ei_status = (edgeImpulse.get_status() == EdgeImpulse::Status::running ? "running" : "not running");
            ESP_LOGI(TAG, "EI current status = %s (%d)", ei_status, static_cast<int>(edgeImpulse.get_status()));

            if (ai_run_enable == false && (edgeImpulse.get_status() == EdgeImpulse::Status::running)) {
                ESP_LOGI(TAG, "Stopping EI thread");
                edgeImpulse.set_status(EdgeImpulse::Status::not_running);
            } else if (ai_run_enable == true && (edgeImpulse.get_status() == EdgeImpulse::Status::not_running)) {
                ESP_LOGI(TAG, "Starting EI thread");
                if (edgeImpulse.start_ei_thread(ei_callback_func) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start EI thread");
                    // Should this be retried?
                    delay(500);
                }
            }
        }

#else
        // Delay longer if not EI enabled
        delay(300);

#endif  // EDGE_IMPULSE_ENABLED

#ifdef USE_GPS
        // Self-configure the local timezone from a GPS fix (one-shot, app-set TZ wins), keep the
        // last-sync timestamp current, and power-cycle the GPS in low-power bursts while awake so it
        // is not left drawing current continuously during 24/7 recording. See manageGpsWhileAwake().
        manageGpsWhileAwake(gpsTzApplied);
#endif

        // Duty cycle sleep state machine - check if awake time expired
        handleSleepCycleStateMachine();

        // Don't forget the watchdog
        delay(1);
    }  // end while(true)

    // Should never get here
    input.uninstall();
    wav_writer.finish();

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
