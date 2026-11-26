#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "History.h"
#include "../dsp/ParametricEQ.h"
#include "../dsp/StereoImager.h"
#include "../dsp/Maximizer.h"
#include "../analysis/AnalysisEngine.h"
#include "../analysis/ReferenceTrack.h"

namespace mooni
{
enum class MonitorMode : int { Normal = 0, Mono, Side };

/** Persistent, non-automatable user settings. */
struct Settings
{
    int genreChoice = 0;            // 0 = auto
    bool autoListen = true;
    AnalysisMode mode = AnalysisMode::Full;
    bool reducedMotion = false;
    int chatter = 1;                // 0 quiet, 1 normal, 2 chatty
    bool fairCompare = true;        // loudness-matched bypass
    bool useReferenceAsTarget = true;
    bool matchReferenceLoudness = true;
    float uiScale = 1.0f;
};

class MooniProcessor : public juce::AudioProcessor,
                       private juce::AsyncUpdater,
                       private juce::AudioProcessorListener
{
public:
    MooniProcessor();
    ~MooniProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mooni Analyzer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ----- model access (message thread) -----
    juce::AudioProcessorValueTreeState apvts;
    AnalysisEngine engine;
    ReferenceTrack reference;
    History history;
    ParamUndo undo;
    Settings settings;

    void setSettings (const Settings& s);
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    float value (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setValue (const juce::String& id, float v);     // with host notification + gesture
    std::array<dsp::EqBandParams, dsp::ParametricEQ::kNumBands> currentEqBands() const;
    juce::String fingerprint() const;
    double getCurrentSampleRate() const noexcept { return currentRate.load(); }

    // ----- actions (message thread) -----
    void pushUndoSnapshot();
    bool undoStep();
    bool redoStep();
    void applyEqRecommendations (const Report& r);
    void applyStereoRecommendations (const Report& r);
    void applyMaximizerDrive (float driveDb, float targetLufs);
    void resetEq();
    void resetStereo();
    void restoreParams (const juce::ValueTree& state);
    void abSwitch();                 // toggles between slot A and B
    int  abCurrent() const noexcept { return abSlot; }
    void abCopyToOther();

    // monitoring (not saved: never leave a session in mono by accident)
    std::atomic<int> monitor { (int) MonitorMode::Normal };
    std::atomic<bool> referenceListen { false };
    std::atomic<bool> bandListen { false };
    std::atomic<float> bandListenLo { 200 }, bandListenHi { 500 };

    // meters written by the audio thread
    std::atomic<float> meterGrDb { 0 }, meterClipDb { 0 }, meterOutPeak { -100 }, meterInPeak { -100 };
    std::atomic<float> appliedFairGainDb { 0 };
    std::atomic<int> latencySamples { 0 };
    std::atomic<float> referenceGainDb { 0 };   // set by the UI: loudness match for reference monitoring

    // UI state (persisted)
    int lastRoom = 0;
    int reportsSeen = 0;

    /** While true, parameter gestures don't create undo steps (batch actions push one step at the end). */
    std::atomic<bool> suppressSnapshots { false };

    // events for the character (message thread reads)
    std::atomic<int> paramGestureCount { 0 };
    juce::String lastGestureParam;

private:
    void handleAsyncUpdate() override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int parameterIndex) override;
    DiagnosticsContext makeContext() const;
    void readParams();

    dsp::ParametricEQ eq;
    dsp::StereoImager imager;
    dsp::Maximizer maximizer;

    // pointers to raw parameter values (lock-free reads on the audio thread)
    std::atomic<float>* pBypass = nullptr; std::atomic<float>* pEqOn = nullptr;
    std::array<std::atomic<float>*, params::kEqBands> pBandOn {}, pBandType {}, pBandFreq {}, pBandGain {}, pBandQ {}, pBandCh {};
    std::atomic<float>* pImgOn = nullptr; std::atomic<float>* pImgBal = nullptr;
    std::array<std::atomic<float>*, 3> pXo {};
    std::array<std::atomic<float>*, params::kImgBands> pW {};
    std::atomic<float>* pMaxOn = nullptr; std::atomic<float>* pDrive = nullptr; std::atomic<float>* pCeil = nullptr;
    std::atomic<float>* pRel = nullptr; std::atomic<float>* pClip = nullptr;

    static constexpr int kInternalBlock = 512;
    juce::AudioBuffer<float> inTap, preTap, work, refBuf;
    std::atomic<double> currentRate { 44100.0 };

    // monitoring DSP
    dsp::BiquadCoeffs listenHp, listenLp; dsp::BiquadState lhp[2][2], llp[2][2];
    dsp::Ramp listenMix, refMix;
    dsp::OnePole fairGain;
    float listenDesigned[2] { 0, 0 };

    std::mutex pendingLock;
    std::vector<std::pair<ReportPtr, bool>> pendingReports;
    std::atomic<bool> gestureSnapshotPending { false };
    std::atomic<int> lastGestureIndex { -1 };
    std::atomic<bool> useRefAtomic { true }, fairAtomic { true };

    // A/B
    int abSlot = 0;
    juce::ValueTree abStates[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MooniProcessor)
};
} // namespace mooni
