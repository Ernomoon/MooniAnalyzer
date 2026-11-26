#pragma once
#include "Widgets.h"
#include "EqGraph.h"

namespace mooni::ui
{
struct MooniSpot
{
    juce::Point<float> centre;   // room-local
    float radius = 60;
    float peekLine = 0;          // room-local y; 0 = no clipping
    bool glasses = false;
    juce::Rectangle<float> speechArea;   // room-local
};

class Room : public juce::Component
{
public:
    explicit Room (UiContext& c) : ctx (c) {}
    virtual void refresh (float dt) = 0;      // ~30 Hz
    virtual void reportChanged() {}
    virtual MooniSpot mooniSpot() const = 0;
protected:
    UiContext& ctx;
    void card (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, const juce::String& sub = {})
    {
        drawGlassCard (g, r.toFloat());
        drawCardTitle (g, r.toFloat(), title, sub);
    }
};

juce::String makeTextReport (const Report&, const MooniProcessor&);

// ---------------------------------------------------------------------------
class ObservatoryRoom : public Room
{
public:
    explicit ObservatoryRoom (UiContext&);
    void refresh (float dt) override;
    void reportChanged() override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    ScoreDial dial;
    SubscoreBars subs;
    ProblemList problems;
    MetricGrid metrics;
    juce::TextButton modeFull, modeSmart, listenBtn, finishBtn, applyAllBtn, copyBtn;
    juce::ToggleButton autoBtn;
    juce::String translation;
};

class EqBandStrip : public juce::Component
{
public:
    explicit EqBandStrip (UiContext&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    struct Column
    {
        juce::ToggleButton on;
        juce::ComboBox type, channel;
        juce::Slider freq, gain, q;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aOn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aType, aCh;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aF, aG, aQ;
    };
    UiContext& ctx;
    std::array<Column, params::kEqBands> cols;
};

class EqRoom : public Room
{
public:
    explicit EqRoom (UiContext&);
    void refresh (float dt) override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    EqGraph graph;
    EqBandStrip strip;
    juce::TextButton applyBtn, resetBtn, compareBtn;
    juce::ToggleButton eqOnBtn, showInBtn, showRecBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aEqOn;
};

class WidthBands : public juce::Component
{
public:
    explicit WidthBands (UiContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> ruler() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    UiContext& ctx;
    std::array<juce::Slider, 4> sliders;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> att;
    int dragXo = -1, hoverXo = -1;
};

class TidesRoom : public Room
{
public:
    explicit TidesRoom (UiContext&);
    void refresh (float dt) override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    WidthBands bands;
    TideMirror mirror;
    MoonPhaseMeter phase;
    juce::Slider balance;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aBal;
    juce::ToggleButton onBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aOn;
    juce::TextButton applyBtn, resetBtn, monoBtn, sideBtn;
};

class GravityRoom : public Room
{
public:
    explicit GravityRoom (UiContext&);
    void refresh (float dt) override;
    void reportChanged() override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void runPlan();
    VMeter inMeter { ru ("Вход"), -36, 0 }, outMeter { ru ("Выход"), -36, 0 }, tpMeter { ru ("Пик"), -24, 1 }, grMeter { ru ("Сжатие"), 0, 12 };
    LoudnessTimeline timeline;
    juce::Slider target, drive, ceiling, release, clip;
    juce::Label targetLabel, driveLabel, ceilLabel, relLabel, clipLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aTarget, aDrive, aCeil, aRel, aClip;
    juce::ToggleButton onBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aOn;
    juce::OwnedArray<juce::TextButton> presets, characters;
    juce::TextButton planBtn, applyPlanBtn, fitBtn;
    LoudnessPlanner::Result plan;
    int seenPlan = 0;
    float grSmooth = 0, tpHold = -100, spin = 0;
};

class ReferenceRoom : public Room
{
public:
    explicit ReferenceRoom (UiContext&);
    void refresh (float dt) override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::TextButton loadBtn, clearBtn, restartBtn;
    juce::ToggleButton listenBtn, matchBtn, useBtn;
    std::unique_ptr<juce::FileChooser> chooser;
    int seenChanges = -1;
};

class LogbookRoom : public Room, private juce::ListBoxModel
{
public:
    explicit LogbookRoom (UiContext&);
    void refresh (float dt) override;
    MooniSpot mooniSpot() const override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override { repaint(); }
    juce::ListBox list { "history", this };
    juce::TextButton restoreBtn, copyBtn, abBtn, abCopyBtn, undoBtn, redoBtn;
    std::vector<History::Entry> entries;
    int seenChanges = -1;
};

// ---------------------------------------------------------------------------
class SettingsPanel : public juce::Component
{
public:
    explicit SettingsPanel (UiContext&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void sync();
    std::function<void()> onClose;
private:
    void push();
    UiContext& ctx;
    juce::ComboBox genre, chatter;
    juce::ToggleButton autoListen, reduced, fair, useRef;
    juce::TextButton modeFull, modeSmart, closeBtn;
};

class AboutPanel : public juce::Component
{
public:
    explicit AboutPanel (UiContext& c) : ctx (c) { closeBtn.setButtonText (ru ("Закрыть")); closeBtn.onClick = [this] { if (onClose) onClose(); }; addAndMakeVisible (closeBtn); }
    void paint (juce::Graphics&) override;
    void resized() override { closeBtn.setBounds (getWidth() / 2 - 60, getHeight() - 56, 120, 34); }
    void tick (float dt) { t += dt; repaint(); }
    std::function<void()> onClose;
private:
    UiContext& ctx;
    juce::TextButton closeBtn;
    float t = 0;
};
} // namespace mooni::ui
