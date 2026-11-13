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

// Native unit test for the microphone self-test analysis
// (lib/audio_input/src/MicAnalyzer.hpp). Run with:
//   pio test -e generic_unit_tests -f test_generic_mic_analyzer
//
// Every case synthesises a capture the way the device records one: silence, then buzzer tones
// separated by gaps, with the sound arriving a little after the switch marks.

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "unity.h"
#include "../../lib/audio_input/src/MicAnalyzer.hpp"

using namespace mictest;

namespace {

const double kTwoPi = 6.283185307179586476925286766559;

struct Rng {
    uint32_t state;
    explicit Rng(uint32_t seed) : state(seed) {}
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double uniform() {  // (0, 1]
        return (static_cast<double>(next()) + 1.0) / 4294967296.0;
    }
    double gauss() {
        return sqrt(-2.0 * log(uniform())) * cos(kTwoPi * uniform());
    }
};

/// Paul Kellet's pink-noise filter: equal power per octave from white noise.
struct PinkFilter {
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double step(double white) {
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        return pink;
    }
};

struct Scenario {
    uint32_t sampleRate = 16000;
    std::vector<uint16_t> tones = {1000, 2700, 4000};
    uint32_t reps = 2;
    uint32_t silenceMs = 1500;
    uint32_t onMs = 600;
    uint32_t gapMs = 400;
    uint32_t tailMs = 300;
    uint32_t latencyMs = 80;     // the sound reaches the capture this long after the mark
    double noiseRms = 1e-4;      // of full scale
    bool pinkNoise = false;
    double toneAmplitude = 0.01; // of full scale; 0 = a mic that hears nothing
    double toneDetune = 1.0;     // actual pitch / nominal pitch
    double dc = 0.0;
    uint32_t seed = 12345;
};

struct Capture {
    std::vector<int32_t> samples;
    std::vector<Mark> marks;
    std::vector<Segment> segments;
    uint32_t sampleRate = 0;
    std::vector<uint16_t> tones;
};

uint32_t msToSamples(uint32_t ms, uint32_t sampleRate) {
    return static_cast<uint32_t>((static_cast<uint64_t>(ms) * sampleRate) / 1000);
}

int32_t toS24(double x) {
    double v = floor(x * 8388608.0 + 0.5);
    if (v > 8388607.0) v = 8388607.0;
    if (v < -8388608.0) v = -8388608.0;
    return static_cast<int32_t>(v);
}

void rebuildSegments(Capture& cap) {
    cap.segments.assign(MAX_SEGMENTS, Segment());
    const size_t n = buildSegments(cap.marks.data(), cap.marks.size(),
                                   static_cast<uint32_t>(cap.samples.size()),
                                   msToSamples(200, cap.sampleRate), msToSamples(100, cap.sampleRate),
                                   cap.segments.data(), cap.segments.size());
    cap.segments.resize(n);
}

Capture makeCapture(const Scenario& sc) {
    Capture cap;
    cap.sampleRate = sc.sampleRate;
    cap.tones = sc.tones;
    const uint32_t fs = sc.sampleRate;
    const uint32_t totalMs = sc.silenceMs + sc.reps * sc.tones.size() * (sc.onMs + sc.gapMs) + sc.tailMs;
    const uint32_t total = msToSamples(totalMs, fs);

    std::vector<double> x(total, 0.0);
    Rng rng(sc.seed);
    PinkFilter pink;
    // normalise the pink filter's output to unit RMS (measured once, it is ~3.1 for this filter)
    double pinkGain = 1.0;
    if (sc.pinkNoise) {
        Rng probe(999);
        PinkFilter pf;
        double sq = 0.0;
        for (int i = 0; i < 200000; i++) {
            const double v = pf.step(probe.gauss());
            if (i >= 20000) sq += v * v;
        }
        pinkGain = 1.0 / sqrt(sq / 180000.0);
        for (int i = 0; i < 20000; i++) pink.step(rng.gauss());  // settle
    }
    for (uint32_t n = 0; n < total; n++) {
        const double w = rng.gauss();
        x[n] = sc.dc + sc.noiseRms * (sc.pinkNoise ? pinkGain * pink.step(w) : w);
    }

    uint32_t t = msToSamples(sc.silenceMs, fs);
    const uint32_t latency = msToSamples(sc.latencyMs, fs);
    for (uint32_t r = 0; r < sc.reps; r++) {
        for (size_t i = 0; i < sc.tones.size(); i++) {
            Mark m;
            m.onIdx = t;
            m.offIdx = t + msToSamples(sc.onMs, fs);
            m.tone = static_cast<uint8_t>(i);
            cap.marks.push_back(m);
            const double f = sc.tones[i] * sc.toneDetune;
            for (uint32_t n = m.onIdx + latency; (n < m.offIdx + latency) && (n < total); n++) {
                x[n] += sc.toneAmplitude * sin(kTwoPi * f * n / fs);
            }
            t = m.offIdx + msToSamples(sc.gapMs, fs);
        }
    }

    cap.samples.resize(total);
    for (uint32_t n = 0; n < total; n++) {
        cap.samples[n] = toS24(x[n]);
    }
    rebuildSegments(cap);
    return cap;
}

Workspace gWorkspace;

bool run(const Capture& cap, Result& res) {
    Input in;
    in.samples = cap.samples.data();
    in.count = static_cast<uint32_t>(cap.samples.size());
    in.sampleRate = cap.sampleRate;
    in.segments = cap.segments.data();
    in.numSegments = cap.segments.size();
    in.toneHz = cap.tones.data();
    in.numTones = cap.tones.size();
    return analyze(in, gWorkspace, res);
}

void assertAllFinite(const Result& r) {
    TEST_ASSERT_TRUE(isfinite(r.peakDbfs));
    TEST_ASSERT_TRUE(isfinite(r.noiseDbfs));
    TEST_ASSERT_TRUE(isfinite(r.dcDbfs));
    TEST_ASSERT_TRUE(isfinite(r.slopeDbPerOct));
    TEST_ASSERT_TRUE(isfinite(r.crestDb));
    TEST_ASSERT_TRUE(isfinite(r.kurtosis));
    for (size_t b = 0; b < NUM_BANDS; b++) {
        TEST_ASSERT_TRUE(isfinite(r.bandDbfs[b]));
    }
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(isfinite(r.tones[t].onDbfs));
        TEST_ASSERT_TRUE(isfinite(r.tones[t].offDbfs));
        TEST_ASSERT_TRUE(isfinite(r.tones[t].riseDb));
        TEST_ASSERT_TRUE(isfinite(r.tones[t].riseMinDb));
        TEST_ASSERT_TRUE(isfinite(r.tones[t].onTotalDbfs));
    }
}

}  // namespace

