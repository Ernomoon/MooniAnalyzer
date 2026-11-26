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

    // Layout: sidebar | (top bar / room) ; bottom bar spans everything.
    static constexpr int kSide = 210, kRoomW = 1200;
    static constexpr int kW = kSide + kRoomW, kH = 780, kTop = 64, kBottom = 68;

    // for automated screenshots
    void setRoom (int r);
    void forceTick (float dt) { tickOnce (dt); }
    ui::MooniView& getMooni() { return mooni; }
    void showSettings (bool);
    void showAbout (bool);
    void showHelp (bool);

    static juce::String roomName (int r);
    static juce::String roomHint (int r);

private:
    struct Canvas : public juce::Component
    {
        explicit Canvas (MooniEditor& e) : ed (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        MooniEditor& ed;
    };
    struct Bars : public juce::Component
    {
        explicit Bars (MooniEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { ed.paintTopBar (g); ed.paintBottomBar (g); }
        MooniEditor& ed;
    };
    /** Left navigation: one row per section with icon, name, one-line hint and status. */
    struct Sidebar : public juce::Component
    {
        explicit Sidebar (MooniEditor& e) : ed (e) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseUp (const juce::MouseEvent&) override;
        int itemAt (juce::Point<int>) const;
        static juce::Rectangle<int> itemBounds (int i) { return { 10, 76 + i * 66, kSide - 20, 60 }; }
        MooniEditor& ed;
        int hover = -1;
    };
    struct Dimmer : public juce::Component
    {
        float alpha = 0; bool modal = false;
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent&) override { if (modal && onClick) onClick(); }
    };
    struct HelpPanel : public juce::Component
    {
        HelpPanel();
        void paint (juce::Graphics&) override;
        void resized() override { closeBtn.setBounds (getWidth() / 2 - 70, getHeight() - 56, 140, 36); }
        juce::TextButton closeBtn;
        std::function<void()> onClose;
    };

    void timerCallback() override;
    void tickOnce (float dt);
    void paintTopBar (juce::Graphics&);
    void paintBottomBar (juce::Graphics&);
    void updateReport();
    void toggleListenBand (float lo, float hi);
    void closeOverlays();
    int problemsForRoom (int r) const;

    MooniProcessor& proc;
    ui::MooniLookAndFeel laf;
    juce::TooltipWindow tooltips;
    ui::UiContext ctx;
    Canvas canvas { *this };
    ui::WorldView world;
    std::vector<std::unique_ptr<ui::Room>> rooms;
    Bars bars { *this };
    Sidebar sidebar { *this };
    ui::MooniView mooni;
    ui::SpeechBubble bubble;
    ui::BipToast toast;
    Dimmer eclipse, backdrop;
    std::unique_ptr<ui::MooniBrain> brain;
    std::unique_ptr<ui::SettingsPanel> settingsPanel;
    std::unique_ptr<ui::AboutPanel> aboutPanel;
    std::unique_ptr<HelpPanel> helpPanel;

    juce::ToggleButton modEq, modStereo, modMax;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modEqAtt, modStereoAtt, modMaxAtt;
    juce::TextButton undoBtn, redoBtn, abBtn, fairBtn, bypassBtn, helpBtn, gearBtn, infoBtn, stopListenBtn, monitorResetBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    int room = 0;
    double lastTick = 0;
    float eclipseAmt = 0, uiTime = 0;
    ReportPtr shownReport;
    int seenHistory = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MooniEditor)
};
} // namespace mooni
