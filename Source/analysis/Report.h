#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>
#include <memory>

namespace mooni
{
/** Fractional-octave band grids used by the analyser. */
struct BandGrid
{
    static constexpr int kSixthBands = 61;       // 1/6 octave, 20 Hz .. 20.2 kHz
    static constexpr int kFineBands  = 241;      // 1/24 octave, 20 Hz .. 20.2 kHz
    static float sixthCentre (int i) { return 20.0f * std::pow (2.0f, (float) i / 6.0f); }
    static float fineCentre (int i)  { return 20.0f * std::pow (2.0f, (float) i / 24.0f); }
};

/** Raw measurements of one listening session (or of a reference file). Pure data. */
struct MixStats
{
    double sampleRate = 0;
    double secondsAnalysed = 0;     // seconds of audio above the absolute gate
    double secondsTotal = 0;

    // loudness / dynamics
    float integrated = -100, lra = 0, maxMomentary = -100, maxShortTerm = -100;
    float truePeakDb = -100, samplePeakDb = -100;
    float rmsDb = -100;              // RMS of active audio (both channels)
    float crestDb = 0;               // sample peak - RMS
    float plr = 0;                   // true peak - integrated
    float microDynamicsDb = 0;       // mean (block peak - block loudness) over active 400 ms blocks
    float punchDb = 0;               // mean 10 ms peak vs 300 ms RMS ratio, dB
    int   clipEvents = 0;            // runs of identical full-scale-ish samples
    int   flatTopRuns = 0;           // runs of >= 4 identical samples above -12 dBFS
    int   truePeakOvers = 0;         // 400 ms blocks whose true peak exceeds 0 dBTP
    float clipEventsPerMinute = 0;
    float dcOffsetDb = -120;
    int   onsets = 0;

    // spectrum (power, dB, absolute; sum of channels / 2)
    std::array<float, BandGrid::kSixthBands> sixth {};
    std::array<float, BandGrid::kFineBands> fine {};
    std::array<float, BandGrid::kSixthBands> sideSixth {};   // side-channel power per band
    float infrasonicRatioDb = -100;   // energy < 25 Hz relative to 25..200 Hz

    // stereo
    float correlation = 1, widthPct = 0, balanceDb = 0, monoLossDb = 0;
    float negativeCorrFraction = 0;   // share of active 100 ms steps with correlation < 0
    float worstCorrelation = 1;
    std::array<float, 3> xovers { 120, 1000, 6000 };
    std::array<float, 4> bandWidthPct {};
    std::array<float, 4> bandCorr { 1, 1, 1, 1 };
    std::array<float, 4> bandMonoLossDb {};

    // translation (estimates from the long-term spectrum)
    float lowEndShare = 0;            // share of K-weighted energy below 150 Hz (0..1)
    float phoneLossDb = 0;            // loudness lost on a small speaker (HP ~150 Hz) estimate

    // loudness history (short-term every 100 ms, for the timeline)
    std::vector<float> shortTermHistory;

    bool isValid() const { return secondsAnalysed >= 1.0; }
};

enum class Module : int { None = 0, Observatory, Eq, Stereo, Maximizer, Reference, Logbook };

struct Problem
{
    juce::String id;              // stable key, e.g. "lowmid_mud"
    juce::String category;        // "Баланс", "Динамика", ...
    float severity = 0;           // 0..1
    juce::String title;           // short
    juce::String what;            // what was detected (with numbers)
    juce::String why;             // why it matters
    juce::String fix;             // suggestion
    Module module = Module::None;
    float freqLo = 0, freqHi = 0; // for "listen to the problem" (0 = n/a)
    bool measured = true;         // false -> subjective estimate
};

struct EqSuggestion
{
    int type = 0;          // dsp::FilterType
    float freq = 1000, gainDb = 0, q = 1;
    int channel = 0;       // dsp::BandChannel
    juce::String reason;
};

struct SubScore { juce::String name; int value = 0; float weight = 1; juce::String note; };

struct Report
{
    bool valid = false;
    juce::int64 timeMs = 0;
    juce::String label;               // "Оригинал", "Прослушивание 3", ...
    int session = 0;                  // analysis session id (smart-mode refreshes share it)
    MixStats stats;                   // measured at the plugin output (what you hear)
    float inIntegrated = -100, inTruePeak = -100;           // plugin input ("before")
    std::array<float, BandGrid::kSixthBands> inSixth {};

    int genreProfile = 0;             // index into genre table actually used
    juce::String genreGuess;          // auto-detected (text, may be empty)
    float genreConfidence = 0;

    int score = 0;
    std::vector<SubScore> subscores;
    juce::String verdict;             // one-line summary from Mooni
    std::vector<Problem> problems;    // sorted, most important first

    // recommendations
    std::array<float, BandGrid::kSixthBands> targetSixth {};   // target band levels (same offset as stats)
    std::vector<EqSuggestion> eq;
    std::array<float, 4> recWidthPct { 100, 100, 100, 100 };   // recommended imager parameter values
    float recBalanceDb = 0;
    float recTargetLufs = -9;
    juce::String targetReason;
    bool usedReference = false;

    // context of the analysis
    juce::String paramFingerprint;    // processing settings at analysis time
    bool fromReferenceFile = false;

    juce::ValueTree toValueTree() const;
    static Report fromValueTree (const juce::ValueTree&);
};

using ReportPtr = std::shared_ptr<const Report>;

// ---------------------------------------------------------------------------
// Genre profiles: context for tolerances and targets (heuristics, not standards).
// ---------------------------------------------------------------------------
struct GenreProfile
{
    const char* key;
    juce::String name;
    float lufsTarget;          // typical master loudness for the style
    float lowShelfDb;          // expected band level 40-100 Hz relative to the 500 Hz-2 kHz region
    float subExtraDb;          // extra allowance below 50 Hz (808 / sub-heavy styles)
    float highSlopeDbOct;      // expected slope of band level above 1 kHz (dB / octave)
    float tolerance;           // tonal tolerance (dB)
    float minPlr;              // PLR below this = over-limited for the style
    float minMicroDyn;         // micro-dynamics below this = squashed
    float maxAvgGrDb;          // acceptable average limiter GR when planning loudness
};

const std::vector<GenreProfile>& genreProfiles();   // index 0 = "Авто"
int genreIndexForKey (const juce::String& key);
} // namespace mooni