void setUp(void) {
}

void tearDown(void) {
}

// A working mic: white noise at -77 dBFS, every tone arrives at -40 dBFS.
void test_healthy_mic(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);

    TEST_ASSERT_FLOAT_WITHIN(0.5f, -76.99f, r.noiseDbfs);   // 10*log10(2 * 1e-8)
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -40.0f, r.peakDbfs);
    TEST_ASSERT_EQUAL_UINT32(0, r.clipped);
    TEST_ASSERT_TRUE(r.stuckRun <= 3);
    TEST_ASSERT_TRUE(r.dcDbfs < -100.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.01f, r.slopeDbPerOct);  // white noise: +3 dB per octave
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 31.25f, r.bandEdgeHz[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 8000.0f, r.bandEdgeHz[NUM_BANDS]);

    TEST_ASSERT_EQUAL_UINT32(0, r.impulses);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropouts);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 0.0f, r.kurtosis);
    TEST_ASSERT_TRUE((r.crestDb > 9.0f) && (r.crestDb < 16.0f));

    TEST_ASSERT_EQUAL(3, r.numTones);
    for (size_t t = 0; t < r.numTones; t++) {
        const ToneResult& tone = r.tones[t];
        TEST_ASSERT_TRUE(tone.valid);
        TEST_ASSERT_EQUAL_UINT8(2, tone.reps);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, -40.0f, tone.onDbfs);
        TEST_ASSERT_TRUE(tone.riseDb > 45.0f);
        TEST_ASSERT_FLOAT_WITHIN(3.0f, tone.riseDb, tone.riseMinDb);
        TEST_ASSERT_FLOAT_WITHIN(20.0f, static_cast<float>(sc.tones[t]), tone.peakHz);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, -40.0f, tone.onTotalDbfs);
    }
}

