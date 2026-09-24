/**
 * @file ElocDetector.hpp
 * @brief The TFLite Micro AI runtime (esp32dev-tflm): model, audio buffers, AI task, event counters
 *
 * Drop-in for the EdgeImpulse class: it offers the same public names and types the shared firmware
 * code uses (main.cpp, ElocLora, ElocCommands, I2SMEMSSampler), selected through
 * include/ai_runtime.h. The per-window detection rules stay in main.cpp (handleClassification()),
 * shared by both runtimes.
 *
 * Memory is allocated once: the model and classifier at boot (loadModel()), the audio double buffer
 * in buffers_setup(). Nothing is allocated per window.
 *
 * A library of its own, apart from lib/eloc_ml: this is firmware glue (config, sampler, clock), while
 * eloc_ml stays self-contained so its tests build without the rest of the firmware.
 */

#ifndef ELOC_ML_ELOCDETECTOR_HPP_
#define ELOC_ML_ELOCDETECTOR_HPP_

#include <WString.h>
#include <functional>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../../../include/ei_inference.h"  // inference_t
#include "../../../include/project_config.h"
#include "ModelPackage.hpp"
#include "TflmClassifier.hpp"

extern TaskHandle_t ei_TaskHandler;

class ElocDetector {
 public:
    /// Filled by I2SMEMSSampler (decimated to the model's sample rate), read by the AI task
    inference_t inference;

    enum class Status { not_running = 0, running = 1 };

    struct DetectedEventInfo {
        int64_t time;
        uint8_t numClassifierMatch;
        float classifierValue[AI_MAX_LABELS];
        String label[AI_MAX_LABELS];
    };

    /// Outcome of classify_window()
    enum class WindowResult {
        Classified,  ///< probabilities are valid
        Silent,      ///< skipped by the silence guard (AI_SILENCE_PEAK): no detection
        Failed,      ///< classifier error: no detection
    };

    explicit ElocDetector(int i2s_sample_rate);

    // ---- model

    /**
     * @brief Load and validate the compiled-in model and build the classifier
     * @note  Call once at boot, before Bluetooth starts, while the heap is quiet. On failure the
     *        reason is logged and kept in get_aiError(); AI then refuses to start. Never crashes.
     * @return true when the model is ready to run
     */
    bool loadModel();

    bool model_ready() const { return mModelReady; }
    const eloc_ml::ModelPackage& model() const { return mPackage; }

    /// Log the model and runtime settings (the EdgeImpulse counterpart prints model_metadata.h)
    void output_inferencing_settings();

    /// Model sample rate in Hz (0 if no model)
    uint32_t get_sample_rate() const { return mModelReady ? mPackage.sampleRate() : 0; }
    /// Samples per inference window (0 if no model)
    uint32_t get_window_samples() const { return mModelReady ? mPackage.windowSamples() : 0; }

    // ---- audio buffers (same contract as EdgeImpulse)

    bool buffers_setup(uint32_t n_samples);
    void free_buffers(void);
    bool microphone_inference_record(void);
    inference_t& getInference() { return inference; }

    // ---- AI task

    enum Status get_status() const { return status; }
    void set_status(enum Status newStatus) { status = newStatus; }

    /**
     * @brief Start the AI task, which calls @p callback for every completed window
     * @return ESP_OK, or an error when no model is loaded, the I2S rate does not fit the model, or
     *         the task could not be created (reason in get_aiError())
     */
    esp_err_t start_ei_thread(std::function<void()> callback);

    /**
     * @brief Classify the window the sampler just completed (the buffer not being filled)
     * @param probs  labelCount() probabilities, index 0 = background
     * @param dspMs  front-end time
     * @param nnMs   Invoke() time
     */
    WindowResult classify_window(float* probs, uint32_t& dspMs, uint32_t& nnMs);

    // ---- labels

    uint32_t get_label_count() const { return mModelReady ? mPackage.labelCount() : 0; }
    const char* get_ei_classifier_inferencing_categories(int i) const {
        return mPackage.label(static_cast<uint32_t>(i));
    }

    // ---- counters and event info (same semantics as EdgeImpulse)

    uint32_t get_detectingTime_secs() const { return detectingTime_secs; }
    uint32_t get_totalDetectingTime_secs() const { return totalDetectingTime_secs + detectingTime_secs; }
    String get_aiModel() const;
    void increment_detectedEvents() { detectedEvents++; }
    uint32_t get_detectedEvents() const { return detectedEvents; }
    void updateEventInfo(const char* const* labels, const float* values, uint32_t numMatches);
    DetectedEventInfo get_lastEventInfo() const { return lastEventInfo; }

    void addDetectionToWindow(uint32_t timestamp);
    bool checkDetectionCriteria(uint32_t currentTime);
    void clearDetectionWindow();

    // ---- status extras (TFLM only)

    /// Why AI cannot run ("" when fine)
    const char* get_aiError() const { return mError; }
    uint32_t get_silentWindows() const { return silentWindows; }
    uint32_t get_lastDspMs() const { return lastDspMs; }
    uint32_t get_lastNnMs() const { return lastNnMs; }
    size_t get_arenaUsed() const { return mClassifier.arenaUsed(); }

 private:
    void ei_thread();
    static void start_ei_thread_wrapper(void* _this);
    void setError(const char* fmt, ...);

    Status status = Status::not_running;
    std::function<void()> callback;

    eloc_ml::ModelPackage mPackage;
    eloc_ml::TflmClassifier mClassifier;
    bool mModelReady = false;
    char mError[128] = "";

    /// Set while the AI task exists; start waits for a stopping task to finish first
    volatile bool mThreadAlive = false;
    bool mStackLogged = false;

    int64_t detectingStartTime_sec = 0;
    uint32_t detectingTime_secs = 0;
    uint32_t totalDetectingTime_secs = 0;
    uint32_t detectedEvents = 0;
    DetectedEventInfo lastEventInfo;

    uint32_t silentWindows = 0;
    int64_t lastSilentLogUs = 0;
    uint32_t silentSinceLog = 0;
    uint32_t lastDspMs = 0;
    uint32_t lastNnMs = 0;

    /// Detection tracking for the observation window (same as EdgeImpulse)
    struct {
        uint32_t detectionTimes[128];
        uint8_t writeIndex = 0;
        uint8_t count = 0;
    } detectionWindow;
};

#endif  // ELOC_ML_ELOCDETECTOR_HPP_
