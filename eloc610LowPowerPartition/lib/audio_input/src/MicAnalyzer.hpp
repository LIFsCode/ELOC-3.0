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

#ifndef MICANALYZER_HPP_
#define MICANALYZER_HPP_

/**
 * Analysis behind the microphone self-test (setMicTest / getMicTest, README-MicTest-Plan.md).
 *
 * Takes one finished capture plus the list of when the buzzer was on, and returns raw numbers:
 * levels, an octave spectrum of the buzzer-off time, crackle statistics and, per buzzer tone, how
 * far the level at that pitch rose while the buzzer sounded. It gives no verdict - the thresholds
 * are still to be found on real broken microphones.
 *
 * Header-only and free of ESP/Arduino code on purpose, so test/test_generic_mic_analyzer can run
 * it on the PC. Samples are signed 24-bit values (the raw I2S word shifted right by 8), i.e.
 * before the MicVolume2_pwr gain shift.
 *
 * Level convention: 0 dBFS is a full-scale sine, as in the microphone datasheets. For the
 * ICS-43434 (-26 dBFS at 94 dB SPL) that makes dB SPL = dBFS + 120.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <algorithm>

namespace mictest {

static const size_t FFT_N          = 1024;           ///< analysis block; 64 ms at 16 kHz
static const size_t FFT_HALF       = FFT_N / 2;
static const size_t MAX_TONES      = 6;
static const size_t MAX_REPS       = 5;
static const size_t MAX_MARKS      = MAX_TONES * MAX_REPS;
static const size_t MAX_SEGMENTS   = 2 * MAX_MARKS + 1;
static const size_t NUM_BANDS      = 8;              ///< octaves: bins [2,4) [4,8) .. [256,512)
static const size_t TONE_HALF_MAX  = 32;             ///< tone band is at most +-32 bins
static const size_t TONE_BINS_MAX  = 2 * TONE_HALF_MAX + 1;
static const size_t RMS_SUBBLOCK   = 256;            ///< crackle reference block; 16 ms at 16 kHz
static const size_t MAX_RMS_BLOCKS = 1024;
static const size_t MAX_OFF_BLOCKS = 512;            ///< buzzer-off blocks kept per tone for the median
static const size_t MAX_ON_BLOCKS  = 128;            ///< buzzer-on blocks kept per repetition

static const float FULL_SCALE      = 8388608.0f;     ///< 2^23
static const float DB_FLOOR        = -160.0f;        ///< reported instead of -infinity
static const float TONE_TOLERANCE  = 0.05f;          ///< tone band = +-5 % around the nominal pitch
static const float IMPULSE_FACTOR  = 8.0f;           ///< a step this many times the typical one is a crackle
static const float DROPOUT_FACTOR  = 0.1f;           ///< a sub-block this far below the typical one is a dropout

/**
 * How much of the capture to skip around the buzzer switches, in ms. Measured on ELOC_00247
 * (2026-11-17, README-MicTest-Plan.md "Bench results"): a switch is heard 64-90 ms after its mark,
 * and after switching off the sound takes until about 240 ms after the mark to fade into the
 * background. With 200 ms after switch-off the fading tone counted as crackle - 3 to 24 false
 * impulses depending on the I2S read phase. 300 ms gave none at any phase; 350 keeps a margin.
 */
static const uint32_t GUARD_ON_MS  = 200;            ///< skipped after the buzzer is switched on
static const uint32_t GUARD_OFF_MS = 350;            ///< skipped after the buzzer is switched off
static const uint32_t LEAD_SKIP_MS = 100;            ///< skipped at the very start of the capture

/// One buzzer tone as the test task played it. Sample indices are the capture's write position
/// at the moment the buzzer was switched, so they run ahead of the sound (64-90 ms on the bench).
struct Mark {
    uint32_t onIdx;
    uint32_t offIdx;
    uint8_t  tone;      ///< index into Input::toneHz
};

/// A stretch of the capture that is safe to analyse. tone < 0: buzzer off.
struct Segment {
    uint32_t begin;
    uint32_t end;       ///< exclusive
    int8_t   tone;
};

struct Input {
    const int32_t*  samples;       ///< signed 24-bit values
    uint32_t        count;
    uint32_t        sampleRate;
    const Segment*  segments;
    size_t          numSegments;
    const uint16_t* toneHz;
    size_t          numTones;
};