// "Only pink noise": the mic outputs noise and does not hear the buzzer at all.
void test_deaf_mic_pink_noise(void) {
    Scenario sc;
    sc.pinkNoise = true;
    sc.noiseRms = 1e-3;
    sc.toneAmplitude = 0.0;
    Capture cap = makeCapture(sc);
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);

    TEST_ASSERT_FLOAT_WITHIN(1.0f, -56.99f, r.noiseDbfs);    // 10*log10(2 * 1e-6)
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, r.slopeDbPerOct);   // pink: flat per octave
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(r.tones[t].valid);
        TEST_ASSERT_FLOAT_WITHIN(3.0f, 0.0f, r.tones[t].riseDb);
        TEST_ASSERT_TRUE(r.tones[t].riseMinDb > -4.0f);
    }
    TEST_ASSERT_EQUAL_UINT32(0, r.impulses);
}

// "Faint": 20 dB less sensitivity shows up as 20 dB less rise.
void test_faint_mic(void) {
    Scenario normal;
    Scenario faint;
    faint.toneAmplitude = 0.001;
    Result rn;
    Result rf;
    TEST_ASSERT_TRUE(run(makeCapture(normal), rn));
    TEST_ASSERT_TRUE(run(makeCapture(faint), rf));
    for (size_t t = 0; t < rn.numTones; t++) {
        TEST_ASSERT_FLOAT_WITHIN(1.5f, 20.0f, rn.tones[t].riseDb - rf.tones[t].riseDb);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, -60.0f, rf.tones[t].onDbfs);
    }
}

// "Crackling": 12 single-sample spikes in the silent part.
void test_crackling_mic(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    const uint32_t first = msToSamples(200, sc.sampleRate);
    const uint32_t spacing = msToSamples(80, sc.sampleRate);
    for (uint32_t i = 0; i < 12; i++) {
        cap.samples[first + i * spacing] += toS24(0.01);
    }
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_EQUAL_UINT32(12, r.impulses);
    TEST_ASSERT_TRUE(r.kurtosis > 5.0f);
    TEST_ASSERT_TRUE(r.crestDb > 30.0f);
    // the tones are unaffected
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(r.tones[t].riseDb > 45.0f);
    }
}

// The dangerous mix: a mic that hears nothing AND crackles all the time. The crackles land in
// buzzer-on and buzzer-off stretches alike and must not be mistaken for hearing the buzzer.
void test_deaf_and_crackling_mic(void) {
    Scenario sc;
    sc.pinkNoise = true;
    sc.noiseRms = 1e-3;
    sc.toneAmplitude = 0.0;
    Capture cap = makeCapture(sc);
    Rng rng(777);
    const uint32_t spacing = msToSamples(250, sc.sampleRate);
    for (uint32_t n = spacing; n < cap.samples.size(); n += spacing) {
        const uint32_t jitter = rng.next() % msToSamples(100, sc.sampleRate);
        if (n + jitter < cap.samples.size()) {
            cap.samples[n + jitter] += toS24(0.3);
        }
    }
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_TRUE(r.impulses >= 5);
    TEST_ASSERT_TRUE(r.kurtosis > 5.0f);
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(r.tones[t].valid);
        TEST_ASSERT_FLOAT_WITHIN(3.0f, 0.0f, r.tones[t].riseDb);
    }
}

// Spikes that fall into a buzzer-on stretch are not counted: only buzzer-off time is searched.
void test_tone_itself_is_not_crackle(void) {
    Scenario sc;
    sc.toneAmplitude = 0.2;   // loud buzzer, large sample-to-sample steps while it sounds
    Result r;
    TEST_ASSERT_TRUE(run(makeCapture(sc), r));
    TEST_ASSERT_EQUAL_UINT32(0, r.impulses);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 0.0f, r.kurtosis);
}

// "Nothing at all": the data line is stuck at zero.
void test_dead_mic(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    std::fill(cap.samples.begin(), cap.samples.end(), 0);
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_EQUAL_FLOAT(DB_FLOOR, r.noiseDbfs);
    TEST_ASSERT_EQUAL_FLOAT(DB_FLOOR, r.peakDbfs);
    TEST_ASSERT_EQUAL_UINT32(cap.samples.size(), r.stuckRun);
    TEST_ASSERT_EQUAL_UINT32(0, r.impulses);
    TEST_ASSERT_EQUAL_UINT32(0, r.dropouts);
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(r.tones[t].valid);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.tones[t].riseDb);
        TEST_ASSERT_EQUAL_FLOAT(DB_FLOOR, r.tones[t].onDbfs);
    }
}

