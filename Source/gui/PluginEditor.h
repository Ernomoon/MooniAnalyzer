#pragma once
#include "../core/PluginProcessor.h"
#include "Theme.h"
#include "World.h"
#include "MooniCharacter.h"
#include "MooniBrain.h"
#include "Rooms.h"

namespace mooni
{
class MooniEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MooniEditor (MooniProcessor&);
    ~MooniEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kW = 1200, kH = 780, kTop = 64, kBottom = 68;

    // for automated screenshots
    void setRoom (int r);
    void forceTick (float dt) { tickOnce (dt); }
    ui::MooniView& getMooni() { return mooni; }
    void showSettings (bool);
    void showAbout (bool);

private:
    struct Canvas : public juce::Component
    {
        explicit Canvas (MooniEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        MooniEditor& ed;
    };
    struct Bars : public juce::Component
    {
        explicit Bars (MooniEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { ed.paintTopBar (g); ed.paintBottomBar (g); }
        MooniEditor& ed;
    };
    struct Dimmer : public juce::Component
    {
        float alpha = 0; bool modal = false;
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent&) override { if (modal && onClick) onClick(); }
    };

    void timerCallback() override;
    void tickOnce (float dt);
    void paintTopBar (juce::Graphics&);
    void paintBottomBar (juce::Graphics&);
    void updateReport();
    void toggleListenBand (float lo, float hi);

    MooniProcessor& proc;
    ui::MooniLookAndFeel laf;
    juce::TooltipWindow tooltips;
    ui::UiContext ctx;
    Canvas canvas { *this };
    ui::WorldView world;
    std::vector<std::unique_ptr<ui::Room>> rooms;
    Bars bars { *this };
    ui::MooniView mooni;
    ui::SpeechBubble bubble;
    ui::BipToast toast;
    Dimmer eclipse, backdrop;
    std::unique_ptr<ui::MooniBrain> brain;
    std::unique_ptr<ui::SettingsPanel> settingsPanel;
    std::unique_ptr<ui::AboutPanel> aboutPanel;

    juce::OwnedArray<juce::TextButton> nav;
    juce::TextButton undoBtn, redoBtn, abBtn, fairBtn, bypassBtn, gearBtn, infoBtn, stopListenBtn, monitorResetBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    int room = 0;
    double lastTick = 0;
    float eclipseAmt = 0, uiTime = 0;
    ReportPtr shownReport;
    int seenHistory = -1;
    juce::String lastBottom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MooniEditor)
};
} // namespace mooni
