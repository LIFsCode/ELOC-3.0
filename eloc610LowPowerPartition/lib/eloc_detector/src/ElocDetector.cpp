/**
 * @file ElocDetector.cpp
 * @brief The TFLite Micro AI runtime (see ElocDetector.hpp)
 */

#include "ElocDetector.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <Arduino.h>  // delay()
#include "ESP32Time.h"
#include "ElocConfig.hpp"
#include "I2SMEMSSampler.h"

#include "CompiledModel.hpp"
#include "MlAlloc.hpp"

/**
 * @note Recording time comes from ESP32Time, like EdgeImpulse.cpp (esp_timer_get_time() was
 * found to be inaccurate for this).
 */
extern ESP32Time timeObject;
extern I2SMEMSSampler input;

using eloc_ml::MemKind;

static const char* TAG = "ElocDetector";

/// A silent window is logged at most this often
static const int64_t SILENT_LOG_INTERVAL_US = 60LL * 1000 * 1000;

/// How long the AI task waits for audio before re-checking whether it should stop
static const TickType_t AI_TASK_POLL_TICKS = pdMS_TO_TICKS(1000);

ElocDetector::ElocDetector(int i2s_sample_rate) {
    ESP_LOGV(TAG, "Func: %s", __func__);
    (void)i2s_sample_rate;  // the model's rate is checked against the real I2S rate at AI start
    inference = {};
    status = Status::not_running;
}

void ElocDetector::setError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(mError, sizeof(mError), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s", mError);
}

bool ElocDetector::loadModel() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    const size_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const int64_t t0 = esp_timer_get_time();

    mModelReady = false;
    mClassifier.release();
    if (!mPackage.load(eloc_ml::compiledModelData(), eloc_ml::compiledModelSize())) {
        setError("AI model rejected: %s", mPackage.error());
        return false;
    }
    if (!mClassifier.init(mPackage)) {
        setError("AI model cannot run: %s", mClassifier.error());
        mPackage.unload();
        return false;
    }
    mModelReady = true;
    mError[0] = '\0';

    const int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "AI model \"%s\" (job %s, created %s) loaded in %lld ms", mPackage.name(), mPackage.jobId(),
             mPackage.createdUtc(), (t1 - t0) / 1000);
    ESP_LOGI(TAG, "Tensor arena: %u of %u bytes used (estimate %u); front-end %u bytes while AI runs",
             static_cast<unsigned>(mClassifier.arenaUsed()), static_cast<unsigned>(mClassifier.arenaSize()),
             static_cast<unsigned>(mPackage.arenaEstimate()), static_cast<unsigned>(mClassifier.frontendBytes()));

    // The front-end's per-frame buffers want internal RAM, which Bluetooth is short of. Hold them
    // only while the AI task runs (start_ei_thread() takes them back), not from boot.
    mClassifier.suspend();
    ESP_LOGI(TAG, "Heap held by the model while AI is off: internal %d bytes, PSRAM %d bytes",
             static_cast<int>(internalBefore - heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<int>(psramBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return true;
}

void ElocDetector::output_inferencing_settings() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    if (!mModelReady) {
        ESP_LOGE(TAG, "No AI model loaded: %s", mError);
        return;
    }
    const eloc_ml::FeatureConfig& f = mPackage.features();
    static const char* transforms[] = {"linear", "log", "pcen", "pcen+log"};

    ESP_LOGI(TAG, "TFLite Micro inferencing settings:");
    ESP_LOGI(TAG, "Sample rate: %u Hz, window: %u samples (%u ms)", static_cast<unsigned>(f.sampleRate),
             static_cast<unsigned>(f.windowSamples), static_cast<unsigned>(f.windowSamples * 1000ULL / f.sampleRate));
    ESP_LOGI(TAG, "Features: %u frames x %u mels, frame %u / step %u / FFT %u, %s", static_cast<unsigned>(f.nFrames),
             static_cast<unsigned>(f.nMels), static_cast<unsigned>(f.frameLength), static_cast<unsigned>(f.frameStep),
             static_cast<unsigned>(f.fftLength), transforms[static_cast<int>(f.transform)]);
    ESP_LOGI(TAG, "Model: %u MAC per window, %s output", static_cast<unsigned>(mPackage.macs()),
             mPackage.activation() == eloc_ml::OutputActivation::Sigmoid ? "sigmoid" : "softmax");
    for (uint32_t i = 0; i < mPackage.labelCount(); i++) {
        const eloc_ml::DetectionDefaults& d = mPackage.detectionDefaults(i);
        if (d.present) {
            ESP_LOGI(TAG, "Label %u: %s (model recommends threshold %.0f, observationWindowS %u, "
                          "requiredDetections %u; not applied)",
                     static_cast<unsigned>(i), mPackage.label(i), d.threshold * 100.0f,
                     static_cast<unsigned>(d.observationWindowS), static_cast<unsigned>(d.requiredDetections));
        } else {
            ESP_LOGI(TAG, "Label %u: %s", static_cast<unsigned>(i), mPackage.label(i));
        }
    }
    ESP_LOGI(TAG, "Silence guard: windows with peak <= %d LSB are not classified", AI_SILENCE_PEAK);
}

String ElocDetector::get_aiModel() const {
    return mModelReady ? String(mPackage.name()) : String("");
}

bool ElocDetector::buffers_setup(uint32_t n_samples) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    if (inference.buffers[0] != nullptr && inference.buffers[1] != nullptr && inference.n_samples == n_samples) {
        return true;  // already set up
    }
    free_buffers();
    if (n_samples == 0) {
        return false;
    }

    // PSRAM, as EI_BUFFER_IN_PSRAM does for the Edge Impulse build
    int16_t* b0 = eloc_ml::mlAllocArray<int16_t>(n_samples, MemKind::Large);
    int16_t* b1 = eloc_ml::mlAllocArray<int16_t>(n_samples, MemKind::Large);
    if (b0 == nullptr || b1 == nullptr) {
        eloc_ml::mlFree(b0);
        eloc_ml::mlFree(b1);
        ESP_LOGE(TAG, "Failed to allocate 2 x %u bytes for the inference buffers",
                 static_cast<unsigned>(n_samples * sizeof(int16_t)));
        return false;
    }
    ESP_LOGI(TAG, "Allocated 2 inference buffers of %u samples in PSRAM", static_cast<unsigned>(n_samples));

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;
    inference.status_running = false;
    inference.buffers[0] = b0;
    inference.buffers[1] = b1;
    return true;
}

