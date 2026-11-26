#pragma once
#include "Accumulator.h"
#include "Diagnostics.h"
#include "LoudnessPlanner.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <mutex>
#include <functional>

namespace mooni
{
enum class AnalysisMode : int { Full = 0, Smart = 1 };

enum class AnalysisState : int
{
    Idle = 0,      // waiting (no session)
    Listening,     // collecting
    Ready,         // enough material collected (smart: report available; full: preliminary)
    Done,          // finalised report available
    TooShort       // playback stopped before enough material was heard
};

/** Everything the GUI needs at ~30 Hz. Copied under a short lock (GUI <-> analysis thread only). */
struct LiveData
{
    static constexpr int kSpectrumPoints = 220;
    static constexpr int kScopePoints = 384;
    static constexpr int kHistory = 600;   // 60 s of short-term loudness at 10 Hz

    AnalysisState state = AnalysisState::Idle;
    AnalysisMode mode = AnalysisMode::Full;
    bool transportPlaying = false;
    bool signalPresent = false;
    float momentary = -100, shortTerm = -100, integrated = -100, integratedIn = -100, lra = 0;
    float truePeak = -100, samplePeak = -100, truePeakHold = -100;
    float correlation = 1, widthPct = 0;
    float confidence = 0;
    double activeSeconds = 0;
    float loudStab = 0, specStab = 0, stereoStab = 0;
    int overs = 0, clipRuns = 0, onsetsTotal = 0;
    int sessionId = 0;
    float matchGainDb = 0;                       // in - out loudness (for loudness-matched bypass)
    std::array<float, kSpectrumPoints> specOut {}, specIn {};   // dB, display grid
    std::array<float, kScopePoints * 2> scope {};               // interleaved (side, mid) for the tide mirror
    std::vector<float> histOut, histIn;          // short-term loudness history
    int fifoDrops = 0;
    double planCaptured = 0;
    static float spectrumFreq (int i) { return 20.0f * std::pow (1000.0f, (float) i / (kSpectrumPoints - 1)); }
};

class AnalysisEngine : private juce::Thread
{
public:
    AnalysisEngine();
    ~AnalysisEngine() override;

    /** Called from prepareToPlay (before audio starts). */
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();

    /** Audio thread: lock-free, wait-free. in = plugin input, pre = before maximiser, out = plugin output. */
    void pushAudio (const float* inL, const float* inR, const float* preL, const float* preR,
                    const float* outL, const float* outR, int numSamples, bool hostPlaying, bool hasTransport) noexcept;

    // control (any non-audio thread)
    void startSession();
    void finishSession();          // finalise now (like stopping playback)
    void cancelSession();
    void setMode (AnalysisMode m) { mode = (int) m; }
    AnalysisMode getMode() const  { return (AnalysisMode) mode.load(); }
    void setAutoListen (bool b)   { autoListen = b; }
    bool getAutoListen() const    { return autoListen.load(); }
    void setCrossovers (float a, float b, float c) noexcept { xo0 = a; xo1 = b; xo2 = c; }   // applied to the next session

    /** Rebuild the latest report with new context (genre/reference) without re-listening. */
    void rediagnose();

    void setContextProvider (std::function<DiagnosticsContext()> f) { std::lock_guard<std::mutex> g (ctxLock); contextProvider = std::move (f); }
    void setReportCallback (std::function<void (ReportPtr, bool /*sessionFinal*/)> f) { std::lock_guard<std::mutex> g (ctxLock); reportCallback = std::move (f); }

    void getLive (LiveData& out) const;
    ReportPtr getLatestReport() const;
    int reportCounter() const noexcept { return reports.load(); }
    int readyCounter() const noexcept { return readyEvents.load(); }
    int sessionCounter() const noexcept { return sessions.load(); }

    LoudnessPlanner& getPlanner() noexcept { return planner; }
    std::atomic<float> matchGainDb { 0.0f };
    std::atomic<bool> freezeMatchFlag { false };   // true while the loudness-matched bypass is audible

private:
    void run() override;
    void initAtRate();
    void beginSessionInternal();
    void finaliseInternal (bool final);
    void publishLive();
    void handleTransport (bool playing, bool hasTransport, bool signal);

    // FIFO: 6 channels
    static constexpr int kFifoSize = 1 << 17;
    juce::AbstractFifo fifo { kFifoSize };
    std::array<std::vector<float>, 6> fifoBuf;
    std::atomic<bool> hostPlaying { false }, hostHasTransport { false };
    std::atomic<int> drops { 0 };

    std::atomic<double> pendingRate { 0.0 };
    double rate = 0.0;
    std::atomic<int> mode { (int) AnalysisMode::Full };
    std::atomic<bool> autoListen { true };
    std::atomic<int> cmdStart { 0 }, cmdFinish { 0 }, cmdCancel { 0 }, cmdRediagnose { 0 };
    int seenStart = 0, seenFinish = 0, seenCancel = 0, seenRediagnose = 0;

    std::atomic<float> xo0 { 120 }, xo1 { 1000 }, xo2 { 6000 };   // lock-free: set from the audio thread

    // analysis-thread state
    MixAccumulator sessOut, sessIn, liveOut, liveIn;
    LoudnessPlanner planner;
    AnalysisState state = AnalysisState::Idle;
    bool wasPlaying = false;
    double silentSeconds = 1.0e9, stoppedSeconds = 0;   // "long silent" until the first signal
    bool readyNotified = false;
    int sessionId = 0;
    float tpHold = -100; double tpHoldAge = 0;
    std::vector<float> scratch[6];

    mutable std::mutex liveLock;
    LiveData live;
    mutable std::mutex reportLock;
    ReportPtr latest;
    std::atomic<int> reports { 0 }, readyEvents { 0 }, sessions { 0 };

    std::mutex ctxLock;
    std::function<DiagnosticsContext()> contextProvider;
    std::function<void (ReportPtr, bool)> reportCallback;
    MixStats lastStats; bool hasLastStats = false;
    juce::int64 lastPublish = 0;
};
} // namespace mooni