struct ToneResult {
    uint16_t freqHz;
    bool     valid;          ///< false: no usable buzzer-on block for this tone
    uint8_t  reps;           ///< buzzer-on segments that went into the result
    float    onDbfs;         ///< level in the tone band, buzzer on (median block, mean of the repetitions)
    float    offDbfs;        ///< level in the same band, buzzer off (median block)
    float    riseDb;         ///< on - off
    float    riseMinDb;      ///< lowest single repetition
    float    peakHz;         ///< strongest bin inside the tone band, buzzer on
    float    onTotalDbfs;    ///< broadband level while this tone played
};

struct Result {
    // whole capture
    float    peakDbfs;
    uint32_t clipped;        ///< samples at the 24-bit rails
    uint32_t stuckRun;       ///< longest run of identical consecutive samples

    // buzzer-off time
    float    noiseDbfs;      ///< broadband level, DC removed
    float    dcDbfs;
    uint32_t offSamples;
    uint32_t offBlocks;      ///< spectrum blocks averaged
    float    bandDbfs[NUM_BANDS];
    float    bandEdgeHz[NUM_BANDS + 1];
    float    slopeDbPerOct;  ///< fit through bands 1..7; pink noise = 0, white noise = +3

    // crackle, buzzer-off time, on sample-to-sample steps
    uint32_t impulses;
    float    crestDb;        ///< largest step over the typical step
    float    kurtosis;       ///< excess kurtosis of the steps; plain noise = 0
    uint32_t dropouts;

    ToneResult tones[MAX_TONES];
    size_t     numTones;
};

/// Scratch memory for analyze(), ~37 KB. The caller owns it so the device can put it in PSRAM.
struct Workspace {
    float re[FFT_N];
    float im[FFT_N];
    float window[FFT_N];
    float twRe[FFT_HALF];
    float twIm[FFT_HALF];
    float offPower[FFT_HALF + 1];
    float tonePower[MAX_TONES][TONE_BINS_MAX];
    float toneOff[MAX_TONES][MAX_OFF_BLOCKS];
    float onBlocks[MAX_ON_BLOCKS];
    float subRms[MAX_RMS_BLOCKS];
};

/// Level of a mean-square power, 0 dBFS = full-scale sine (mean square 0.5).
inline float powerToDbfs(double meanSquare) {
    const double v = 2.0 * meanSquare;
    if (!(v > 1e-16)) {
        return DB_FLOOR;
    }
    return static_cast<float>(10.0 * log10(v));
}

inline float amplitudeToDb(double amplitude) {
    if (!(amplitude > 1e-8)) {
        return DB_FLOOR;
    }
    return static_cast<float>(20.0 * log10(amplitude));
}

/// A tone is only usable well below the Nyquist frequency and above the first octave band.
inline bool toneUsable(uint32_t freqHz, uint32_t sampleRate) {
    return (freqHz >= 100) && (static_cast<float>(freqHz) <= 0.45f * static_cast<float>(sampleRate));
}

/**
 * @brief Turn the buzzer switch marks into the stretches that are safe to analyse.
 *
 * Every stretch starts a guard time after its switch and ends at the next switch, where the
 * samples already captured are all from before it. The guard covers the capture latency plus the
 * buzzer settling, and after switching off also the tone fading away, which takes longer.
 *
 * @param endIdx   number of captured samples
 * @param guardOn  samples to skip after the buzzer is switched on
 * @param guardOff samples to skip after the buzzer is switched off
 * @param leadSkip samples to skip at the very start of the capture
 * @return number of segments written
 */
inline size_t buildSegments(const Mark* marks, size_t numMarks, uint32_t endIdx, uint32_t guardOn,
                            uint32_t guardOff, uint32_t leadSkip, Segment* out, size_t maxOut) {
    size_t n = 0;
    uint32_t offBegin = leadSkip;
    for (size_t i = 0; i <= numMarks; i++) {
        // buzzer-off stretch in front of mark i (or, after the last mark, to the end)
        uint32_t offEnd = (i < numMarks) ? marks[i].onIdx : endIdx;
        if (offEnd > endIdx) {
            offEnd = endIdx;
        }
        if ((offBegin < offEnd) && (n < maxOut)) {
            out[n].begin = offBegin;
            out[n].end = offEnd;
            out[n].tone = -1;
            n++;
        }
        if (i == numMarks) {
            break;
        }
        uint32_t onBegin = marks[i].onIdx + guardOn;
        uint32_t onEnd = (marks[i].offIdx < endIdx) ? marks[i].offIdx : endIdx;
        if ((onBegin < onEnd) && (n < maxOut)) {
            out[n].begin = onBegin;
            out[n].end = onEnd;
            out[n].tone = static_cast<int8_t>(marks[i].tone);
            n++;
        }
        offBegin = marks[i].offIdx + guardOff;
    }
    return n;
}

