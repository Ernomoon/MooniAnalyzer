#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace mooni::params
{
inline constexpr int kEqBands = 8;
inline constexpr int kImgBands = 4;

// IDs (stable: used for automation and saved sessions)
inline const juce::String bypass   = "bypass";
inline const juce::String eqOn     = "eqOn";
inline const juce::String imgOn    = "imgOn";
inline const juce::String imgBal   = "imgBal";
inline const juce::String maxOn    = "maxOn";
inline const juce::String maxDrive = "maxDrive";
inline const juce::String maxCeil  = "maxCeil";
inline const juce::String maxRel   = "maxRelease";
inline const juce::String maxClip  = "maxClip";
inline const juce::String maxTarget = "maxTarget";

inline juce::String eqBandOn (int i)   { return "eq" + juce::String (i) + "On"; }
inline juce::String eqBandType (int i) { return "eq" + juce::String (i) + "Type"; }
inline juce::String eqBandFreq (int i) { return "eq" + juce::String (i) + "Freq"; }
inline juce::String eqBandGain (int i) { return "eq" + juce::String (i) + "Gain"; }
inline juce::String eqBandQ (int i)    { return "eq" + juce::String (i) + "Q"; }
inline juce::String eqBandCh (int i)   { return "eq" + juce::String (i) + "Ch"; }
inline juce::String imgXover (int i)   { return "imgX" + juce::String (i + 1); }
inline juce::String imgWidth (int i)   { return "imgW" + juce::String (i + 1); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

juce::StringArray filterTypeNames();
juce::StringArray channelNames();

struct EqDefaults { int type; float freq, gain, q; };
EqDefaults eqDefault (int band);
inline constexpr float kDefaultXover[3] { 120.0f, 1000.0f, 6000.0f };
} // namespace mooni::params