// Stuck at a non-zero value: same verdict, and the DC offset is reported.
void test_stuck_at_constant(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    std::fill(cap.samples.begin(), cap.samples.end(), -1);   // 0xFFFFFF.. on the data line
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_EQUAL_UINT32(cap.samples.size(), r.stuckRun);
    TEST_ASSERT_EQUAL_FLOAT(DB_FLOOR, r.noiseDbfs);
}

void test_dc_offset_and_clipping(void) {
    Scenario sc;
    sc.dc = 0.1;
    sc.toneAmplitude = 0.0;
    Capture cap = makeCapture(sc);
    // 5 samples at the positive rail and 3 at the negative one, all inside the silent part
    const uint32_t base = msToSamples(400, sc.sampleRate);
    for (uint32_t i = 0; i < 5; i++) cap.samples[base + i * 100] = 8388607;
    for (uint32_t i = 0; i < 3; i++) cap.samples[base + 50 + i * 100] = -8388608;
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_EQUAL_UINT32(8, r.clipped);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, -20.0f, r.dcDbfs);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, r.peakDbfs);
}

// DC alone must not leak into the noise level or the spectrum.
void test_dc_offset_does_not_raise_noise(void) {
    Scenario sc;
    sc.dc = 0.1;
    Result r;
    TEST_ASSERT_TRUE(run(makeCapture(sc), r));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -76.99f, r.noiseDbfs);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.01f, r.slopeDbPerOct);
    TEST_ASSERT_FLOAT_WITHIN(0.3f, -20.0f, r.dcDbfs);
}

// Buzzer or I2S clock 3 % off: the band still holds the whole tone.
void test_off_pitch_tone(void) {
    Scenario sc;
    sc.toneDetune = 1.03;
    Result r;
    TEST_ASSERT_TRUE(run(makeCapture(sc), r));
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_FLOAT_WITHIN(1.5f, -40.0f, r.tones[t].onDbfs);
        TEST_ASSERT_FLOAT_WITHIN(20.0f, sc.tones[t] * 1.03f, r.tones[t].peakHz);
    }
}

// An intermittent mic: 300 ms of the silent part goes flat.
void test_dropouts(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    const uint32_t from = msToSamples(500, sc.sampleRate);
    const uint32_t len = msToSamples(300, sc.sampleRate);
    for (uint32_t n = from; n < from + len; n++) {
        cap.samples[n] = 0;
    }
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    TEST_ASSERT_TRUE(r.dropouts >= 15);     // 300 ms / 16 ms = 18 sub-blocks, the edge ones are mixed
    TEST_ASSERT_TRUE(r.dropouts <= 19);
    TEST_ASSERT_TRUE(r.stuckRun >= len);
}

void test_sample_rate_8k(void) {
    TEST_ASSERT_TRUE(toneUsable(1000, 8000));
    TEST_ASSERT_TRUE(toneUsable(2700, 8000));
    TEST_ASSERT_FALSE(toneUsable(4000, 8000));
    TEST_ASSERT_FALSE(toneUsable(50, 16000));
    TEST_ASSERT_TRUE(toneUsable(4000, 16000));

    Scenario sc;
    sc.sampleRate = 8000;
    sc.tones = {1000, 2700};
    Result r;
    TEST_ASSERT_TRUE(run(makeCapture(sc), r));
    assertAllFinite(r);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 4000.0f, r.bandEdgeHz[NUM_BANDS]);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, -76.99f, r.noiseDbfs);
    for (size_t t = 0; t < r.numTones; t++) {
        TEST_ASSERT_TRUE(r.tones[t].valid);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, -40.0f, r.tones[t].onDbfs);
        TEST_ASSERT_TRUE(r.tones[t].riseDb > 40.0f);
    }
}

