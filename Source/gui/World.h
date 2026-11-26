#pragma once
#include "Theme.h"

namespace mooni::ui
{
/** The background universe of the Listening Moon station. One room at a time. */
class WorldView : public juce::Component
{
public:
    WorldView();

    void setRoom (int r) { room = r; repaint(); }
    /** level 0..1 (output loudness), width = stereo width %, gr = limiter gain reduction dB (positive) */
    void tick (float dt, float level, float widthPct, float grDb, bool signal, bool reducedMotion);
    void setBandWidths (const std::array<float, 4>& w) { bandWidths = w; }

    std::function<void()> onEarthClicked, onBipClicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Point<float> earthCentre() const;
    float earthRadius() const;

private:
    void buildSky();
    void drawEarth (juce::Graphics&);
    void drawBip (juce::Graphics&);
    void drawObservatory (juce::Graphics&);
    void drawCraterField (juce::Graphics&);
    void drawTides (juce::Graphics&);
    void drawGravity (juce::Graphics&);
    void drawReferencePlanet (juce::Graphics&);
    void drawLogbook (juce::Graphics&);
    juce::Point<float> bipPos() const;

    int room = 0;
    float t = 0, level = 0, width = 0, gr = 0;
    bool signal = false, reduced = false;
    std::array<float, 4> bandWidths { 100, 100, 100, 100 };
    juce::Image sky;
    struct Twinkle { juce::Point<float> p; float r, phase, speed; };
    std::vector<Twinkle> twinkles;
    struct Shooting { juce::Point<float> p, v; float life = -1; } shooting;
    float shootTimer = 6.0f;
    struct Ring { float r; float alpha; };
    std::vector<Ring> rings; float ringTimer = 0;
    juce::Random rng { 42 };
    int frame = 0;
};
} // namespace mooni::ui
