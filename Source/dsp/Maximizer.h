#pragma once
#include "Biquad.h"
#include "TruePeak.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>
#include <memory>

namespace mooni::dsp
{
struct MaximizerParams
{
    bool  enabled   = false;
    float driveDb   = 0.0f;   // input gain into the limiter
    float ceilingDb = -1.0f;  // true-peak ceiling (dBTP)
    float releaseMs = 120.0f;
    float clipPct   = 0.0f;   // 0..100: share of peak reduction handed to a soft clipper
};

/** Fixed-delay delay line (stereo). */
struct StereoDelay
{
    std::vector<float> l, r; int pos = 0, len = 0;
    void prepare (int samples) { len = std::max (0, samples); l.assign ((size_t) len + 1, 0.0f); r.assign ((size_t) len + 1, 0.0f); pos = 0; }
    void reset() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }
    inline void process (float& a, float& b) noexcept
    {
        if (len == 0) return;
        const float oa = l[(size_t) pos], ob = r[(size_t) pos];
        l[(size_t) pos] = a; r[(size_t) pos] = b;
        pos = (pos + 1) % len;
        a = oa; b = ob;
    }
};

/** Loudness maximiser: drive -> (optional 4x-oversampled soft clipper) -> true-peak
    look-ahead brick-wall limiter. Latency is constant (reported) whether on or off.
    Gain computer: per-sample target gain from 4x true-peak detection, sliding-minimum
    hold over the look-ahead window, box-filter attack (guarantees the envelope reaches the
    required gain at the peak), then exponential release. */
class Maximizer
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const MaximizerParams& p) { params = p; }

    void process (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency; }
    float getBlockGainReductionDb() const noexcept { return lastBlockGrDb; }
    float getBlockClipReductionDb() const noexcept { return lastBlockClipDb; }

private:
    void processClipper (float* l, float* r, int n) noexcept;
    static inline float softClip (float x, float t) noexcept
    {
        // linear below knee (0.7 t), smooth tanh-shaped approach to t above it
        const float knee = 0.7f * t, ax = std::abs (x);
        if (ax <= knee) return x;
        const float range = t - knee;
        const float y = knee + range * std::tanh ((ax - knee) / range);
        return std::copysign (y, x);
    }

    double sr = 44100.0;
    MaximizerParams params;

    int look = 64;        // look-ahead (samples)
    int osLatency = 0;    // clipper oversampler latency (rounded)
    int latency = 0;      // total reported latency

    // clipper path
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    StereoDelay clipDryDelay;
    Ramp clipMix;
    std::vector<float> clipL, clipR;

    // limiter
    TruePeakDetectorT<8> tpL, tpR;
    // sliding minimum (monotonic wedge) over the hold window
    std::vector<float> wedgeVal; std::vector<juce::int64> wedgeIdx;
    int wedgeHead = 0, wedgeSize = 0, holdLen = 0; juce::int64 sampleIndex = 0;
    inline float pushHold (float v) noexcept
    {
        const int cap = (int) wedgeVal.size();
        // drop larger values from the back
        while (wedgeSize > 0 && wedgeVal[(size_t) ((wedgeHead + wedgeSize - 1) % cap)] >= v) --wedgeSize;
        const int slot = (wedgeHead + wedgeSize) % cap;
        wedgeVal[(size_t) slot] = v; wedgeIdx[(size_t) slot] = sampleIndex; ++wedgeSize;
        // expire from the front
        while (wedgeIdx[(size_t) wedgeHead] <= sampleIndex - holdLen) { wedgeHead = (wedgeHead + 1) % cap; --wedgeSize; }
        ++sampleIndex;
        return wedgeVal[(size_t) wedgeHead];
    }
    std::vector<double> boxBuf; int boxPos = 0; double boxSum = 0.0;
    double env = 1.0;
    StereoDelay mainDelay;       // aligns audio with the gain envelope
    StereoDelay bypassDelay;     // keeps latency constant while switched off

    OnePole drive, ceiling;
    Ramp onMix;
    float lastBlockGrDb = 0.0f, lastBlockClipDb = 0.0f;
    std::vector<float> dryL, dryR;
};
} // namespace mooni::dsp