namespace detail {

inline void initTables(Workspace& ws) {
    const double twoPi = 6.283185307179586476925286766559;
    for (size_t n = 0; n < FFT_N; n++) {
        // periodic Hann
        ws.window[n] = static_cast<float>(0.5 - 0.5 * cos(twoPi * static_cast<double>(n) / FFT_N));
    }
    for (size_t k = 0; k < FFT_HALF; k++) {
        const double a = -twoPi * static_cast<double>(k) / FFT_N;
        ws.twRe[k] = static_cast<float>(cos(a));
        ws.twIm[k] = static_cast<float>(sin(a));
    }
}

/// In-place radix-2 FFT of ws.re / ws.im (FFT_N points).
inline void fft(Workspace& ws) {
    float* re = ws.re;
    float* im = ws.im;
    for (size_t i = 1, j = 0; i < FFT_N; i++) {
        size_t bit = FFT_N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (size_t len = 2; len <= FFT_N; len <<= 1) {
        const size_t half = len >> 1;
        const size_t step = FFT_N / len;
        for (size_t i = 0; i < FFT_N; i += len) {
            for (size_t k = 0; k < half; k++) {
                const float wRe = ws.twRe[k * step];
                const float wIm = ws.twIm[k * step];
                const size_t a = i + k;
                const size_t b = a + half;
                const float vRe = re[b] * wRe - im[b] * wIm;
                const float vIm = re[b] * wIm + im[b] * wRe;
                re[b] = re[a] - vRe;
                im[b] = im[a] - vIm;
                re[a] += vRe;
                im[a] += vIm;
            }
        }
    }
}

/// Load one block, remove its mean, window it and transform it.
/// @return mean square of the block with the mean removed (before the window)
inline double blockSpectrum(Workspace& ws, const int32_t* block) {
    double sum = 0.0;
    for (size_t n = 0; n < FFT_N; n++) {
        sum += block[n];
    }
    const float mean = static_cast<float>(sum / FFT_N);
    double sq = 0.0;
    for (size_t n = 0; n < FFT_N; n++) {
        const float x = (static_cast<float>(block[n]) - mean) / FULL_SCALE;
        sq += static_cast<double>(x) * x;
        ws.re[n] = x * ws.window[n];
        ws.im[n] = 0.0f;
    }
    fft(ws);
    return sq / FFT_N;
}

inline float binPower(const Workspace& ws, size_t k) {
    return ws.re[k] * ws.re[k] + ws.im[k] * ws.im[k];
}

/// Median of `count` values; reorders them.
inline float median(float* values, size_t count) {
    if (count == 0) {
        return 0.0f;
    }
    std::sort(values, values + count);
    if (count & 1) {
        return values[count / 2];
    }
    return 0.5f * (values[count / 2 - 1] + values[count / 2]);
}

/// Bins that make up the band of one tone.
inline void toneBand(uint32_t freqHz, uint32_t sampleRate, size_t& lo, size_t& hi) {
    const float k0f = static_cast<float>(freqHz) * FFT_N / static_cast<float>(sampleRate);
    size_t k0 = static_cast<size_t>(k0f + 0.5f);
    size_t half = static_cast<size_t>(ceilf(TONE_TOLERANCE * k0f));
    if (half < 3) {
        half = 3;
    }
    if (half > TONE_HALF_MAX) {
        half = TONE_HALF_MAX;
    }
    if (k0 > FFT_HALF - 1) {
        k0 = FFT_HALF - 1;
    }
    lo = (k0 > half + 1) ? (k0 - half) : 1;
    hi = k0 + half;
    if (hi > FFT_HALF - 1) {
        hi = FFT_HALF - 1;
    }
}

}  // namespace detail

/**
 * @brief Analyse one capture.
 * @return false when there is nothing to analyse (no samples, capture shorter than one block);
 *         `out` then holds the floor values.
 */
inline bool analyze(const Input& in, Workspace& ws, Result& out) {
    memset(&out, 0, sizeof(out));
    out.peakDbfs = DB_FLOOR;
    out.noiseDbfs = DB_FLOOR;
    out.dcDbfs = DB_FLOOR;
    for (size_t b = 0; b < NUM_BANDS; b++) {
        out.bandDbfs[b] = DB_FLOOR;
    }
    out.numTones = (in.numTones < MAX_TONES) ? in.numTones : MAX_TONES;
    for (size_t t = 0; t < out.numTones; t++) {
        out.tones[t].freqHz = in.toneHz[t];
        out.tones[t].onDbfs = DB_FLOOR;
        out.tones[t].offDbfs = DB_FLOOR;
        out.tones[t].onTotalDbfs = DB_FLOOR;
    }
    if ((in.samples == NULL) || (in.count < FFT_N) || (in.sampleRate == 0)) {
        return false;
    }

    detail::initTables(ws);
    memset(ws.offPower, 0, sizeof(ws.offPower));
    memset(ws.tonePower, 0, sizeof(ws.tonePower));

    const float fs = static_cast<float>(in.sampleRate);
    const float binHz = fs / FFT_N;
    for (size_t b = 0; b <= NUM_BANDS; b++) {
        out.bandEdgeHz[b] = binHz * static_cast<float>(static_cast<size_t>(2) << b);
    }

    double sumW2 = 0.0;
    for (size_t n = 0; n < FFT_N; n++) {
        sumW2 += static_cast<double>(ws.window[n]) * ws.window[n];
    }
    // Mean-square power of a band = specNorm * (sum of |X[k]|^2 over its one-sided bins). Follows
    // from Parseval, so it holds for noise and for a tone wherever it sits inside the band.
    const double specNorm = 2.0 / (static_cast<double>(FFT_N) * sumW2);

    // ---- whole capture: peak, clipping, stuck samples --------------------------------------
    {
        int32_t maxAbs = 0;
        uint32_t run = 1;
        uint32_t longest = 1;
        for (uint32_t n = 0; n < in.count; n++) {
            const int32_t s = in.samples[n];
            const int32_t a = (s < 0) ? -(s + 1) : s;   // -2^23 maps to 2^23-1, no overflow
            if (a > maxAbs) {
                maxAbs = a;
            }
            if ((s >= 8388607) || (s <= -8388608)) {
                out.clipped++;
            }
            if ((n > 0) && (s == in.samples[n - 1])) {
                run++;
                if (run > longest) {
                    longest = run;
                }
            } else {
                run = 1;
            }
        }
        out.peakDbfs = amplitudeToDb(static_cast<double>(maxAbs) / FULL_SCALE);
        out.stuckRun = longest;
    }

    // ---- buzzer-off time: level and DC ------------------------------------------------------
    {
        double sum = 0.0;
        double sumSq = 0.0;
        uint32_t cnt = 0;
        for (size_t i = 0; i < in.numSegments; i++) {
            const Segment& seg = in.segments[i];
            if ((seg.tone >= 0) || (seg.end > in.count)) {
                continue;
            }
            for (uint32_t n = seg.begin; n < seg.end; n++) {
                const double x = static_cast<double>(in.samples[n]) / FULL_SCALE;
                sum += x;
                sumSq += x * x;
            }
            cnt += seg.end - seg.begin;
        }
        out.offSamples = cnt;
        if (cnt > 0) {
            const double mean = sum / cnt;
            double var = sumSq / cnt - mean * mean;
            if (var < 0.0) {
                var = 0.0;
            }
            out.noiseDbfs = powerToDbfs(var);
            out.dcDbfs = amplitudeToDb(fabs(mean));
        }
    }

    // ---- buzzer-off time: spectrum -----------------------------------------------------------
    size_t toneLo[MAX_TONES];
    size_t toneHi[MAX_TONES];
    for (size_t t = 0; t < out.numTones; t++) {
        detail::toneBand(out.tones[t].freqHz, in.sampleRate, toneLo[t], toneHi[t]);
    }
    for (size_t i = 0; i < in.numSegments; i++) {
        const Segment& seg = in.segments[i];
        if ((seg.tone >= 0) || (seg.end > in.count)) {
            continue;
        }
        for (uint32_t start = seg.begin; start + FFT_N <= seg.end; start += FFT_HALF) {
            detail::blockSpectrum(ws, in.samples + start);
            for (size_t k = 0; k <= FFT_HALF; k++) {
                ws.offPower[k] += detail::binPower(ws, k);
            }
            // Per block, the power in each tone's band: the background the tones are compared to
            if (out.offBlocks < MAX_OFF_BLOCKS) {
                for (size_t t = 0; t < out.numTones; t++) {
                    float p = 0.0f;
                    for (size_t k = toneLo[t]; k <= toneHi[t]; k++) {
                        p += detail::binPower(ws, k);
                    }
                    ws.toneOff[t][out.offBlocks] = p;
                }
            }
            out.offBlocks++;
        }
    }
    if (out.offBlocks > 0) {
        for (size_t b = 0; b < NUM_BANDS; b++) {
            const size_t lo = static_cast<size_t>(2) << b;
            const size_t hi = static_cast<size_t>(4) << b;   // exclusive
            double p = 0.0;
            for (size_t k = lo; k < hi; k++) {
                p += ws.offPower[k];
            }
            out.bandDbfs[b] = powerToDbfs(specNorm * p / out.offBlocks);
        }
        // Least-squares line through bands 1..7. The lowest band has only two bins and sits below
        // the microphone's own high-pass, so it is left out.
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        int m = 0;
        for (size_t b = 1; b < NUM_BANDS; b++) {
            sx += b;
            sy += out.bandDbfs[b];
            sxx += static_cast<double>(b) * b;
            sxy += static_cast<double>(b) * out.bandDbfs[b];
            m++;
        }
        const double den = m * sxx - sx * sx;
        if (den != 0.0) {
            out.slopeDbPerOct = static_cast<float>((m * sxy - sx * sy) / den);
        }
    }

    // ---- buzzer-off time: crackle ------------------------------------------------------------
    // Works on the step from one sample to the next. That removes DC and most low-frequency
    // ambient sound (and flattens pink noise), so short spikes stand out.
    {
        const float lsb = 1.0f / FULL_SCALE;
        size_t numSub = 0;
        double sumD2 = 0.0;
        double sumD4 = 0.0;
        uint32_t cntD = 0;
        float maxAbsD = 0.0f;
        for (size_t i = 0; i < in.numSegments; i++) {
            const Segment& seg = in.segments[i];
            if ((seg.tone >= 0) || (seg.end > in.count) || (seg.end - seg.begin < 2)) {
                continue;
            }
            double subSq = 0.0;
            size_t subCnt = 0;
            for (uint32_t n = seg.begin + 1; n < seg.end; n++) {
                const float d = static_cast<float>(in.samples[n] - in.samples[n - 1]) / FULL_SCALE;
                const double d2 = static_cast<double>(d) * d;
                sumD2 += d2;
                sumD4 += d2 * d2;
                cntD++;
                const float ad = fabsf(d);
                if (ad > maxAbsD) {
                    maxAbsD = ad;
                }
                subSq += d2;
                if (++subCnt == RMS_SUBBLOCK) {
                    if (numSub < MAX_RMS_BLOCKS) {
                        ws.subRms[numSub++] = static_cast<float>(sqrt(subSq / RMS_SUBBLOCK));
                    }
                    subSq = 0.0;
                    subCnt = 0;
                }
            }
        }
        if (cntD > 0) {
            const double m2 = sumD2 / cntD;
            const double m4 = sumD4 / cntD;
            out.kurtosis = (m2 > 0.0) ? static_cast<float>(m4 / (m2 * m2) - 3.0) : 0.0f;
        }
        float typical = 0.0f;
        if (numSub > 0) {
            // The median sub-block is the reference, so a few loud bursts cannot raise it.
            std::sort(ws.subRms, ws.subRms + numSub);
            typical = ws.subRms[numSub / 2];
            if (typical > 2.0f * lsb) {
                for (size_t i = 0; i < numSub; i++) {
                    if (ws.subRms[i] < DROPOUT_FACTOR * typical) {
                        out.dropouts++;
                    }
                }
            }
        } else if (cntD > 0) {
            typical = static_cast<float>(sqrt(sumD2 / cntD));
        }
        // A constant signal has no typical step at all; one LSB keeps the ratio finite.
        const float ref = (typical > lsb) ? typical : lsb;
        out.crestDb = (maxAbsD > 0.0f) ? static_cast<float>(20.0 * log10(maxAbsD / ref)) : 0.0f;

        const float threshold = IMPULSE_FACTOR * ref;
        uint32_t refractory = in.sampleRate / 500;   // 2 ms: one crackle, however many samples
        if (refractory < 1) {
            refractory = 1;
        }
        for (size_t i = 0; i < in.numSegments; i++) {
            const Segment& seg = in.segments[i];
            if ((seg.tone >= 0) || (seg.end > in.count) || (seg.end - seg.begin < 2)) {
                continue;
            }
            bool haveHit = false;
            uint32_t lastHit = 0;
            for (uint32_t n = seg.begin + 1; n < seg.end; n++) {
                const float d = static_cast<float>(in.samples[n] - in.samples[n - 1]) / FULL_SCALE;
                if (fabsf(d) > threshold) {
                    if (!haveHit || ((n - lastHit) > refractory)) {
                        out.impulses++;
                    }
                    haveHit = true;
                    lastHit = n;
                }
            }
        }
    }

    // ---- tones -------------------------------------------------------------------------------
    // Both sides of the comparison use the MEDIAN block, not the mean. A few crackles or one bird
    // call then move neither the buzzer-on level nor the background. With the mean on one side
    // only, a deaf but crackling microphone would show a rise it does not have.
    const size_t offStored = (out.offBlocks < MAX_OFF_BLOCKS) ? out.offBlocks : MAX_OFF_BLOCKS;
    for (size_t t = 0; t < out.numTones; t++) {
        ToneResult& tone = out.tones[t];
        const size_t lo = toneLo[t];
        const size_t hi = toneHi[t];

        const double pOff = specNorm * detail::median(ws.toneOff[t], offStored);
        tone.offDbfs = powerToDbfs(pOff);

        const double eps = 1e-20;
        double sumOn = 0.0;
        double sumTotal = 0.0;
        uint32_t totalBlocks = 0;
        float riseMin = 0.0f;
        for (size_t i = 0; i < in.numSegments; i++) {
            const Segment& seg = in.segments[i];
            if ((seg.tone != static_cast<int8_t>(t)) || (seg.end > in.count)) {
                continue;
            }
            size_t segBlocks = 0;
            for (uint32_t start = seg.begin; start + FFT_N <= seg.end; start += FFT_HALF) {
                const double blockTotal = detail::blockSpectrum(ws, in.samples + start);
                float band = 0.0f;
                for (size_t k = lo; k <= hi; k++) {
                    const float p = detail::binPower(ws, k);
                    band += p;
                    ws.tonePower[t][k - lo] += p;
                }
                if (segBlocks < MAX_ON_BLOCKS) {
                    ws.onBlocks[segBlocks++] = band;
                }
                sumTotal += blockTotal;
                totalBlocks++;
            }
            if (segBlocks == 0) {
                continue;
            }
            const double pOnSeg = specNorm * detail::median(ws.onBlocks, segBlocks);
            sumOn += pOnSeg;
            const float riseSeg = static_cast<float>(10.0 * log10((pOnSeg + eps) / (pOff + eps)));
            if ((tone.reps == 0) || (riseSeg < riseMin)) {
                riseMin = riseSeg;
            }
            tone.reps++;
        }
        if (tone.reps == 0) {
            continue;
        }
        const double pOn = sumOn / tone.reps;
        tone.valid = true;
        tone.onDbfs = powerToDbfs(pOn);
        tone.riseDb = static_cast<float>(10.0 * log10((pOn + eps) / (pOff + eps)));
        tone.riseMinDb = riseMin;
        tone.onTotalDbfs = powerToDbfs(sumTotal / totalBlocks);
        size_t best = lo;
        for (size_t k = lo; k <= hi; k++) {
            if (ws.tonePower[t][k - lo] > ws.tonePower[t][best - lo]) {
                best = k;
            }
        }
        tone.peakHz = static_cast<float>(best) * binHz;
    }

    return true;
}

}  // namespace mictest

#endif  // MICANALYZER_HPP_