void test_build_segments(void) {
    Mark marks[2];
    marks[0].onIdx = 24000; marks[0].offIdx = 33600; marks[0].tone = 0;
    marks[1].onIdx = 40000; marks[1].offIdx = 49600; marks[1].tone = 1;
    Segment seg[MAX_SEGMENTS];
    size_t n = buildSegments(marks, 2, 60000, 3200, 1600, seg, MAX_SEGMENTS);
    TEST_ASSERT_EQUAL(5, n);
    // silence, tone 0, gap, tone 1, tail
    TEST_ASSERT_EQUAL_UINT32(1600, seg[0].begin);  TEST_ASSERT_EQUAL_UINT32(24000, seg[0].end);  TEST_ASSERT_EQUAL_INT8(-1, seg[0].tone);
    TEST_ASSERT_EQUAL_UINT32(27200, seg[1].begin); TEST_ASSERT_EQUAL_UINT32(33600, seg[1].end);  TEST_ASSERT_EQUAL_INT8(0, seg[1].tone);
    TEST_ASSERT_EQUAL_UINT32(36800, seg[2].begin); TEST_ASSERT_EQUAL_UINT32(40000, seg[2].end);  TEST_ASSERT_EQUAL_INT8(-1, seg[2].tone);
    TEST_ASSERT_EQUAL_UINT32(43200, seg[3].begin); TEST_ASSERT_EQUAL_UINT32(49600, seg[3].end);  TEST_ASSERT_EQUAL_INT8(1, seg[3].tone);
    TEST_ASSERT_EQUAL_UINT32(52800, seg[4].begin); TEST_ASSERT_EQUAL_UINT32(60000, seg[4].end);  TEST_ASSERT_EQUAL_INT8(-1, seg[4].tone);

    // capture stopped in the middle of the second tone: nothing may reach past the end
    n = buildSegments(marks, 2, 45000, 3200, 1600, seg, MAX_SEGMENTS);
    TEST_ASSERT_EQUAL(4, n);
    TEST_ASSERT_EQUAL_UINT32(45000, seg[3].end);
    for (size_t i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(seg[i].end <= 45000);
        TEST_ASSERT_TRUE(seg[i].begin < seg[i].end);
    }
}

// A capture cut short reports the missing tone as not measured instead of inventing a value.
void test_truncated_capture(void) {
    Scenario sc;
    Capture cap = makeCapture(sc);
    // keep the silence and the first tone only
    cap.samples.resize(cap.marks[1].onIdx);
    rebuildSegments(cap);
    Result r;
    TEST_ASSERT_TRUE(run(cap, r));
    assertAllFinite(r);
    TEST_ASSERT_TRUE(r.tones[0].valid);
    TEST_ASSERT_EQUAL_UINT8(1, r.tones[0].reps);
    TEST_ASSERT_FALSE(r.tones[1].valid);
    TEST_ASSERT_FALSE(r.tones[2].valid);
}

void test_too_short_capture(void) {
    std::vector<int32_t> few(FFT_N - 1, 100);
    uint16_t tones[1] = {1000};
    Input in;
    in.samples = few.data();
    in.count = static_cast<uint32_t>(few.size());
    in.sampleRate = 16000;
    in.segments = NULL;
    in.numSegments = 0;
    in.toneHz = tones;
    in.numTones = 1;
    Result r;
    TEST_ASSERT_FALSE(analyze(in, gWorkspace, r));
    TEST_ASSERT_EQUAL_FLOAT(DB_FLOOR, r.noiseDbfs);
    TEST_ASSERT_FALSE(r.tones[0].valid);
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_healthy_mic);
    RUN_TEST(test_deaf_mic_pink_noise);
    RUN_TEST(test_faint_mic);
    RUN_TEST(test_crackling_mic);
    RUN_TEST(test_deaf_and_crackling_mic);
    RUN_TEST(test_tone_itself_is_not_crackle);
    RUN_TEST(test_dead_mic);
    RUN_TEST(test_stuck_at_constant);
    RUN_TEST(test_dc_offset_and_clipping);
    RUN_TEST(test_dc_offset_does_not_raise_noise);
    RUN_TEST(test_off_pitch_tone);
    RUN_TEST(test_dropouts);
    RUN_TEST(test_sample_rate_8k);
    RUN_TEST(test_build_segments);
    RUN_TEST(test_truncated_capture);
    RUN_TEST(test_too_short_capture);
    return UNITY_END();
}

int main(void) {
    return runUnityTests();
}
