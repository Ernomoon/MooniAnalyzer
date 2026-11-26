#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Text.h"

namespace mooni::ui
{
/** The palette of the Listening Moon station. */
namespace col
{
    const juce::Colour night0   { 0xff0a081d };   // deep space
    const juce::Colour night1   { 0xff130f31 };
    const juce::Colour night2   { 0xff1c1744 };   // panels
    const juce::Colour night3   { 0xff272059 };   // raised
    const juce::Colour line     { 0xff3b3278 };
    const juce::Colour lilac    { 0xffb9a5f2 };
    const juce::Colour lilacDim { 0xff7d6ebb };
    const juce::Colour cream    { 0xfff6ecdd };   // moon surface
    const juce::Colour creamDim { 0xffcfc2b1 };
    const juce::Colour mint     { 0xff6ff2c2 };   // good / signal
    const juce::Colour amber    { 0xffffc46a };   // attention
    const juce::Colour orange   { 0xffff9b5e };
    const juce::Colour coral    { 0xffff6b7f };   // problem
    const juce::Colour earth    { 0xff52aaff };   // the DAW planet
    const juce::Colour pink     { 0xffff8fbd };
    const juce::Colour text     { 0xffefe9ff };
    const juce::Colour textDim  { 0xffa99fd3 };
    const juce::Colour textFaint{ 0xff6f66a0 };

    juce::Colour forScore (int score);
    juce::Colour forSeverity (float severity);
}

enum class FontKind { Body, Semi, Heavy, Display };

juce::Font font (FontKind kind, float height);
inline juce::Font body (float h)    { return font (FontKind::Body, h); }
inline juce::Font semi (float h)    { return font (FontKind::Semi, h); }
inline juce::Font heavy (float h)   { return font (FontKind::Heavy, h); }
inline juce::Font display (float h) { return font (FontKind::Display, h); }

/** Drawing helpers shared by all rooms. */
void drawGlassCard (juce::Graphics&, juce::Rectangle<float>, float radius = 18.0f, float alpha = 0.86f);
void drawCardTitle (juce::Graphics&, juce::Rectangle<float> card, const juce::String& title, const juce::String& subtitle = {});
void drawStar (juce::Graphics&, juce::Point<float> c, float rOuter, float rInner, float rotation, juce::Colour);
juce::Path starPath (juce::Point<float> c, float rOuter, float rInner, float rotation, int points = 5);
void drawCrater (juce::Graphics&, juce::Point<float> c, float r, juce::Colour base, float depth = 1.0f);
void drawGlow (juce::Graphics&, juce::Point<float> c, float r, juce::Colour, float alpha);
void drawMoonPhase (juce::Graphics&, juce::Rectangle<float> r, float illuminated /* -1..1 correlation */, juce::Colour lit, juce::Colour dark);
void drawChip (juce::Graphics&, juce::Rectangle<float>, const juce::String&, juce::Colour fg, juce::Colour bg, float fontH = 12.0f);
juce::String formatLufs (float v);
juce::String formatDb (float v, int decimals = 1);

class MooniLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MooniLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked,
                            bool hasSub, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 8; }
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool over, bool dragging) override;

private:
    juce::Typeface::Ptr tfBody, tfSemi, tfHeavy, tfDisplay;
};

/** Button "flavours" via a property on the component. */
enum class ButtonStyle { Normal, Primary, Ghost, Danger, Pill };
void setButtonStyle (juce::Button&, ButtonStyle);
} // namespace mooni::ui
