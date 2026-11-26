#include "Theme.h"
#include "MooniFonts.h"

namespace mooni::ui
{
juce::Colour col::forScore (int s)
{
    if (s >= 85) return mint;
    if (s >= 68) return amber;
    if (s >= 50) return orange;
    return coral;
}

juce::Colour col::forSeverity (float v)
{
    if (v >= 0.7f) return coral;
    if (v >= 0.4f) return orange;
    if (v >= 0.2f) return amber;
    return lilac;
}

static juce::Typeface::Ptr loadTf (const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}

struct Typefaces
{
    juce::Typeface::Ptr body, semi, heavy, display;
    Typefaces()
    {
        body    = loadTf (MooniFonts::NunitoMedium_ttf, MooniFonts::NunitoMedium_ttfSize);
        semi    = loadTf (MooniFonts::NunitoSemiBold_ttf, MooniFonts::NunitoSemiBold_ttfSize);
        heavy   = loadTf (MooniFonts::NunitoExtraBold_ttf, MooniFonts::NunitoExtraBold_ttfSize);
        display = loadTf (MooniFonts::UnboundedBold_ttf, MooniFonts::UnboundedBold_ttfSize);
    }
    static Typefaces& get() { static Typefaces t; return t; }
};

juce::Font font (FontKind kind, float height)
{
    auto& t = Typefaces::get();
    juce::Typeface::Ptr tf = kind == FontKind::Body ? t.body : kind == FontKind::Semi ? t.semi : kind == FontKind::Heavy ? t.heavy : t.display;
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

juce::String formatLufs (float v) { return v <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : juce::String (v, 1); }
juce::String formatDb (float v, int decimals) { return v <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : juce::String (v, decimals); }

// ---------------------------------------------------------------------------
void drawGlassCard (juce::Graphics& g, juce::Rectangle<float> r, float radius, float alpha)
{
    // soft drop shadow
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f * (float) i));
        g.fillRoundedRectangle (r.translated (0, 2.0f * (float) i).expanded ((float) i), radius + (float) i);
    }
    juce::ColourGradient grad (col::night3.withAlpha (alpha), r.getX(), r.getY(), col::night1.withAlpha (alpha), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    // top highlight
    juce::ColourGradient hl (col::lilac.withAlpha (0.10f), r.getX(), r.getY(), col::lilac.withAlpha (0.0f), r.getX(), r.getY() + 40, false);
    g.setGradientFill (hl);
    g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
    g.setColour (col::line.withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void drawCardTitle (juce::Graphics& g, juce::Rectangle<float> card, const juce::String& title, const juce::String& subtitle)
{
    g.setColour (col::text);
    g.setFont (display (13.0f));
    g.drawText (title.toUpperCase(), card.getX() + 18, card.getY() + 12, card.getWidth() - 36, 18, juce::Justification::centredLeft);
    if (subtitle.isNotEmpty())
    {
        g.setColour (col::textDim);
        g.setFont (body (12.5f));
        g.drawText (subtitle, card.getX() + 18, card.getY() + 31, card.getWidth() - 36, 16, juce::Justification::centredLeft);
    }
}

juce::Path starPath (juce::Point<float> c, float ro, float ri, float rot, int points)
{
    juce::Path p;
    for (int i = 0; i < points * 2; ++i)
    {
        const float a = rot + juce::MathConstants<float>::pi * (float) i / (float) points - juce::MathConstants<float>::halfPi;
        const float r = (i % 2 == 0) ? ro : ri;
        const auto pt = c + juce::Point<float> (std::cos (a) * r, std::sin (a) * r);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();
    return p;
}

void drawStar (juce::Graphics& g, juce::Point<float> c, float ro, float ri, float rot, juce::Colour colour)
{
    auto p = starPath (c, ro, ri, rot);
    g.setColour (colour);
    g.fillPath (p);
    g.strokePath (p, juce::PathStrokeType (ro * 0.18f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawCrater (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour base, float depth)
{
    auto e = juce::Rectangle<float> (r * 2.0f, r * 1.6f).withCentre (c);
    g.setColour (base.darker (0.25f * depth));
    g.fillEllipse (e);
    g.setColour (base.darker (0.45f * depth));
    g.fillEllipse (e.reduced (r * 0.18f).translated (r * 0.08f, r * 0.08f));
    g.setColour (base.brighter (0.25f).withAlpha (0.6f));
    g.drawEllipse (e.translated (-r * 0.05f, -r * 0.05f), juce::jmax (1.0f, r * 0.12f));
}

void drawGlow (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, float alpha)
{
    juce::ColourGradient grad (colour.withAlpha (alpha), c.x, c.y, colour.withAlpha (0.0f), c.x + r, c.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
}

void drawMoonPhase (juce::Graphics& g, juce::Rectangle<float> r, float corr, juce::Colour lit, juce::Colour dark)
{
    // correlation +1 = full moon, 0 = half, -1 = new moon (dark)
    const float k = juce::jlimit (-1.0f, 1.0f, corr);
    auto c = r.getCentre(); const float rad = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (dark);
    g.fillEllipse (r);
    juce::Path litP;
    // terminator ellipse width: illuminated fraction f = (k+1)/2
    const float f = (k + 1.0f) * 0.5f;
    const float term = rad * std::abs (1.0f - 2.0f * f);
    litP.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, juce::MathConstants<float>::pi, true);   // right half
    if (f >= 0.5f) litP.addCentredArc (c.x, c.y, term, rad, 0.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, false);
    else           litP.addCentredArc (c.x, c.y, term, rad, 0.0f, juce::MathConstants<float>::pi, 0.0f, false);
    litP.closeSubPath();
    g.setColour (lit);
    g.fillPath (litP);
    g.setColour (lit.withAlpha (0.25f));
    drawCrater (g, c + juce::Point<float> (rad * 0.25f, -rad * 0.2f), rad * 0.16f, lit, 0.6f);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawEllipse (r, 1.0f);
}

void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour fg, juce::Colour bg, float fontH)
{
    g.setColour (bg);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (fg);
    g.setFont (semi (fontH));
    g.drawFittedText (t, r.toNearestInt().reduced (6, 0), juce::Justification::centred, 1);
}

// ---------------------------------------------------------------------------
MooniLookAndFeel::MooniLookAndFeel()
{
    auto& t = Typefaces::get();
    tfBody = t.body; tfSemi = t.semi; tfHeavy = t.heavy; tfDisplay = t.display;
    setDefaultSansSerifTypeface (tfSemi);

    setColour (juce::ResizableWindow::backgroundColourId, col::night0);
    setColour (juce::TextButton::buttonColourId, col::night3);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::night0);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::night2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::lilac.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::lilac.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, col::mint);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::ScrollBar::thumbColourId, col::lilacDim);
}

juce::Typeface::Ptr MooniLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.isBold()) return tfHeavy;
    return tfSemi;
}

