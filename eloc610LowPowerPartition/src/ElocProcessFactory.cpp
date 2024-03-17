/*
 * Created on Sun Mar 03 2024
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2024 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "project_config.h"
#include "config.h"
#include "ElocConfig.hpp"

#include <sys/stat.h>

#include "ElocProcessFactory.hpp"

ElocProcessFactory elocProcessing;

// BUGME: this is rather crappy encapsulation.. signal_t requires non class function pointers
//       but all EdgeImpulse stuff got encapsulated within a class, which does not match
template <ElocProcessFactory* INST>
int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    return INST->getEdgeImpulse().microphone_audio_signal_get_data(offset, length, out_ptr);
}
static const read_func_t read_func = &microphone_audio_signal_get_data<&elocProcessing>;

static const char* TAG = "ElocProcessing";

//BUGME: this should be moved somewhere better
esp_err_t checkSDCard();

ElocProcessFactory::ElocProcessFactory():
    mInput(), 
#ifdef EDGE_IMPULSE_ENABLED
    mEdgeImpulse(I2S_DEFAULT_SAMPLE_RATE),
#endif
    mWav_writer(),
    mCurrentState(RecState::recordOff_detectOff),
    mReq_evt_queue(nullptr)
{
    mReq_evt_queue = xQueueCreate(10, sizeof(RecState));
    assert(mReq_evt_queue != nullptr);
    xQueueReset(mReq_evt_queue);

}

ElocProcessFactory::~ElocProcessFactory()
{
}

void ElocProcessFactory::init() {
#ifdef EDGE_IMPULSE_ENABLED

    this->testEdgeImpulse();

    #ifdef AI_CONTINUOUS_INFERENCE
        mEdgeImpulse.buffers_setup(EI_CLASSIFIER_SLICE_SIZE);
    #else
        mEdgeImpulse.buffers_setup(EI_CLASSIFIER_RAW_SAMPLE_COUNT);
    #endif  // AI_CONTINUOUS_INFERENCE

#endif  // EDGE_IMPULSE_ENABLED

    this->testInput();

    //TODO: this should be moved to begin() so a change in config is effective immedeately
    /**
     * @note Using MicUseTimingFix == true or false doesn't seem to effect ICS-43434 mic
     */
    mInput.init(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift,
                               getConfig().listenOnly, getMicInfo().MicUseTimingFix);
}

/**
 * @brief Create session folder on SD card & save config file
*/
bool ElocProcessFactory::createSessionFolder()
{
    //TODO: check if another session identifier based on ISO time for mat would be more helpful
    gSessionIdentifier = getDeviceInfo().fileHeader + String(time_utils::getSystemTimeMS());
    mSessionFolder = String("/sdcard/eloc/") + gSessionIdentifier;
    ESP_LOGI(TAG, "Creating session folder %s", mSessionFolder.c_str());
    mkdir(mSessionFolder.c_str(), 0777);

    String cfg;
    printConfig(cfg);
    ESP_LOGI(TAG, "Starting session with this config:\n: %s", cfg.c_str());
    String fname = mSessionFolder + "/" + gSessionIdentifier + ".config";
    FILE *f = fopen(fname.c_str(), "w+");

    if (f == nullptr) {
        ESP_LOGE(TAG, "Failed to open config file for storage %s!", fname.c_str());
        return false;
    }

    fprintf(f, "%s", cfg.c_str());
    fclose(f);

    session_folder_created = true;

    return true;
}

esp_err_t ElocProcessFactory::begin(RecState mode) { 
    
    switch (mode) {
        case RecState::recInvalid:
        case RecState::recordOff_detectOff:
            ESP_LOGE(TAG, "Ignoring invalid begin() mode %s", toString(mode));
            return ESP_ERR_INVALID_ARG;
        default:
            break;
    }
    if (mInput.is_i2s_installed_and_started()) {
        ESP_LOGE(TAG, "I2S Sampler already started");
        //TODO: should we abort here?
    }
    // Start a new recording?
    if (checkSDCard() == ESP_OK) {
        createSessionFolder();

        if ((mode == RecState::recordOn_detectOff) || 
            (mode == RecState::recordOn_detectOn) ||
            (mode == RecState::recordOnEvent)) {

            // TODO: add new mode as init parameter
            // create a new wave file wav_writer & make sure sample rate is up to date
            if (mWav_writer.initialize(i2s_mic_Config.sample_rate, 2, NUMBER_OF_CHANNELS) != true) {
                ESP_LOGE(TAG, "Failed to initialize WAVFileWriter");
            }
            switch(mode) {
                case RecState::recordOnEvent:
                    mWav_writer.set_mode(WAVFileWriter::Mode::single);
                    break;
                case RecState::recordOn_detectOff:
                case RecState::recordOn_detectOn:
                    mWav_writer.set_mode(WAVFileWriter::Mode::continuous);
                    break;
                default:
                    break;
            }
            
            // Start thread to continuously write to wav file & when sufficient data is collected finish the file
            mWav_writer.start_wav_write_task(getConfig().secondsPerFile);
        }
        #ifdef EDGE_IMPULSE_ENABLED
            mEdgeImpulse.enableSaveResultsToSD(mSessionFolder);
        #endif    
    }
    else {
        ESP_LOGW(TAG, "SD-Card not available, disabling WAV Writer!");
        mWav_writer.set_mode(WAVFileWriter::Mode::disabled);
        #ifdef EDGE_IMPULSE_ENABLED
            mEdgeImpulse.disableSaveResultsToSD();
        #endif    
    }
    #ifdef EDGE_IMPULSE_ENABLED
        if ((mode == RecState::recordOff_detectOn) || 
            (mode == RecState::recordOn_detectOn) ||
            (mode == RecState::recordOnEvent)) {
            if (mEdgeImpulse.start_ei_thread(read_func) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start EI thread");
                // Should this be retried?
            }
            
        }
    #endif

    mInput.register_wavFileWriter(&mWav_writer, mWav_writer.getTaskHandle());
    mInput.register_ei_inference(&mEdgeImpulse.getInference(), EI_CLASSIFIER_FREQUENCY, mEdgeImpulse.getTaskHandle());


    /**
     * @note Using MicUseTimingFix == true or false doesn't seem to effect ICS-43434 mic
     */
    mInput.init(I2S_DEFAULT_PORT, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift,
                               getConfig().listenOnly, getMicInfo().MicUseTimingFix);
    
    // Keep trying until successful
    if (mInput.install_and_start() == ESP_OK) {
        delay(300);
        mInput.zero_dma_buffer(I2S_DEFAULT_PORT);
        mInput.start_read_task(sample_buffer_size/ sizeof(signed short));
    }

    return ESP_OK; 
}

