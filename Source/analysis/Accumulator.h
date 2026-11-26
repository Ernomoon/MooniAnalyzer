#pragma once
#include "Report.h"
#include "../dsp/LoudnessMeter.h"
#include "../dsp/TruePeak.h"
#include "../dsp/StereoImager.h"
#include <juce_dsp/juce_dsp.h>
#include <deque>

namespace mooni
{
/** Accumulates long-term measurements of a stereo stream. Runs on the analysis thread
    (or offline for reference files). Never touched by the audio thread. */
class MixAccumulator
{
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHop = kFftSize / 2;
    static constexpr int kBins = kFftSize / 2 + 1;

    MixAccumulator();

    void prepare (double sampleRate, std::array<float, 3> crossovers);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    MixStats computeStats() const;

    struct Readiness
    {
        float confidence = 0;       // 0..1
        double activeSeconds = 0;
        float loudnessStability = 0, spectrumStability = 0, stereoStability = 0;
        bool ready = false;
    };
    Readiness readiness() const;

    // live data (read on the analysis thread, then published)
    const dsp::LoudnessMeter& meter() const noexcept { return loud; }
    float recentCorrelation() const noexcept { return lastStepCorr; }
    float recentWidthPct() const noexcept { return lastStepWidth; }
    int   consumeOnsets() noexcept { const int o = onsetsSinceRead; onsetsSinceRead = 0; return o; }
    const std::vector<float>& liveSpectrumPower() const noexcept { return livePower; }
    double getSampleRate() const noexcept { return sr; }
    double activeSeconds() const noexcept { return activeSteps * 0.1; }
    float  currentPeakDb() const noexcept { return lastStepPeakDb; }
    float  currentTruePeakDb() const noexcept { return lastStepTpDb; }
    int    truePeakOverCount() const noexcept { return truePeakOvers; }
    int    flatRunCount() const noexcept { return flatTopRuns; }

private:
    void stepCompleted();
    void fftFrame();
    void bandPowers (const std::vector<double>& binPower, double frames, const float* centres, int numBands,
                     double halfWidthOct, float* out) const;

    double sr = 48000.0;
    std::array<float, 3> xo { 120, 1000, 6000 };

    dsp::LoudnessMeter loud;
    dsp::TruePeakDetector tpL, tpR;

    // stereo band split (L and R)
    dsp::LR4 lMid, lLow, lHigh, rMid, rLow, rHigh;
    std::array<double, 4> bLL {}, bRR {}, bLR {};
    double LL = 0, RR = 0, LR = 0;
    double stepLL = 0, stepRR = 0, stepLR = 0;
    int corrSteps = 0, negCorrSteps = 0;
    float worstCorr = 1.0f, lastStepCorr = 1.0f, lastStepWidth = 0.0f;

    // levels
    float maxSample = 0, maxTp = 0, stepPeak = 0, stepTp = 0, lastStepPeakDb = -100, lastStepTpDb = -100;
    std::deque<float> recentStepPeaks;
    double activeSumSq = 0; juce::int64 activeCount = 0;
    double sumL = 0, sumR = 0; juce::int64 count = 0;
    int truePeakOvers = 0;
    double microSum = 0; int microCount = 0;
    int activeSteps = 0;

    // clipping / flat tops
    float prevL = 0, prevR = 0; int runL = 0, runR = 0;
    std::array<int, 121> flatRunHist {};   // runs >= 3 by level: index = round(-dB*10), 0..12 dB
    int flatTopRuns = 0;
    void endRun (float v, int run);

    // transients
    int winLen = 480, winFill = 0;
    float winPeak = 0; double winEnergy = 0;
    double slowRms2 = 0, avgEnergy = 0;
    double punchSum = 0; int punchCount = 0;
    int onsets = 0, onsetsSinceRead = 0, samplesSinceOnset = 0;

    // spectrum
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ringM, ringS, fftBuf;
    int ringPos = 0, hopFill = 0;
    std::vector<double> sumPower, sumSide;
    double frames = 0;
    std::vector<float> livePower;
    std::vector<double> kWeight;
    double windowNorm = 1;

    // stability snapshots (one per second of active audio)
    struct Snapshot { float integrated; float width; std::array<float, BandGrid::kSixthBands> bands; };
    std::deque<Snapshot> snaps;
    int stepsSinceSnap = 0;
    int stepLen = 4800, stepFill = 0;
};
} // namespace mooni