void setButtonStyle (juce::Button& b, ButtonStyle s) { b.getProperties().set ("mstyle", (int) s); }
static ButtonStyle styleOf (const juce::Component& c) { return (ButtonStyle) (int) c.getProperties().getWithDefault ("mstyle", 0); }

void MooniLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto st = styleOf (b);
    const bool on = b.getToggleState();
    const float rad = r.getHeight() * (st == ButtonStyle::Pill ? 0.5f : 0.32f);
    const float a = b.isEnabled() ? 1.0f : 0.4f;

    juce::Colour fill = col::night3, border = col::line, glow = juce::Colours::transparentBlack;
    switch (st)
    {
        case ButtonStyle::Primary: fill = col::mint; border = col::mint.brighter (0.3f); glow = col::mint; break;
        case ButtonStyle::Danger:  fill = on ? col::coral : col::coral.withAlpha (0.18f); border = col::coral.withAlpha (on ? 1.0f : 0.7f); glow = on ? col::coral : glow; break;
        case ButtonStyle::Ghost:   fill = juce::Colours::transparentBlack; border = col::line; break;
        case ButtonStyle::Pill:    fill = on ? col::lilac : col::night3.withAlpha (0.7f); border = on ? col::lilac.brighter (0.2f) : col::line; break;
        case ButtonStyle::Normal:  default: if (on) { fill = col::lilac.withAlpha (0.85f); border = col::lilac; } break;
    }
    if (over) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.12f);
    if (! glow.isTransparent() && (over || (on && st == ButtonStyle::Danger))) drawGlow (g, r.getCentre(), r.getWidth() * 0.6f, glow, 0.18f);
    g.setColour (fill.withMultipliedAlpha (a));
    g.fillRoundedRectangle (r, rad);
    g.setColour (border.withMultipliedAlpha (a));
    g.drawRoundedRectangle (r, rad, 1.0f);
}