void ElocDetector::free_buffers(void) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    status = Status::not_running;
    int16_t* b0 = inference.buffers[0];
    int16_t* b1 = inference.buffers[1];
    // Detach first: I2SMEMSSampler skips a null buffer. Then give a read in progress time to finish.
    inference.buffers[0] = nullptr;
    inference.buffers[1] = nullptr;
    delay(100);

    inference.buf_select = 0;
    inference.buf_count = 0;
    inference.buf_ready = 0;
    eloc_ml::mlFree(b0);
    eloc_ml::mlFree(b1);
}

bool ElocDetector::microphone_inference_record(void) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    while (inference.buf_ready == 0) {
        delay(1);
    }
    inference.buf_ready = 0;
    return true;
}

esp_err_t ElocDetector::start_ei_thread(std::function<void()> _callback) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    if (!mModelReady) {
        ESP_LOGE(TAG, "AI not started, no usable model: %s", mError);
        return ESP_ERR_INVALID_STATE;
    }
    // The sampler keeps every Nth sample with no filter, so the I2S rate must be a whole multiple of
    // the model's rate; anything else would feed the model silently mis-decimated audio
    const uint32_t i2sRate = input.get_i2s_sampling_rate();
    const uint32_t modelRate = mPackage.sampleRate();
    if (i2sRate < modelRate || (i2sRate % modelRate) != 0) {
        setError("I2S sample rate %u Hz is not a whole multiple of the model's %u Hz", static_cast<unsigned>(i2sRate),
                 static_cast<unsigned>(modelRate));
        return ESP_ERR_INVALID_ARG;
    }
    if (inference.buffers[0] == nullptr || inference.buffers[1] == nullptr ||
        inference.n_samples != mPackage.windowSamples()) {
        setError("AI audio buffers are not allocated");
        return ESP_ERR_NO_MEM;
    }

    // A task that was just told to stop may still be finishing its last window
    for (int waitedMs = 0; mThreadAlive && waitedMs < 3000; waitedMs += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (mThreadAlive) {
        ESP_LOGE(TAG, "Previous AI task has not stopped, not starting another");
        return ESP_ERR_TIMEOUT;
    }
    mError[0] = '\0';

    // Internal RAM when there is some, PSRAM otherwise (slower, but AI still runs)
    const size_t internalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mClassifier.resume()) {
        setError("Out of memory for the AI front-end buffers");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Front-end buffers: %u bytes, %d of them in internal RAM (%u bytes internal free now)",
             static_cast<unsigned>(mClassifier.frontendBytes()),
             static_cast<int>(internalBefore - heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));

    status = Status::running;
    inference.status_running = true;
    detectingStartTime_sec = timeObject.getEpoch();
    detectingTime_secs = 0;
    mStackLogged = false;
    this->callback = _callback;

    mThreadAlive = true;
    BaseType_t ret = xTaskCreatePinnedToCore(start_ei_thread_wrapper, "ai_thread", AI_TASK_STACK_SIZE, this,
                                             TASK_PRIO_AI, &ei_TaskHandler, TASK_AI_CORE);
    if (ret != pdPASS) {
        mThreadAlive = false;
        status = Status::not_running;
        inference.status_running = false;
        mClassifier.suspend();
        setError("Could not create the AI task (%u byte stack)", static_cast<unsigned>(AI_TASK_STACK_SIZE));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AI task started (I2S %u Hz -> model %u Hz, every %u. sample)", static_cast<unsigned>(i2sRate),
             static_cast<unsigned>(modelRate), static_cast<unsigned>(i2sRate / modelRate));
    return ESP_OK;
}

