#include "Params.h"
#include "Text.h"
#include "../dsp/Biquad.h"

namespace mooni::params
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

juce::StringArray filterTypeNames()
{
    return { ru ("Колокол"), ru ("Полка НЧ"), ru ("Полка ВЧ"), ru ("Срез НЧ"), ru ("Срез ВЧ"), ru ("Режекция") };
}

juce::StringArray channelNames() { return { ru ("Стерео"), "Mid", "Side" }; }

EqDefaults eqDefault (int band)
{
    static const EqDefaults d[kEqBands] = {
        { (int) dsp::FilterType::LowCut,    25.0f,   0.0f, 0.71f },
        { (int) dsp::FilterType::LowShelf,  90.0f,   0.0f, 0.71f },
        { (int) dsp::FilterType::Bell,      250.0f,  0.0f, 1.0f },
        { (int) dsp::FilterType::Bell,      700.0f,  0.0f, 1.0f },
        { (int) dsp::FilterType::Bell,      2000.0f, 0.0f, 1.0f },
        { (int) dsp::FilterType::Bell,      5000.0f, 0.0f, 1.0f },
        { (int) dsp::FilterType::HighShelf, 10000.0f, 0.0f, 0.71f },
        { (int) dsp::FilterType::Bell,      14000.0f, 0.0f, 1.0f },
    };
    return d[std::clamp (band, 0, kEqBands - 1)];
}

static Range freqRange (float lo, float hi)
{
    Range r (lo, hi, [] (float s, float e, float v) { return s * std::pow (e / s, v); },
                     [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                     [] (float s, float e, float v) { return juce::jlimit (s, e, v); });
    return r;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto attrDb  = juce::AudioParameterFloatAttributes().withLabel (ru ("дБ"));
    auto attrHz  = juce::AudioParameterFloatAttributes().withLabel (ru ("Гц"));
    auto attrPct = juce::AudioParameterFloatAttributes().withLabel ("%");

    layout.add (std::make_unique<APB> (juce::ParameterID { bypass, 1 }, ru ("Обход обработки (Bypass)"), false));

    auto eq = std::make_unique<juce::AudioProcessorParameterGroup> ("eq", ru ("Эквалайзер"), "|");
    eq->addChild (std::make_unique<APB> (juce::ParameterID { eqOn, 1 }, ru ("EQ: включён"), false));
    for (int i = 0; i < kEqBands; ++i)
    {
        const auto d = eqDefault (i);
        const auto n = ru ("EQ ") + juce::String (i + 1) + ": ";
        eq->addChild (std::make_unique<APB> (juce::ParameterID { eqBandOn (i), 1 }, n + ru ("вкл"), false));
        eq->addChild (std::make_unique<APC> (juce::ParameterID { eqBandType (i), 1 }, n + ru ("тип"), filterTypeNames(), d.type));
        eq->addChild (std::make_unique<APF> (juce::ParameterID { eqBandFreq (i), 1 }, n + ru ("частота"), freqRange (20.0f, 20000.0f), d.freq, attrHz));
        eq->addChild (std::make_unique<APF> (juce::ParameterID { eqBandGain (i), 1 }, n + ru ("усиление"), Range (-18.0f, 18.0f, 0.01f), d.gain, attrDb));
        eq->addChild (std::make_unique<APF> (juce::ParameterID { eqBandQ (i), 1 }, n + "Q", Range (0.1f, 18.0f, 0.001f, 0.35f), d.q));
        eq->addChild (std::make_unique<APC> (juce::ParameterID { eqBandCh (i), 1 }, n + ru ("канал"), channelNames(), 0));
    }
    layout.add (std::move (eq));

    auto img = std::make_unique<juce::AudioProcessorParameterGroup> ("img", ru ("Стерео"), "|");
    img->addChild (std::make_unique<APB> (juce::ParameterID { imgOn, 1 }, ru ("Стерео: включено"), false));
    img->addChild (std::make_unique<APF> (juce::ParameterID { imgXover (0), 1 }, ru ("Стерео: раздел 1"), freqRange (40.0f, 800.0f), kDefaultXover[0], attrHz));
    img->addChild (std::make_unique<APF> (juce::ParameterID { imgXover (1), 1 }, ru ("Стерео: раздел 2"), freqRange (200.0f, 8000.0f), kDefaultXover[1], attrHz));
    img->addChild (std::make_unique<APF> (juce::ParameterID { imgXover (2), 1 }, ru ("Стерео: раздел 3"), freqRange (1000.0f, 16000.0f), kDefaultXover[2], attrHz));
    for (int i = 0; i < kImgBands; ++i)
        img->addChild (std::make_unique<APF> (juce::ParameterID { imgWidth (i), 1 }, ru ("Стерео: ширина ") + juce::String (i + 1),
                                              Range (0.0f, 200.0f, 0.1f), 100.0f, attrPct));
    img->addChild (std::make_unique<APF> (juce::ParameterID { imgBal, 1 }, ru ("Стерео: баланс Л/П"), Range (-3.0f, 3.0f, 0.01f), 0.0f, attrDb));
    layout.add (std::move (img));

    auto mx = std::make_unique<juce::AudioProcessorParameterGroup> ("max", ru ("Максимайзер"), "|");
    mx->addChild (std::make_unique<APB> (juce::ParameterID { maxOn, 1 }, ru ("Максимайзер: включён"), false));
    mx->addChild (std::make_unique<APF> (juce::ParameterID { maxDrive, 1 }, ru ("Максимайзер: драйв"), Range (-6.0f, 24.0f, 0.01f), 0.0f, attrDb));
    mx->addChild (std::make_unique<APF> (juce::ParameterID { maxCeil, 1 }, ru ("Максимайзер: потолок (dBTP)"), Range (-3.0f, 0.0f, 0.01f), -1.0f, attrDb));
    mx->addChild (std::make_unique<APF> (juce::ParameterID { maxRel, 1 }, ru ("Максимайзер: восстановление"), Range (10.0f, 1000.0f, 0.1f, 0.4f), 120.0f,
                                         juce::AudioParameterFloatAttributes().withLabel (ru ("мс"))));
    mx->addChild (std::make_unique<APF> (juce::ParameterID { maxClip, 1 }, ru ("Максимайзер: клиппер"), Range (0.0f, 100.0f, 0.1f), 0.0f, attrPct));
    mx->addChild (std::make_unique<APF> (juce::ParameterID { maxTarget, 1 }, ru ("Максимайзер: цель LUFS"), Range (-18.0f, -3.0f, 0.1f), -9.0f,
                                         juce::AudioParameterFloatAttributes().withLabel ("LUFS")));
    layout.add (std::move (mx));
    return layout;
}
} // namespace mooni::params