juce::Font MooniLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return heavy (juce::jlimit (11.0f, 16.0f, (float) h * 0.45f)); }

void MooniLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const auto st = styleOf (b);
    const bool on = b.getToggleState();
    juce::Colour c = col::text;
    if (st == ButtonStyle::Primary) c = col::night0;
    else if (st == ButtonStyle::Danger) c = on ? col::night0 : col::coral.brighter (0.3f);
    else if ((st == ButtonStyle::Pill || st == ButtonStyle::Normal) && on) c = col::night0;
    g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (8, 2), juce::Justification::centred, 2, 0.9f);
}

void MooniLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = std::min (20.0f, r.getHeight() - 4.0f);
    auto sw = juce::Rectangle<float> (h * 1.8f, h).withCentre ({ r.getX() + h * 0.9f + 2.0f, r.getCentreY() });
    const bool on = b.getToggleState();
    g.setColour ((on ? col::mint : col::night0).withAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (sw, h * 0.5f);
    g.setColour (on ? col::mint.brighter (0.3f) : (over ? col::lilac : col::line));
    g.drawRoundedRectangle (sw, h * 0.5f, 1.0f);
    // the knob is a tiny moon
    auto knob = juce::Rectangle<float> (h - 4, h - 4).withCentre ({ on ? sw.getRight() - h * 0.5f : sw.getX() + h * 0.5f, sw.getCentreY() });
    g.setColour (on ? col::night0 : col::creamDim);
    g.fillEllipse (knob);
    if (! on) { g.setColour (col::creamDim.darker (0.3f)); g.fillEllipse (knob.reduced (knob.getWidth() * 0.32f).translated (knob.getWidth() * 0.1f, -knob.getWidth() * 0.05f)); }
    g.setColour (col::text.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (semi (13.0f));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (sw.getRight() + 8.0f - r.getX()).toNearestInt(), juce::Justification::centredLeft, 2);
}

void MooniLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float size = std::min (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId) == juce::Colour() ? col::lilac : s.findColour (juce::Slider::rotarySliderFillColourId);
    const float a = s.isEnabled() ? 1.0f : 0.35f;

    juce::Path track; track.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, start, end, true);
    g.setColour (col::night0.withMultipliedAlpha (a));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zeroPos = bipolar ? start + (end - start) * (float) s.valueToProportionOfLength (0.0) : start;
    const float ang = start + pos * (end - start);
    juce::Path val; val.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, std::min (zeroPos, ang), std::max (zeroPos, ang), true);
    g.setColour (accent.withMultipliedAlpha (a));
    g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body: a small moon with a crater pointer
    auto body = r.reduced (rad * 0.28f);
    juce::ColourGradient gr (col::cream, body.getX(), body.getY(), col::creamDim.darker (0.25f), body.getRight(), body.getBottom(), false);
    g.setGradientFill (gr);
    g.fillEllipse (body);
    const auto tip = c + juce::Point<float> (std::sin (ang), -std::cos (ang)) * (body.getWidth() * 0.32f);
    g.setColour (col::night1.withAlpha (0.85f * a));
    g.fillEllipse (juce::Rectangle<float> (body.getWidth() * 0.2f, body.getWidth() * 0.2f).withCentre (tip));
}

void MooniLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vert = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto accent = s.findColour (juce::Slider::trackColourId) == juce::Colour() ? col::lilac : s.findColour (juce::Slider::trackColourId);
    const float a = s.isEnabled() ? 1.0f : 0.35f;
    const float t = 5.0f;
    juce::Rectangle<float> track = vert ? juce::Rectangle<float> (t, r.getHeight()).withCentre (r.getCentre())
                                        : juce::Rectangle<float> (r.getWidth(), t).withCentre (r.getCentre());
    g.setColour (col::night0.withMultipliedAlpha (a));
    g.fillRoundedRectangle (track, t * 0.5f);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? (vert ? r.getBottom() - (float) s.valueToProportionOfLength (0.0) * r.getHeight()
                                       : r.getX() + (float) s.valueToProportionOfLength (0.0) * r.getWidth())
                               : (vert ? r.getBottom() : r.getX());
    juce::Rectangle<float> fill = vert ? juce::Rectangle<float> (track.getX(), std::min (pos, zero), t, std::abs (zero - pos))
                                       : juce::Rectangle<float> (std::min (pos, zero), track.getY(), std::abs (pos - zero), t);
    g.setColour (accent.withMultipliedAlpha (a));
    g.fillRoundedRectangle (fill, t * 0.5f);
    const auto knobC = vert ? juce::Point<float> (r.getCentreX(), pos) : juce::Point<float> (pos, r.getCentreY());
    auto knob = juce::Rectangle<float> (15, 15).withCentre (knobC);
    g.setColour (col::cream.withMultipliedAlpha (a));
    g.fillEllipse (knob);
    g.setColour (accent.withMultipliedAlpha (a));
    g.drawEllipse (knob, 1.5f);
}

