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

#ifndef COMMANDS_MICTEST_HPP_
#define COMMANDS_MICTEST_HPP_

#include <stdint.h>
#include <stddef.h>
#include <atomic>

#include <esp_err.h>
#include "WString.h"

class I2SMEMSSampler;

/**
 * Microphone self-test (BT commands setMicTest / getMicTest, see README-MicTest-Plan.md).
 *
 * Plays tones on the on-board buzzer, records what the microphone hears and reports raw numbers:
 * levels, an octave spectrum, crackle statistics and how far each tone rose above the background.
 * No verdict yet - the thresholds are to be found on real broken microphones.
 *
 * One test runs in its own short-lived task (about 10 s): it only copies samples while the
 * buzzer plays, analyses them afterwards (lib/audio_input/src/MicAnalyzer.hpp), and can save the
 * capture as a WAV plus the result as JSON to /sdcard/mictest/.
 */
class MicTest {
public:
    static const size_t MAX_FREQS = 6;

    enum class State : uint8_t { idle, starting, capturing, analysing, saving, done, error };

    struct Params {
        uint16_t freqs[MAX_FREQS];  ///< buzzer tones in Hz
        uint8_t  numFreqs;          ///< 0 = silent test, the buzzer is not used
        uint8_t  reps;              ///< how often the tone list is played
        uint16_t onMs;              ///< length of each tone
        uint16_t gapMs;             ///< silence after each tone
        uint16_t silenceMs;         ///< silence before the first tone
        bool     save;              ///< write WAV + JSON to the SD card
        Params();                   ///< the defaults: 1000/2700/4000 Hz, twice, 600 ms on, 400 ms off
    };

    inline static MicTest& GetInstance() {
        static MicTest instance;
        return instance;
    }

    /// @brief Tell the test which sampler delivers the microphone data. Called once from main.
    void setSampler(I2SMEMSSampler* sampler);

    /// @brief Start a test. Returns at once; poll statusJson() for progress and the result.
    /// @param errMsg why the test was refused
    /// @return ESP_OK when the test task is running
    esp_err_t start(const Params& params, String& errMsg);

    /// @brief State, progress while running, and the last result or error.
    void statusJson(String& out);

    /// @brief Rough time until the result is ready, for the reply to setMicTest.
    uint32_t plannedDurationMs() const {
        return mPlannedMs;
    }

    /// @brief True while the test needs microphone data. The main loop starts I2S on this exactly
    ///        as it does for a recording, so the driver keeps a single owner.
    bool needsI2S() const {
        return mNeedsI2S.load();
    }

    /// @brief True from the accepted start until the result (or error) is ready.
    bool isBusy() const;

private:
    MicTest() {}

    static void taskEntry(void* arg);
    void run();
    void fail(const char* msg);
    void cleanup();

    static void tapEntry(const int32_t* samples, size_t count, void* ctx);
    void onSamples(const int32_t* samples, size_t count);

    void buildResultJson(String& out, bool pretty);
    void saveFiles(uint32_t sampleCount);

    I2SMEMSSampler* mSampler = nullptr;
    std::atomic<State> mState{State::idle};
    std::atomic<bool> mNeedsI2S{false};
    Params mParams;
    uint32_t mTestCount = 0;
    uint32_t mPlannedMs = 0;
    int64_t mStartUs = 0;
    bool mBuzzerHeld = false;

    // Capture. The buffer is filled by the I2S read task; everything else is the test task's.
    int32_t* mBuf = nullptr;
    uint32_t mCapacity = 0;
    void* mWorkspace = nullptr;
    std::atomic<uint32_t> mWritten{0};
    std::atomic<bool> mCapturing{false};
    std::atomic<uint32_t> mTapCalls{0};
    int64_t mFirstBlockUs = 0;
    uint32_t mFirstBlockEnd = 0;
    int64_t mLastBlockUs = 0;
    uint32_t mLastBlockEnd = 0;
};

#endif  // COMMANDS_MICTEST_HPP_