esp_err_t ElocProcessFactory::end() { 
    if (mCurrentState == RecState::recordOff_detectOff) {
        ESP_LOGE(TAG, "Processing already stopped!");
        return ESP_ERR_INVALID_STATE;
    }
    //TODO add tear down for wav writer, edgeImpule & I2S Sampler here
    mEdgeImpulse.set_status(EdgeImpulse::Status::not_running);
    mWav_writer.set_mode(WAVFileWriter::Mode::disabled);

    //Wait until WAV has finished current file
    // any need for waiting for EI ?

    //BUGME: this uninstalls the driver while the read task is still running --> not safe
    //       trigger a termination here and wait until the I2S task has terminated and uninstalled 
    //       the driver
    mInput.uninstall();
    //Wait until task is deleted?

    // TODO: shut down WAV Writer & EdgeImpulse
    
// vTaskSuspendAll();
// delete all tasks here
// xTaskResumeAll();


    // reset internal states
    session_folder_created = false;
    mCurrentState = RecState::recordOff_detectOff;
    mLastDetectedEvents = 0;
    return ESP_OK;
}

void ElocProcessFactory::testInput() {

    if (!getConfig().testI2SClockInput) return;

    ESP_LOGV(TAG, "Func: %s", __func__);

    for (uint32_t i = 1000; i < 34000; i = i + 2000) {
        i2s_mic_Config.sample_rate = i;
        i2s_mic_Config.use_apll = getMicInfo().MicUseAPLL;

        mInput.init(I2S_NUM_0, i2s_mic_pins, i2s_mic_Config, getMicInfo().MicBitShift, getConfig().listenOnly, getMicInfo().MicUseTimingFix);
        mInput.install_and_start();
        delay(100);
        ESP_LOGI(TAG, "Samplesrate %d Hz --> Clockrate: %f", i, i2s_get_clk(I2S_NUM_0));
        mInput.uninstall();
        delay(100);
    }

    delay(100);
}

void ElocProcessFactory::testEdgeImpulse() {
    auto s = elocProcessing.getEdgeImpulse().get_aiModel();
    ESP_LOGI(TAG, "Edge impulse model version: %s", s.c_str());
    mEdgeImpulse.output_inferencing_settings();

    mEdgeImpulse.test_inference(read_func);
}

bool ElocProcessFactory::checkQueueAndChangeMode(TickType_t xTicksToWait) {
    RecState newMode = RecState::recInvalid;
    //TODO: in case of recordOnEvent state set ticks to wait to 10 and continously check for mEdgeImpulse.get_detectedEvents()
    if (xQueueReceive(mReq_evt_queue, &newMode, xTicksToWait)) {
        ESP_LOGI(TAG, "Received new eloc processing mode: %s", toString(newMode));

        switch (newMode) {
            case RecState::recInvalid:
                ESP_LOGE(TAG, "Ignoring invalid mode");
                return true;
            case RecState::recordOff_detectOff:
                this->end();
                break;
            default:
                if (mCurrentState != RecState::recordOff_detectOff) {
                    ESP_LOGE(TAG, "Invalid state transition from %s to %s", toString(mCurrentState), toString(newMode));
                    return true;
                }
                this->begin(newMode);
        }
        return true;
    }
    else {
        if (this->getState() == RecState::recordOnEvent) {
            const uint32_t detectedEvents = mEdgeImpulse.get_detectedEvents();
            if (detectedEvents != mLastDetectedEvents) {
                if (checkSDCard() == ESP_OK) {
                    //TODO: Trigger WavFileWriter here (re-enable it in case it has stopped)
                    /*
                    if (mWav_writer.wav_recording_in_progress == false &&
                        mWav_writer.get_mode() == WAVFileWriter::Mode::single) {
                        //start_sound_recording();
                    }
                    */
                    ESP_LOGI(TAG, "Trigger WAV Writer after event has been detected!");
                }
                mLastDetectedEvents = detectedEvents;
            }
        }
        return false;
    }
}

BaseType_t ElocProcessFactory::queueNewMode(RecState newMode) {
    return xQueueSend(mReq_evt_queue, &newMode, (TickType_t)0);
}

BaseType_t ElocProcessFactory::queueNewModeISR(RecState newMode) {
    return xQueueSendFromISR(mReq_evt_queue, &newMode, (TickType_t)0);
}
