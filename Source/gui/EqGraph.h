#pragma once
#include "Widgets.h"

namespace mooni::ui
{
/** "Кратер частот": the interactive EQ curve over the measured and target spectra.
    Bands are draggable Kratermites (Кратерята). */
class EqGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqGraph (UiContext&);
    void refresh (float dt);
    void setShowInput (bool b) { showInput = b; }
    void setShowRecommendation (bool b) { showRec = b; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverBand = -1; mousePos = {}; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float kMinF = 20.0f, kMaxF = 20000.0f, kRange = 18.0f;

private:
    juce::Rectangle<float> plot() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float db) const;
    float dbForY (float y) const;
    int bandAt (juce::Point<float> p) const;
    juce::Point<float> nodePos (int band) const;
    void showBandMenu (int band);
    void setParam (const juce::String& id, float v);
    void drawMite (juce::Graphics&, juce::Point<float> c, int band, const dsp::EqBandParams& b, bool active);

    UiContext& ctx;
    std::array<float, LiveData::kSpectrumPoints> specOut {}, specIn {};
    bool showInput = false, showRec = true;
    int dragBand = -1, hoverBand = -1;
    juce::Point<float> mousePos, dragStart;
    float squeak = 0, time = 0;
};
} // namespace mooni::ui