void ElocDetector::start_ei_thread_wrapper(void* _this) {
    reinterpret_cast<ElocDetector*>(_this)->ei_thread();
}

void ElocDetector::ei_thread() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    while (status == Status::running) {
        // Time out regularly, so a stop request is seen even when no audio arrives (I2S stopped)
        if (xTaskNotifyWait(0, 0, NULL, AI_TASK_POLL_TICKS) != pdTRUE) {
            continue;
        }
        if (inference.buf_ready == 1 && status == Status::running) {
            callback();

            detectingTime_secs = timeObject.getEpoch() - detectingStartTime_sec;
            if (!mStackLogged) {
                mStackLogged = true;
                ESP_LOGI(TAG, "AI task stack: %u of %u bytes never used after the first window",
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(NULL)),
                         static_cast<unsigned>(AI_TASK_STACK_SIZE));
            }
        }
    }

    // To avoid rounding errors, only accumulate on exit
    totalDetectingTime_secs += timeObject.getEpoch() - detectingStartTime_sec;
    detectingTime_secs = 0;

    // Stop the sampler notifying this task, let a notification already under way land, then drop
    // the handle so nobody can notify a deleted task
    inference.status_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    ei_TaskHandler = nullptr;
    mClassifier.suspend();  // give the front-end's internal RAM back while AI is off
    ESP_LOGI(TAG, "AI task stopped");
    mThreadAlive = false;
    vTaskDelete(NULL);
}

