/*
 * Created on Fri Nov 13 2026
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2026 EDsteve
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

#include "MicTest.hpp"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
// esp_rtc_get_time_us(): the clock the sampler's own rate meter uses, immune to CPU frequency
// scaling (see I2SMEMSSampler::start_read_thread)
#include "esp32/rtc.h"

#include "ArduinoJson.h"
#include "ESP32Time.h"
#include "SDCardSDIO.h"
#include "I2SMEMSSampler.h"
#include "MicAnalyzer.hpp"
#include "ElocSystem.hpp"
#include "ElocConfig.hpp"
#include "FwUpdateTransfer.hpp"
#include "../../ElocHardware/src/ElocLora.hpp"
#include "../../../include/project_config.h"

extern ESP32Time timeObject;
extern SDCardSDIO sd_card;

static const char* TAG = "MicTest";

static_assert(MicTest::MAX_FREQS == mictest::MAX_TONES, "tone limits differ");

namespace {

/// Skipped after every buzzer switch. A sample reaches the capture up to ~130 ms after the
/// microphone picked it up (one DMA buffer filling plus one being read), so the switch marks run
/// ahead of the sound; the rest is margin for the buzzer starting and stopping.
const uint32_t GUARD_MS = 200;
/// Skipped at the very start of the capture.
const uint32_t LEAD_SKIP_MS = 100;
/// Recorded after the last gap so its samples are all in.
const uint32_t TAIL_MS = 300;
/// The main loop starts I2S within about 1.5 s of being asked; be generous.
const uint32_t SAMPLES_TIMEOUT_MS = 6000;
/// Let a freshly powered microphone settle before listening to it.
const uint32_t MIC_SETTLE_MS = 500;
/// Longest capture a single test may ask for.
const uint32_t MAX_CAPTURE_MS = 30000;
/// Analysis plus saving, for the progress estimate only.
const uint32_t POST_PROCESS_MS = 2500;
const uint32_t I2S_START_MS = 1500;

const char* C_MICTEST_DIR = "/sdcard/mictest";

/// Everything getMicTest reports about the last test. Written by the test task while the state
/// is "busy", read by the Bluetooth task once it is done/error - never both at once.
struct Report {
    mictest::Result result;
    uint32_t testId;
    uint32_t sampleRate;
    uint32_t sampleRateMeasured;
    uint32_t capturedMs;
    bool     i2sWasRunning;
    int      micVolume;
    uint16_t skipped[MicTest::MAX_FREQS];
    uint8_t  numSkipped;
    char     time[32];
    char     wavPath[112];
    char     saveError[64];
    char     error[160];
};

Report sReport;

// Tones the current test dropped because they do not fit the sample rate. Decided in start()
// (Bluetooth task), copied into the report by the test task.
uint16_t sSkipped[MicTest::MAX_FREQS];
uint8_t sNumSkipped = 0;

const char* stateName(MicTest::State s) {
    switch (s) {
        case MicTest::State::idle:      return "idle";
        case MicTest::State::starting:  return "starting";
        case MicTest::State::capturing: return "capturing";
        case MicTest::State::analysing: return "analysing";
        case MicTest::State::saving:    return "saving";
        case MicTest::State::done:      return "done";
        case MicTest::State::error:     return "error";
    }
    return "unknown";
}

// malloc() never reaches PSRAM on this build (CONFIG_SPIRAM_USE_CAPS_ALLOC) and internal RAM is
// scarce while Bluetooth, LoRa and the AI runtime are up, so the result document asks for PSRAM
// first - the same choice ElocConfig makes for the config document.
struct PsramJsonAllocator {
    void* allocate(size_t size) {
        return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
    }
    void deallocate(void* ptr) {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t newSize) {
        return heap_caps_realloc_prefer(ptr, newSize, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
    }
};
typedef BasicJsonDocument<PsramJsonAllocator> PsramJsonDocument;

/// One decimal is all these numbers are good for, and it keeps the reply short.
double round1(float v) {
    return round(static_cast<double>(v) * 10.0) / 10.0;
}

void delayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t msToSamples(uint32_t ms, uint32_t sampleRate) {
    return static_cast<uint32_t>((static_cast<uint64_t>(ms) * sampleRate) / 1000);
}

uint32_t captureMs(const MicTest::Params& p) {
    return p.silenceMs + static_cast<uint32_t>(p.reps) * p.numFreqs * (static_cast<uint32_t>(p.onMs) + p.gapMs) + TAIL_MS;
}

}  // namespace

MicTest::Params::Params() : numFreqs(3), reps(2), onMs(600), gapMs(400), silenceMs(1500), save(true) {
    memset(freqs, 0, sizeof(freqs));
    freqs[0] = 1000;
    freqs[1] = 2700;  // the MLT-8540H buzzer is loudest around here
    freqs[2] = 4000;
}

void MicTest::setSampler(I2SMEMSSampler* sampler) {
    mSampler = sampler;
}

bool MicTest::isBusy() const {
    const State s = mState.load();
    return (s == State::starting) || (s == State::capturing) || (s == State::analysing) || (s == State::saving);
}

esp_err_t MicTest::start(const Params& params, String& errMsg) {
    if (isBusy()) {
        errMsg = "a microphone test is already running";
        return ESP_ERR_INVALID_STATE;
    }
    if (mSampler == nullptr) {
        errMsg = "microphone test is not set up";
        return ESP_ERR_INVALID_STATE;
    }
    if (FwUpdateTransfer::GetInstance().isBinaryMode()) {
        errMsg = "firmware transfer in progress";
        return ESP_ERR_INVALID_STATE;
    }
    if (ElocLora::GetInstance().surveyIsActive()) {
        errMsg = "coverage survey is running and uses the buzzer - stop it first";
        return ESP_ERR_INVALID_STATE;
    }
    ElocSystem& sys = ElocSystem::GetInstance();
    if ((params.numFreqs > 0) && (sys.isSirenActive() || (sys.getIntruderState() != IntruderState_t::IDLE))) {
        errMsg = "intruder alarm is active and owns the buzzer";
        return ESP_ERR_INVALID_STATE;
    }

    if ((params.numFreqs > MAX_FREQS) || (params.reps < 1) || (params.reps > mictest::MAX_REPS)) {
        errMsg = "use up to 6 tones and reps 1..5";
        return ESP_ERR_INVALID_ARG;
    }
    if ((params.onMs < 400) || (params.onMs > 3000) || (params.gapMs < 300) || (params.gapMs > 3000) ||
        (params.silenceMs < 500) || (params.silenceMs > 10000)) {
        errMsg = "allowed: onMs 400..3000, gapMs 300..3000, silenceMs 500..10000";
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t fs = mSampler->get_i2s_sampling_rate();
    Params p = params;
    p.numFreqs = 0;
    uint8_t numSkipped = 0;
    uint16_t skipped[MAX_FREQS] = {0};
    for (uint8_t i = 0; i < params.numFreqs; i++) {
        if (mictest::toneUsable(params.freqs[i], fs)) {
            p.freqs[p.numFreqs++] = params.freqs[i];
        } else {
            skipped[numSkipped++] = params.freqs[i];
        }
    }
    if ((params.numFreqs > 0) && (p.numFreqs == 0)) {
        errMsg = String("no usable tone: at ") + fs + " Hz sample rate tones must be 100.." +
                 static_cast<uint32_t>(0.45f * fs) + " Hz";
        return ESP_ERR_INVALID_ARG;
    }
    // A buzzer-on stretch must hold at least two analysis blocks after the guard time
    if ((p.numFreqs > 0) &&
        (msToSamples(p.onMs - GUARD_MS, fs) < (mictest::FFT_N + mictest::FFT_HALF))) {
        const uint32_t needMs = GUARD_MS + ((mictest::FFT_N + mictest::FFT_HALF) * 1000 + fs - 1) / fs;
        errMsg = String("onMs too short for ") + fs + " Hz sample rate, use at least " + needMs;
        return ESP_ERR_INVALID_ARG;
    }
    if (captureMs(p) > MAX_CAPTURE_MS) {
        errMsg = "test too long - at most 30 s in total";
        return ESP_ERR_INVALID_ARG;
    }

    const State previous = mState.load();
    mParams = p;
    sNumSkipped = numSkipped;
    memcpy(sSkipped, skipped, sizeof(sSkipped));
    mPlannedMs = captureMs(p) + POST_PROCESS_MS +
                 (mSampler->is_i2s_installed_and_started() ? 0 : (I2S_START_MS + MIC_SETTLE_MS));
    mStartUs = esp_timer_get_time();
    mState.store(State::starting);
    mNeedsI2S.store(true);

    // 8 KB: saving walks the same deep FatFS path that overran the Bluetooth task's old 4 KB stack
    if (xTaskCreate(taskEntry, "MicTest", 8192, this, TASK_PRIO_CMD, NULL) != pdPASS) {
        mNeedsI2S.store(false);
        mState.store(previous);
        errMsg = "not enough memory to start the test task";
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void MicTest::taskEntry(void* arg) {
    static_cast<MicTest*>(arg)->run();
    vTaskDelete(NULL);
}

void MicTest::tapEntry(const int32_t* samples, size_t count, void* ctx) {
    static_cast<MicTest*>(ctx)->onSamples(samples, count);
}

// Runs in the I2S read task, once per buffer: copy and return.
void MicTest::onSamples(const int32_t* samples, size_t count) {
    mTapCalls.fetch_add(1);
    if (!mCapturing.load()) {
        return;
    }
    const uint32_t written = mWritten.load();
    uint32_t n = static_cast<uint32_t>(count);
    if (n > (mCapacity - written)) {
        n = mCapacity - written;
    }
    if (n == 0) {
        return;
    }
    int32_t* dst = mBuf + written;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = samples[i] >> 8;  // 24-bit value, sign kept; no volume shift
    }
    const int64_t now = static_cast<int64_t>(esp_rtc_get_time_us());
    const uint32_t end = written + n;
    if (written == 0) {
        mFirstBlockUs = now;
        mFirstBlockEnd = end;
    }
    mLastBlockUs = now;
    mLastBlockEnd = end;
    mWritten.store(end);
}

void MicTest::cleanup() {
    mCapturing.store(false);
    if (mSampler != nullptr) {
        mSampler->set_raw_tap(nullptr, nullptr);
    }
    if (mBuzzerHeld) {
        ElocSystem::GetInstance().releaseBuzzerForTest();
        mBuzzerHeld = false;
    }
    mNeedsI2S.store(false);
    // The read task may be in the middle of its last copy
    delayMs(100);
}

void MicTest::fail(const char* msg) {
    ESP_LOGE(TAG, "Test %u failed: %s", sReport.testId, msg);
    cleanup();
    if (mBuf != nullptr) {
        heap_caps_free(mBuf);
        mBuf = nullptr;
    }
    if (mWorkspace != nullptr) {
        heap_caps_free(mWorkspace);
        mWorkspace = nullptr;
    }
    strlcpy(sReport.error, msg, sizeof(sReport.error));
    mState.store(State::error);
}

void MicTest::run() {
    Report& rep = sReport;
    memset(&rep, 0, sizeof(rep));
    rep.testId = ++mTestCount;
    rep.micVolume = getMicInfo().MicVolume2_pwr;
    rep.numSkipped = sNumSkipped;
    memcpy(rep.skipped, sSkipped, sizeof(rep.skipped));
    strlcpy(rep.time, timeObject.getDateTime(false).c_str(), sizeof(rep.time));
    const String fileStamp = timeObject.getTime("%Y%m%d_%H%M%S");

    ElocSystem& sys = ElocSystem::GetInstance();
    const Params& p = mParams;
    ESP_LOGI(TAG, "Test %u: %u tone(s) x %u, on %u ms, gap %u ms, silence %u ms", rep.testId,
             p.numFreqs, p.reps, p.onMs, p.gapMs, p.silenceMs);

    // ---- 1. samples arriving? ---------------------------------------------------------------
    // needsI2S() is already set, so the main loop starts I2S if nothing else has. The tap is
    // hooked up straight away and only counts until the capture begins - the first call proves
    // that the read task is delivering.
    rep.i2sWasRunning = mSampler->is_i2s_installed_and_started();
    mCapturing.store(false);
    mTapCalls.store(0);
    mSampler->set_raw_tap(tapEntry, this);
    uint32_t waited = 0;
    while ((mTapCalls.load() == 0) && (waited < SAMPLES_TIMEOUT_MS)) {
        delayMs(50);
        waited += 50;
    }
    if (mTapCalls.load() == 0) {
        fail("no samples from the microphone - the I2S driver did not start");
        return;
    }
    if (!rep.i2sWasRunning) {
        delayMs(MIC_SETTLE_MS);
    }

    // ---- 2. memory ---------------------------------------------------------------------------
    const uint32_t fs = mSampler->get_i2s_sampling_rate();
    rep.sampleRate = fs;
    const uint32_t plannedMs = captureMs(p);
    mCapacity = msToSamples(plannedMs + 200, fs);
    const size_t bufBytes = static_cast<size_t>(mCapacity) * sizeof(int32_t);
    mBuf = static_cast<int32_t*>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    mWorkspace = heap_caps_malloc_prefer(sizeof(mictest::Workspace), 2,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_8BIT);
    if ((mBuf == nullptr) || (mWorkspace == nullptr)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "not enough PSRAM: need %u kB, largest free block %u kB",
                 static_cast<unsigned>((bufBytes + sizeof(mictest::Workspace)) / 1024),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
        fail(msg);
        return;
    }

    // ---- 3. buzzer ---------------------------------------------------------------------------
    if (p.numFreqs > 0) {
        if (!sys.acquireBuzzerForTest()) {
            fail("intruder alarm is active and owns the buzzer");
            return;
        }
        mBuzzerHeld = true;
        // Whatever beep was cut off must not ring into the silent part
        delayMs(GUARD_MS);
    }

    // ---- 4. capture --------------------------------------------------------------------------
    mictest::Mark marks[mictest::MAX_MARKS];
    size_t numMarks = 0;
    mWritten.store(0);
    mFirstBlockUs = 0;
    mLastBlockUs = 0;
    mFirstBlockEnd = 0;
    mLastBlockEnd = 0;
    mCapturing.store(true);
    mState.store(State::capturing);
    const int64_t captureStartUs = esp_timer_get_time();

    delayMs(p.silenceMs);
    for (uint8_t r = 0; r < p.reps; r++) {
        for (uint8_t t = 0; t < p.numFreqs; t++) {
            // The write position is read BEFORE each switch: everything captured up to here is
            // certain to be from before it.
            marks[numMarks].tone = t;
            marks[numMarks].onIdx = mWritten.load();
            sys.testTone(p.freqs[t]);
            delayMs(p.onMs);
            marks[numMarks].offIdx = mWritten.load();
            sys.testTone(0);
            numMarks++;
            delayMs(p.gapMs);
        }
    }
    delayMs(TAIL_MS);

    const uint32_t elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - captureStartUs) / 1000);
    cleanup();  // stops the capture, gives the buzzer back, waits out the last copy
    const uint32_t captured = mWritten.load();
    rep.capturedMs = static_cast<uint32_t>((static_cast<uint64_t>(captured) * 1000) / fs);
    if ((mLastBlockUs > mFirstBlockUs) && (mLastBlockEnd > mFirstBlockEnd)) {
        rep.sampleRateMeasured = static_cast<uint32_t>(
            (static_cast<uint64_t>(mLastBlockEnd - mFirstBlockEnd) * 1000000ULL) /
            static_cast<uint64_t>(mLastBlockUs - mFirstBlockUs));
    }
    ESP_LOGI(TAG, "Captured %u samples in %u ms (%u Hz measured, %u Hz configured)", captured,
             elapsedMs, rep.sampleRateMeasured, fs);
    if (captured < msToSamples(elapsedMs, fs) / 2) {
        char msg[128];
        snprintf(msg, sizeof(msg), "capture incomplete: %u samples in %u ms at %u Hz - I2S stopped delivering",
                 captured, elapsedMs, fs);
        fail(msg);
        return;
    }

    // ---- 5. analyse --------------------------------------------------------------------------
    mState.store(State::analysing);
    mictest::Segment segments[mictest::MAX_SEGMENTS];
    const size_t numSegments = mictest::buildSegments(marks, numMarks, captured, msToSamples(GUARD_MS, fs),
                                                      msToSamples(LEAD_SKIP_MS, fs), segments,
                                                      mictest::MAX_SEGMENTS);
    mictest::Input in;
    in.samples = mBuf;
    in.count = captured;
    in.sampleRate = fs;
    in.segments = segments;
    in.numSegments = numSegments;
    in.toneHz = p.freqs;
    in.numTones = p.numFreqs;
    const int64_t analyseStartUs = esp_timer_get_time();
    const bool ok = mictest::analyze(in, *static_cast<mictest::Workspace*>(mWorkspace), rep.result);
    ESP_LOGI(TAG, "Analysis took %u ms", static_cast<unsigned>((esp_timer_get_time() - analyseStartUs) / 1000));
    heap_caps_free(mWorkspace);
    mWorkspace = nullptr;
    if (!ok) {
        fail("capture too short to analyse");
        return;
    }

    // ---- 6. save -----------------------------------------------------------------------------
    if (p.save) {
        mState.store(State::saving);
        snprintf(rep.wavPath, sizeof(rep.wavPath), "%s/%s_%s.wav", C_MICTEST_DIR,
                 getDeviceInfo().nodeName.c_str(), fileStamp.c_str());
        saveFiles(captured);
    }
    heap_caps_free(mBuf);
    mBuf = nullptr;

    const mictest::Result& res = rep.result;
    ESP_LOGI(TAG, "Test %u done: noise %.1f dBFS, slope %.1f dB/oct, impulses %u, crest %.1f dB, stuckRun %u",
             rep.testId, res.noiseDbfs, res.slopeDbPerOct, res.impulses, res.crestDb, res.stuckRun);
    for (size_t t = 0; t < res.numTones; t++) {
        const mictest::ToneResult& tone = res.tones[t];
        if (tone.valid) {
            ESP_LOGI(TAG, "  %u Hz: on %.1f dBFS, off %.1f dBFS, rise %.1f dB (min %.1f), peak %.0f Hz",
                     tone.freqHz, tone.onDbfs, tone.offDbfs, tone.riseDb, tone.riseMinDb, tone.peakHz);
        } else {
            ESP_LOGW(TAG, "  %u Hz: not measured", tone.freqHz);
        }
    }
    mState.store(State::done);
}

// 24-bit mono WAV of the capture plus the result as JSON beside it. A failure here never fails
// the test: the numbers are already in the report, only "saveError" is added.
void MicTest::saveFiles(uint32_t sampleCount) {
    Report& rep = sReport;
    if (!sd_card.isMounted()) {
        strlcpy(rep.saveError, "no SD card", sizeof(rep.saveError));
        rep.wavPath[0] = '\0';
        return;
    }
    // Hold the card against a hot-swap unmount, as the survey and the AI results writer do
    if (!sd_card.claimFs(3000)) {
        strlcpy(rep.saveError, "SD card busy", sizeof(rep.saveError));
        rep.wavPath[0] = '\0';
        return;
    }
    const size_t CHUNK_SAMPLES = 1024;  // 3072 bytes = 6 sectors
    uint8_t* chunk = nullptr;
    FILE* fp = nullptr;
    do {
        if (!sd_card.isMounted()) {
            strlcpy(rep.saveError, "no SD card", sizeof(rep.saveError));
            break;
        }
        if ((mkdir(C_MICTEST_DIR, 0777) != 0) && (errno != EEXIST)) {
            snprintf(rep.saveError, sizeof(rep.saveError), "cannot create %s (errno %d)", C_MICTEST_DIR, errno);
            break;
        }
        // Internal DMA-capable memory lets the SD driver write straight from the buffer; PSRAM
        // works too, only slower
        chunk = static_cast<uint8_t*>(heap_caps_malloc_prefer(CHUNK_SAMPLES * 3, 2,
                                                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (chunk == nullptr) {
            strlcpy(rep.saveError, "no memory for the WAV buffer", sizeof(rep.saveError));
            break;
        }
        fp = fopen(rep.wavPath, "wb");
        if (fp == nullptr) {
            snprintf(rep.saveError, sizeof(rep.saveError), "cannot open WAV file (errno %d)", errno);
            break;
        }

        const uint32_t dataBytes = sampleCount * 3;
        uint8_t header[44];
        memcpy(header, "RIFF", 4);
        const uint32_t riffSize = 36 + dataBytes;
        memcpy(header + 4, &riffSize, 4);
        memcpy(header + 8, "WAVEfmt ", 8);
        const uint32_t fmtSize = 16;
        const uint16_t pcm = 1;
        const uint16_t channels = 1;
        const uint32_t rate = rep.sampleRate;
        const uint32_t byteRate = rate * 3;
        const uint16_t blockAlign = 3;
        const uint16_t bits = 24;
        memcpy(header + 16, &fmtSize, 4);
        memcpy(header + 20, &pcm, 2);
        memcpy(header + 22, &channels, 2);
        memcpy(header + 24, &rate, 4);
        memcpy(header + 28, &byteRate, 4);
        memcpy(header + 32, &blockAlign, 2);
        memcpy(header + 34, &bits, 2);
        memcpy(header + 36, "data", 4);
        memcpy(header + 40, &dataBytes, 4);
        bool writeOk = (fwrite(header, 1, sizeof(header), fp) == sizeof(header));

        for (uint32_t pos = 0; writeOk && (pos < sampleCount); pos += CHUNK_SAMPLES) {
            const uint32_t n = ((sampleCount - pos) < CHUNK_SAMPLES) ? (sampleCount - pos) : CHUNK_SAMPLES;
            for (uint32_t i = 0; i < n; i++) {
                const int32_t s = mBuf[pos + i];
                chunk[3 * i]     = static_cast<uint8_t>(s);
                chunk[3 * i + 1] = static_cast<uint8_t>(s >> 8);
                chunk[3 * i + 2] = static_cast<uint8_t>(s >> 16);
            }
            writeOk = (fwrite(chunk, 3, n, fp) == n);
        }
        if (fclose(fp) != 0) {
            writeOk = false;
        }
        fp = nullptr;
        if (!writeOk) {
            snprintf(rep.saveError, sizeof(rep.saveError), "writing the WAV failed (errno %d)", errno);
            break;
        }

        // The result beside it, same name: a folder of tests can then be compared without the device
        String jsonPath = rep.wavPath;
        jsonPath.replace(".wav", ".json");
        String json;
        buildResultJson(json, true);
        fp = fopen(jsonPath.c_str(), "w");
        if (fp == nullptr) {
            snprintf(rep.saveError, sizeof(rep.saveError), "cannot open JSON file (errno %d)", errno);
            break;
        }
        const bool jsonOk = (fwrite(json.c_str(), 1, json.length(), fp) == json.length());
        if ((fclose(fp) != 0) || !jsonOk) {
            snprintf(rep.saveError, sizeof(rep.saveError), "writing the JSON failed (errno %d)", errno);
        }
        fp = nullptr;
    } while (0);

    if (fp != nullptr) {
        fclose(fp);
    }
    if (chunk != nullptr) {
        heap_caps_free(chunk);
    }
    sd_card.releaseFs();
    if (rep.saveError[0] != '\0') {
        ESP_LOGE(TAG, "Saving failed: %s", rep.saveError);
        // the path is only reported for a WAV that is really there
        struct stat st;
        if (stat(rep.wavPath, &st) != 0) {
            rep.wavPath[0] = '\0';
        }
    } else {
        ESP_LOGI(TAG, "Saved %s (+ .json)", rep.wavPath);
    }
}

void MicTest::buildResultJson(String& out, bool pretty) {
    const Report& rep = sReport;
    const mictest::Result& res = rep.result;
    out = "";

    PsramJsonDocument doc(4096);
    if (doc.capacity() == 0) {
        // Without memory ArduinoJson leaves an empty object; say so instead
        out = "{\"state\":\"error\",\"error\":\"no memory for the result\"}";
        return;
    }
    doc["state"] = "done";
    doc["test"] = rep.testId;
    doc["time"] = rep.time;
    doc["fs[Hz]"] = rep.sampleRate;
    doc["fsMeasured[Hz]"] = rep.sampleRateMeasured;
    doc["duration[ms]"] = rep.capturedMs;
    doc["i2sWasRunning"] = rep.i2sWasRunning;
    doc["micVolume2_pwr"] = rep.micVolume;
    if (rep.sampleRate > 0) {
        const uint32_t tolerance = rep.sampleRate / 20;
        if ((rep.sampleRateMeasured > rep.sampleRate + tolerance) ||
            (rep.sampleRateMeasured + tolerance < rep.sampleRate)) {
            doc["warning"] = "measured sample rate is off - I2S clock problem, the other numbers are unreliable";
        }
    }

    JsonObject level = doc.createNestedObject("level");
    level["noise[dBFS]"] = round1(res.noiseDbfs);
    level["peak[dBFS]"] = round1(res.peakDbfs);
    level["dc[dBFS]"] = round1(res.dcDbfs);
    level["clipped"] = res.clipped;
    level["stuckRun"] = res.stuckRun;

    JsonObject spectrum = doc.createNestedObject("spectrum");
    JsonArray edges = spectrum.createNestedArray("edges[Hz]");
    for (size_t b = 0; b <= mictest::NUM_BANDS; b++) {
        edges.add(static_cast<uint32_t>(res.bandEdgeHz[b] + 0.5f));
    }
    JsonArray bands = spectrum.createNestedArray("bands[dBFS]");
    for (size_t b = 0; b < mictest::NUM_BANDS; b++) {
        bands.add(round1(res.bandDbfs[b]));
    }
    spectrum["slope[dB/oct]"] = round1(res.slopeDbPerOct);

    JsonObject crackle = doc.createNestedObject("crackle");
    crackle["impulses"] = res.impulses;
    crackle["crest[dB]"] = round1(res.crestDb);
    crackle["kurtosis"] = round1(res.kurtosis);
    crackle["dropouts"] = res.dropouts;
    crackle["analysed[ms]"] = (rep.sampleRate > 0)
        ? static_cast<uint32_t>((static_cast<uint64_t>(res.offSamples) * 1000) / rep.sampleRate) : 0;

    JsonArray tones = doc.createNestedArray("tones");
    for (size_t t = 0; t < res.numTones; t++) {
        const mictest::ToneResult& tr = res.tones[t];
        JsonObject tone = tones.createNestedObject();
        tone["f[Hz]"] = tr.freqHz;
        if (!tr.valid) {
            tone["valid"] = false;
            continue;
        }
        tone["on[dBFS]"] = round1(tr.onDbfs);
        tone["off[dBFS]"] = round1(tr.offDbfs);
        tone["rise[dB]"] = round1(tr.riseDb);
        tone["riseMin[dB]"] = round1(tr.riseMinDb);
        tone["peak[Hz]"] = static_cast<uint32_t>(tr.peakHz + 0.5f);
        tone["onTotal[dBFS]"] = round1(tr.onTotalDbfs);
    }
    if (rep.numSkipped > 0) {
        JsonArray skipped = doc.createNestedArray("skipped[Hz]");
        for (uint8_t i = 0; i < rep.numSkipped; i++) {
            skipped.add(rep.skipped[i]);
        }
    }

    doc["wav"] = rep.wavPath;
    if (rep.saveError[0] != '\0') {
        doc["saveError"] = rep.saveError;
    }

    if (doc.overflowed()) {
        ESP_LOGE(TAG, "Result JSON exceeded its %u byte document - fields are missing!",
                 static_cast<unsigned>(doc.capacity()));
    }
    if (pretty) {
        serializeJsonPretty(doc, out);
    } else {
        serializeJson(doc, out);
    }
}

void MicTest::statusJson(String& out) {
    const State s = mState.load();
    switch (s) {
        case State::done:
            buildResultJson(out, false);
            return;
        case State::error: {
            StaticJsonDocument<384> doc;
            doc["state"] = stateName(s);
            doc["test"] = sReport.testId;
            doc["error"] = sReport.error;
            out = "";
            serializeJson(doc, out);
            return;
        }
        case State::idle:
            out = "{\"state\":\"idle\"}";
            return;
        default:
            break;
    }
    // running
    const uint32_t elapsedMs = static_cast<uint32_t>((esp_timer_get_time() - mStartUs) / 1000);
    uint32_t progress = (mPlannedMs > 0) ? ((elapsedMs * 100) / mPlannedMs) : 0;
    if (progress > 99) {
        progress = 99;
    }
    out = "{\"state\":\"";
    out += stateName(s);
    out += "\",\"progress[%]\":";
    out += progress;
    out += ",\"remaining[ms]\":";
    out += (elapsedMs < mPlannedMs) ? (mPlannedMs - elapsedMs) : 0;
    out += "}";
}