juce::Label* MooniLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textColourId, col::text);
    l->setColour (juce::TextEditor::backgroundColourId, col::night0);
    l->setColour (juce::TextEditor::outlineColourId, col::lilacDim);
    l->setFont (semi (12.0f));
    return l;
}

juce::Slider::SliderLayout MooniLookAndFeel::getSliderLayout (juce::Slider& s)
{
    return LookAndFeel_V4::getSliderLayout (s);
}

void MooniLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::night0.withAlpha (0.8f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (cb.isMouseOver (true) ? col::lilac : col::line);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (col::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font MooniLookAndFeel::getComboBoxFont (juce::ComboBox&) { return semi (12.5f); }
void MooniLookAndFeel::positionComboBoxText (juce::ComboBox& cb, juce::Label& l)
{
    l.setBounds (6, 0, cb.getWidth() - 22, cb.getHeight());
    l.setFont (getComboBoxFont (cb));
}

void MooniLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::night2);
    g.setColour (col::line);
    g.drawRect (0, 0, w, h, 1);
}

void MooniLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool hl, bool ticked,
                                          bool hasSub, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* tc)
{
    if (sep) { g.setColour (col::line); g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY())); return; }
    juce::ignoreUnused (hasSub, shortcut, icon, tc);
    auto r = area.reduced (4, 1);
    if (hl && active) { g.setColour (col::lilac.withAlpha (0.22f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); }
    g.setColour (active ? col::text : col::textFaint);
    g.setFont (semi (13.0f));
    g.drawText (text, r.withTrimmedLeft (24), juce::Justification::centredLeft);
    if (ticked) { g.setColour (col::mint); g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ (float) r.getX() + 12.0f, (float) r.getCentreY() })); }
}

juce::Font MooniLookAndFeel::getPopupMenuFont() { return semi (13.0f); }

void MooniLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (col::night0);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::lilacDim);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    juce::AttributedString as;
    as.append (text, body (13.0f), col::text);
    as.setJustification (juce::Justification::centredLeft);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (as, (float) w - 20.0f);
    tl.draw (g, r.reduced (10.0f, 7.0f));
}

juce::Rectangle<int> MooniLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString as; as.append (tip, body (13.0f), col::text);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (as, 300.0f);
    const int w = (int) std::ceil (tl.getWidth()) + 22, h = (int) std::ceil (tl.getHeight()) + 16;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 6) : pos.y + 18, w, h).constrainedWithin (parent);
}

void MooniLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tsz, bool over, bool)
{
    juce::Rectangle<float> thumb = vertical ? juce::Rectangle<float> ((float) x + 2, (float) ts, (float) w - 4, (float) tsz)
                                            : juce::Rectangle<float> ((float) ts, (float) y + 2, (float) tsz, (float) h - 4);
    g.setColour ((over ? col::lilac : col::lilacDim).withAlpha (0.6f));
    g.fillRoundedRectangle (thumb, 3.0f);
}

void MooniLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::night0);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 7.0f);
}

void MooniLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te)
{
    g.setColour (te.hasKeyboardFocus (true) ? col::mint : col::line);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 7.0f, 1.0f);
}

void MooniLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (! l.isBeingEdited())
    {
        g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
        g.setFont (getLabelFont (l));
        g.drawFittedText (l.getText(), l.getLocalBounds().reduced (2, 0), l.getJustificationType(), 3, 0.9f);
    }
}

void MooniLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour ((over || dragging ? col::lilac : col::lilacDim).withAlpha (0.7f));
    for (int i = 0; i < 3; ++i)
        g.fillEllipse ((float) w - 5.0f - (float) i * 5.0f, (float) h - 5.0f - (float) (2 - i) * 0.0f - 1.0f, 3.0f, 3.0f),
        g.fillEllipse ((float) w - 5.0f, (float) h - 6.0f - (float) i * 5.0f, 3.0f, 3.0f);
}
} // namespace mooni::ui