ElocDetector::WindowResult ElocDetector::classify_window(float* probs, uint32_t& dspMs, uint32_t& nnMs) {
    if (!mModelReady) {
        return WindowResult::Failed;
    }
    // The sampler has already switched to the other buffer; this one holds the finished window
    const int16_t* window = inference.buffers[inference.buf_select ^ 1];
    if (window == nullptr) {
        return WindowResult::Failed;
    }

#if AI_SILENCE_PEAK > 0
    // Silence guard: a dead or muted microphone must not be scored (models trained without silent
    // audio can score digital silence as a detection). A dead-mic net, not a detection feature.
    int peak = 0;
    for (uint32_t i = 0; i < inference.n_samples && peak <= AI_SILENCE_PEAK; i++) {
        int a = abs(static_cast<int>(window[i]));
        if (a > peak) {
            peak = a;
        }
    }
    if (peak <= AI_SILENCE_PEAK) {
        silentWindows++;
        silentSinceLog++;
        const int64_t now = esp_timer_get_time();
        if (lastSilentLogUs == 0 || now - lastSilentLogUs >= SILENT_LOG_INTERVAL_US) {
            ESP_LOGW(TAG, "Silent window not classified (peak <= %d LSB): %u since the last report, %u in total. "
                          "Microphone disconnected or muted?",
                     AI_SILENCE_PEAK, static_cast<unsigned>(silentSinceLog), static_cast<unsigned>(silentWindows));
            lastSilentLogUs = now;
            silentSinceLog = 0;
        }
        return WindowResult::Silent;
    }
#endif

    eloc_ml::TflmClassifier::Timing timing;
    if (!mClassifier.classify(window, probs, &timing)) {
        ESP_LOGE(TAG, "ERR: Failed to run classifier (Invoke)");
        return WindowResult::Failed;
    }
    dspMs = timing.dspUs / 1000;
    nnMs = timing.nnUs / 1000;
    lastDspMs = dspMs;
    lastNnMs = nnMs;
    return WindowResult::Classified;
}

void ElocDetector::updateEventInfo(const char* const* labels, const float* values, uint32_t numMatches) {
    lastEventInfo.time = timeObject.getSystemTimeMS() / 1000;  // store sys time in seconds
    if (numMatches > AI_MAX_LABELS) {
        ESP_LOGE(TAG, "Number of Event Matches %u, exceeds Label count(%d)!", static_cast<unsigned>(numMatches),
                 AI_MAX_LABELS);
        numMatches = AI_MAX_LABELS;  // limit to max. to avoid buffer overflows
    }
    lastEventInfo.numClassifierMatch = numMatches;
    for (uint32_t i = 0; i < numMatches; i++) {
        lastEventInfo.label[i] = String(labels[i]);
        lastEventInfo.classifierValue[i] = values[i];
    }
}

// The observation window below is the same logic as EdgeImpulse's (and calibration.py's
// simulate_events in the training worker, which the model's recommended settings come from).

void ElocDetector::addDetectionToWindow(uint32_t timestamp) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    detectionWindow.detectionTimes[detectionWindow.writeIndex] = timestamp;
    detectionWindow.writeIndex = (detectionWindow.writeIndex + 1) % 128;
    if (detectionWindow.count < 128) {
        detectionWindow.count++;
    }
    ESP_LOGV(TAG, "Added detection at time %u, count now %d", timestamp, detectionWindow.count);
}

bool ElocDetector::checkDetectionCriteria(uint32_t currentTime) {
    ESP_LOGV(TAG, "Func: %s", __func__);

    // Read on every window, so a change from the app applies to the next one without a reboot
    const inferenceConfig_t& config = getInferenceConfig();

    // Legacy mode: observationWindowS = 0 means immediate action
    if (config.observationWindowS == 0) {
        ESP_LOGV(TAG, "Legacy mode: immediate action");
        return true;
    }

    uint32_t validDetections = 0;
    uint32_t windowStart = currentTime - config.observationWindowS;
    for (int i = 0; i < detectionWindow.count; i++) {
        uint32_t detectionTime = detectionWindow.detectionTimes[i];
        if (detectionTime >= windowStart && detectionTime <= currentTime) {
            validDetections++;
        }
    }
    ESP_LOGV(TAG, "Valid detections in window: %u, required: %u", validDetections, config.requiredDetections);
    return validDetections >= config.requiredDetections;
}

void ElocDetector::clearDetectionWindow() {
    ESP_LOGV(TAG, "Func: %s", __func__);

    detectionWindow.writeIndex = 0;
    detectionWindow.count = 0;
}
