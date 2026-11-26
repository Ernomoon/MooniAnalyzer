#pragma once
#include "../dsp/Maximizer.h"
#include "../dsp/LoudnessMeter.h"
#include <juce_core/juce_core.h>
#include <mutex>
#include <atomic>

namespace mooni
{
/** Plans maximiser settings for a LUFS target from audio that has actually been heard.

    Capture (analysis thread): the signal *before* the maximiser is metered in 100 ms blocks for the
    whole session, and the loudest 3-second chunks are kept (reservoir, ~90 s max).
    Planning (own thread): the real Maximizer DSP runs offline on the kept chunks; the measured
    pre->post loudness change per block level is applied to the whole-session block history to
    predict the integrated loudness. A secant search finds the drive that hits the target. */
class LoudnessPlanner : private juce::Thread
{
public:
    struct Request
    {
        float targetLufs = -9, ceilingDb = -1, releaseMs = 120, clipPct = 0, maxAvgGrDb = 4;
    };
    struct Result
    {
        bool valid = false;
        juce::String error;
        float targetLufs = 0;
        float driveDb = 0;            // recommended drive
        float predictedLufs = -100;   // predicted integrated loudness with that drive
        float avgGrDb = 0, maxGrDb = 0, p95GrDb = 0;   // limiter gain reduction (positive dB)
        float clipShareDb = 0;        // average peak reduction done by the clipper
        float sourceLufs = -100;      // integrated loudness of the material before the maximiser
        float safeTargetLufs = 0;     // loudest target keeping avg GR within the style allowance
        double secondsUsed = 0, secondsSession = 0;
        int risk = 0;                 // 0 ok, 1 noticeable, 2 heavy
        juce::int64 timeMs = 0;
    };

    LoudnessPlanner();
    ~LoudnessPlanner() override;

    // capture side (analysis thread)
    void prepare (double sampleRate);
    void resetSession();
    void addAudio (const float* left, const float* right, int numSamples);
    double capturedSeconds() const;
    double sessionSeconds() const;
    float sourceIntegrated() const;

    // planning side
    bool requestPlan (const Request& r);      // false if busy / not enough audio
    bool isBusy() const noexcept { return busy.load(); }
    Result getResult() const;
    int resultCounter() const noexcept { return results.load(); }

private:
    void run() override;
    struct Chunk { std::vector<float> l, r; float loudness = -100; };
    Result plan (const Request& req, std::vector<Chunk> chunks, std::vector<float> sessionBlocks, double sr);

    mutable std::mutex lock;
    double sr = 48000.0;
    int chunkLen = 144000;
    std::vector<Chunk> pool;
    Chunk current;
    dsp::LoudnessMeter chunkMeter, sessionMeter;
    static constexpr int kMaxChunks = 30;

    Request pending;
    std::atomic<bool> busy { false };
    std::atomic<int> results { 0 };
    Result last;
};
} // namespace mooni
