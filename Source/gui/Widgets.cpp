#include "Widgets.h"
#include "MooniBrain.h"

namespace mooni::ui
{
using juce::Point;
static constexpr float PI = juce::MathConstants<float>::pi;

int roomForModule (Module m)
{
    switch (m)
    {
        case Module::Eq: return 1;
        case Module::Stereo: return 2;
        case Module::Maximizer: return 3;
        case Module::Reference: return 4;
        case Module::Logbook: return 5;
        case Module::Observatory: case Module::None: default: return 0;
    }
}

// ===========================================================================
void ScoreDial::setReport (ReportPtr r, bool st)
{
    if (r != report) { if (r && r->valid && (! report || report->score != r->score)) pulse = 1.0f; report = std::move (r); }
    stale = st;
    target = report && report->valid ? (float) report->score : 0.0f;
    juce::String tip;
    if (report && report->valid)
    {
        tip << ru ("Оценка технического качества и готовности к релизу (1–100).\n");
        for (auto& s : report->subscores) tip << s.name << ": " << s.value << ru (" (вес ") << juce::roundToInt (s.weight * 100) << "%)\n";
        tip << ru ("Профиль: ") << genreProfiles()[(size_t) report->genreProfile].name;
    }
    setTooltip (tip);
}

void ScoreDial::tick (float dt)
{
    const float before = shown;
    shown += (target - shown) * std::min (1.0f, dt * 3.0f);
    if (std::abs (target - shown) < 0.2f) shown = target;
    pulse = std::max (0.0f, pulse - dt * 0.8f);
    if (std::abs (before - shown) > 0.01f || pulse > 0) repaint();
}

void ScoreDial::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float size = std::min (r.getWidth(), r.getHeight()) - 10.0f;
    auto dial = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
    const auto c = dial.getCentre();
    const float rad = size * 0.5f;
    const bool has = report && report->valid;
    const auto accent = has ? col::forScore (juce::roundToInt (shown)) : col::lilacDim;

    if (pulse > 0) drawGlow (g, c, rad * (1.15f + 0.25f * pulse), accent, 0.35f * pulse);
    // moon disk background with craters
    juce::ColourGradient bg (col::night3, c.x - rad * 0.4f, c.y - rad * 0.5f, col::night0, c.x + rad * 0.5f, c.y + rad * 0.7f, true);
    g.setGradientFill (bg); g.fillEllipse (dial.reduced (rad * 0.1f));
    drawCrater (g, c + Point<float> (-rad * 0.42f, rad * 0.32f), rad * 0.1f, col::night3, 0.6f);
    drawCrater (g, c + Point<float> (rad * 0.4f, -rad * 0.38f), rad * 0.07f, col::night3, 0.6f);

    const float start = -PI * 0.8f, end = PI * 0.8f;
    juce::Path track; track.addCentredArc (c.x, c.y, rad * 0.92f, rad * 0.92f, 0, start, end, true);
    g.setColour (col::night0); g.strokePath (track, juce::PathStrokeType (rad * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // tick marks every 10
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const auto p1 = c + Point<float> (std::sin (a), -std::cos (a)) * (rad * 0.80f), p2 = c + Point<float> (std::sin (a), -std::cos (a)) * (rad * 0.75f);
        g.setColour (col::lilacDim.withAlpha (0.6f)); g.drawLine ({ p1, p2 }, 1.5f);
    }
    if (has)
    {
        const float v = juce::jlimit (0.0f, 100.0f, shown) / 100.0f;
        juce::Path arc; arc.addCentredArc (c.x, c.y, rad * 0.92f, rad * 0.92f, 0, start, start + (end - start) * v, true);
        juce::ColourGradient ag (col::coral, c.x - rad, c.y, accent, c.x + rad, c.y, false);
        g.setGradientFill (ag);
        g.strokePath (arc, juce::PathStrokeType (rad * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float a = start + (end - start) * v;
        const auto tip = c + Point<float> (std::sin (a), -std::cos (a)) * (rad * 0.92f);
        drawGlow (g, tip, rad * 0.16f, accent, 0.7f);
        drawStar (g, tip, rad * 0.07f, rad * 0.03f, a, col::cream);
    }

    g.setColour (has ? col::text : col::textDim);
    g.setFont (display (rad * 0.5f));
    g.drawText (has ? juce::String (juce::roundToInt (shown)) : juce::String ("?"), dial.withSizeKeepingCentre (size, rad * 0.6f).translated (0, -rad * 0.08f), juce::Justification::centred);
    g.setFont (semi (rad * 0.11f)); g.setColour (col::textDim);
    g.drawText (has ? ru ("из 100") : ru ("ещё не слушал"), dial.withSizeKeepingCentre (size, rad * 0.2f).translated (0, rad * 0.28f), juce::Justification::centred);
    if (has)
    {
        g.setFont (heavy (rad * 0.13f)); g.setColour (accent);
        g.drawText (MooniBrain::gradeWord (report->score).toUpperCase(), dial.withSizeKeepingCentre (size, rad * 0.2f).translated (0, rad * 0.5f), juce::Justification::centred);
    }
    if (stale)
    {
        auto chip = juce::Rectangle<float> (rad * 1.3f, rad * 0.17f).withCentre (c + Point<float> (0, rad * 0.78f));
        drawChip (g, chip, ru ("настройки изменились"), col::night0, col::amber, rad * 0.085f);
    }
}

// ===========================================================================
void SubscoreBars::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (! report || ! report->valid)
    {
        g.setColour (col::textFaint); g.setFont (body (13.0f));
        g.drawFittedText (ru ("Здесь появится разбор оценки: баланс, динамика, пики, стерео, фаза и чистота."), r.toNearestInt().reduced (10), juce::Justification::centred, 3);
        return;
    }
    const int n = (int) report->subscores.size();
    const float rowH = r.getHeight() / (float) std::max (1, n);
    for (int i = 0; i < n; ++i)
    {
        const auto& s = report->subscores[(size_t) i];
        auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) i, r.getWidth(), rowH).reduced (0, 3);
        if (i == hover) { g.setColour (col::lilac.withAlpha (0.08f)); g.fillRoundedRectangle (row.expanded (4, 2), 6); }
        g.setColour (col::text); g.setFont (semi (13.0f));
        g.drawText (s.name, row.withWidth (150), juce::Justification::centredLeft);
        auto bar = row.withTrimmedLeft (156).withTrimmedRight (40).withSizeKeepingCentre (row.getWidth() - 196, 8);
        g.setColour (col::night0); g.fillRoundedRectangle (bar, 4);
        g.setColour (col::forScore (s.value)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) s.value / 100.0f), 4);
        g.setColour (col::forScore (s.value)); g.setFont (heavy (13.0f));
        g.drawText (juce::String (s.value), row.removeFromRight (36), juce::Justification::centredRight);
        if (i == hover)
        {
            g.setColour (col::textDim); g.setFont (body (11.5f));
            g.drawText (s.note, juce::Rectangle<float> (bar.getX(), bar.getBottom() + 1, bar.getWidth() + 40, 13), juce::Justification::centredLeft);
        }
    }
}

void SubscoreBars::mouseMove (const juce::MouseEvent& e)
{
    if (! report || report->subscores.empty()) return;
    const int h = (int) (e.position.y / ((float) getHeight() / (float) report->subscores.size()));
    if (h != hover) { hover = h; repaint(); }
}

// ===========================================================================
ProblemCard::ProblemCard (UiContext& c, const Problem& p, int idx) : ctx (c), problem (p), index (idx)
{
    const int room = roomForModule (p.module);
    const juce::String roomNames[] = { ru ("Обсерватория"), ru ("Кратер частот"), ru ("Приливы"), ru ("Гравитация"), ru ("Эталон"), ru ("Журнал") };
    openBtn.setButtonText (ru ("Открыть: ") + roomNames[room]);
    openBtn.onClick = [this, room] { if (ctx.goRoom) ctx.goRoom (room); };
    setButtonStyle (openBtn, ButtonStyle::Ghost);
    addChildComponent (openBtn);

    listenBtn.setButtonText (ru ("Подслушать"));
    listenBtn.setTooltip (ru ("Слушать только этот диапазон частот (повторное нажатие — выключить)."));
    listenBtn.onClick = [this] { if (ctx.listenBand) ctx.listenBand (problem.freqLo, problem.freqHi); };
    setButtonStyle (listenBtn, ButtonStyle::Ghost);
    addChildComponent (listenBtn);

    const bool canFix = (p.module == Module::Eq && ctx.report && ! ctx.report->eq.empty()) || p.module == Module::Stereo;
    fixBtn.setButtonText (p.module == Module::Eq ? ru ("Применить EQ") : ru ("Применить стерео"));
    fixBtn.onClick = [this]
    {
        if (! ctx.report) return;
        if (problem.module == Module::Eq) { ctx.proc.applyEqRecommendations (*ctx.report); if (ctx.action) ctx.action ("applyEq", {}); }
        else { ctx.proc.applyStereoRecommendations (*ctx.report); if (ctx.action) ctx.action ("applyStereo", {}); }
    };
    setButtonStyle (fixBtn, ButtonStyle::Primary);
    fixBtn.setVisible (false);
    if (canFix) addChildComponent (fixBtn);
}

int ProblemCard::preferredHeight (int width) const
{
    auto measure = [width] (const juce::String& t, juce::Font f)
    {
        juce::AttributedString as; as.append (t, f, col::text); as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout (as, (float) width - 34.0f); return (int) std::ceil (tl.getHeight());
    };
    int h = 14 + 20 + 4 + measure (problem.what, body (12.5f)) + 12;
    if (expanded) h += measure (problem.why, body (12.5f)) + 10 + measure (problem.fix, semi (12.5f)) + 12 + 32;
    return h;
}

void ProblemCard::resized()
{
    const bool showButtons = expanded;
    auto r = getLocalBounds().reduced (14, 8);
    auto row = r.removeFromBottom (28);
    openBtn.setVisible (showButtons);
    listenBtn.setVisible (showButtons && problem.freqHi > problem.freqLo);
    fixBtn.setVisible (showButtons && fixBtn.getParentComponent() != nullptr);
    if (! showButtons) return;
    if (fixBtn.isVisible()) fixBtn.setBounds (row.removeFromRight (124).reduced (2, 0));
    if (listenBtn.isVisible()) listenBtn.setBounds (row.removeFromRight (110).reduced (2, 0));
    openBtn.setBounds (row.removeFromLeft (std::min (row.getWidth(), 190)).reduced (2, 0));
}

void ProblemCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    const auto sc = col::forSeverity (problem.severity);
    g.setColour (col::night0.withAlpha (isMouseOver (true) ? 0.75f : 0.55f));
    g.fillRoundedRectangle (r, 12);
    g.setColour (sc.withAlpha (0.9f));
    g.fillRoundedRectangle (r.withWidth (4).reduced (0, 8), 2);
    g.setColour (col::line); g.drawRoundedRectangle (r, 12, 1);

    auto t = r.reduced (16, 0).withTrimmedTop (10);
    g.setColour (col::text); g.setFont (heavy (14.0f));
    auto head = t.removeFromTop (20);
    // severity dots
    const int dots = 1 + juce::roundToInt (problem.severity * 3.0f);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (i < dots ? sc : col::line);
        g.fillEllipse (head.getRight() - 46.0f + (float) i * 11.0f, head.getCentreY() - 3.5f, 7, 7);
    }
    g.setColour (col::text);
    g.drawText (juce::String (index + 1) + ". " + problem.title, head.withTrimmedRight (54), juce::Justification::centredLeft);
    if (! problem.measured)
        drawChip (g, juce::Rectangle<float> (head.getRight() - 120.0f, head.getY() + 2, 64, 16), ru ("оценка"), col::night0, col::lilacDim, 10.5f);
    t.removeFromTop (4);

    auto drawPara = [&] (const juce::String& s, juce::Font f, juce::Colour c)
    {
        juce::AttributedString as; as.append (s, f, c); as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout (as, t.getWidth());
        tl.draw (g, t.removeFromTop (tl.getHeight()));
    };
    drawPara (problem.what, body (12.5f), col::textDim);
    if (expanded)
    {
        t.removeFromTop (8);
        drawPara (problem.why, body (12.5f), col::text.withAlpha (0.85f));
        t.removeFromTop (8);
        drawPara (ru ("→ ") + problem.fix, semi (12.5f), col::mint);
    }
}

void ProblemCard::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked()) { expanded = ! expanded; if (onToggle) onToggle(); }
}

ProblemList::ProblemList (UiContext& c) : ctx (c)
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (8);
    addAndMakeVisible (view);
}

void ProblemList::setReport (ReportPtr r)
{
    if (r == report) return;
    report = std::move (r);
    cards.clear();
    content.removeAllChildren();
    if (report && report->valid)
    {
        const int n = std::min<int> (12, (int) report->problems.size());
        for (int i = 0; i < n; ++i)
        {
            auto card = std::make_unique<ProblemCard> (ctx, report->problems[(size_t) i], i);
            card->expanded = i == 0;
            card->onToggle = [this] { layoutCards(); };
            content.addAndMakeVisible (*card);
            cards.push_back (std::move (card));
        }
    }
    layoutCards();
    repaint();
}

void ProblemList::resized() { view.setBounds (getLocalBounds()); layoutCards(); }

void ProblemList::layoutCards()
{
    const int w = std::max (10, getWidth() - 12);
    int y = 0;
    for (auto& c : cards) { const int h = c->preferredHeight (w); c->setBounds (0, y, w, h); y += h + 8; }
    content.setSize (w, std::max (y, 10));
}

void ProblemList::paint (juce::Graphics& g)
{
    if (cards.empty())
    {
        g.setColour (col::textFaint); g.setFont (body (14.0f));
        const auto txt = report && report->valid ? ru ("Серьёзных проблем не нашёл. Я впечатлён.") : ru ("Пока тишина… Включи воспроизведение, и я расскажу, что слышу.");
        g.drawFittedText (txt, getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
}

// ===========================================================================
void MetricGrid::paint (juce::Graphics& g)
{
    if (metrics.empty())
    {
        g.setColour (col::textFaint); g.setFont (body (13.0f));
        g.drawFittedText (ru ("Громкость, пики, динамика и стерео появятся после первого прослушивания."), getLocalBounds().reduced (10), juce::Justification::centred, 2);
        return;
    }
    const int rows = ((int) metrics.size() + cols - 1) / cols;
    const float cw = (float) getWidth() / (float) cols, rh = (float) getHeight() / (float) rows;
    for (size_t i = 0; i < metrics.size(); ++i)
    {
        const auto& m = metrics[i];
        auto cell = juce::Rectangle<float> (cw * (float) ((int) i % cols), rh * (float) ((int) i / cols), cw, rh).reduced (4);
        g.setColour (col::night0.withAlpha (0.45f)); g.fillRoundedRectangle (cell, 10);
        g.setColour (col::textDim); g.setFont (semi (11.0f));
        g.drawText (m.label, cell.reduced (10, 6).removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (m.colour); g.setFont (heavy (std::min (22.0f, rh * 0.38f)));
        auto vr = cell.reduced (10, 4).withTrimmedTop (16);
        g.drawText (m.value, vr, juce::Justification::centredLeft);
        const float vw = juce::GlyphArrangement::getStringWidth (heavy (std::min (22.0f, rh * 0.38f)), m.value);
        g.setColour (col::textFaint); g.setFont (semi (11.0f));
        g.drawText (m.unit, vr.withTrimmedLeft (vw + 5), juce::Justification::centredLeft);
    }
}

juce::String MetricGrid::getTooltip()
{
    if (metrics.empty()) return {};
    const auto p = getMouseXYRelative();
    const int rows = ((int) metrics.size() + cols - 1) / cols;
    const int cx = p.x * cols / std::max (1, getWidth()), cy = p.y * rows / std::max (1, getHeight());
    const int i = cy * cols + cx;
    return i >= 0 && i < (int) metrics.size() ? metrics[(size_t) i].tip : juce::String();
}

// ===========================================================================
void VMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto label = r.removeFromBottom (18);
    auto bar = r.reduced (r.getWidth() * 0.28f, 4);
    g.setColour (col::night0); g.fillRoundedRectangle (bar, 5);
    auto toY = [&] (float v) { return bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, (v - minV) / (maxV - minV)); };
    if (! inverted)
    {
        const float y = toY (value);
        juce::ColourGradient gr (cLo, bar.getX(), bar.getBottom(), cHi, bar.getX(), bar.getY(), false);
        gr.addColour (0.75, col::amber);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (bar.withTop (y), 5);
        if (holdV > minV) { g.setColour (col::cream); g.fillRect (bar.getX(), toY (holdV) - 1, bar.getWidth(), 2.0f); }
    }
    else
    {
        // value is a positive dB amount, grows from the top
        const float h = bar.getHeight() * juce::jlimit (0.0f, 1.0f, value / maxV);
        g.setColour (col::coral.withAlpha (0.85f)); g.fillRoundedRectangle (bar.withHeight (h), 5);
    }
    if (target > minV && target < maxV)
    {
        const float y = toY (target);
        g.setColour (col::mint); g.drawLine (bar.getX() - 6, y, bar.getRight() + 6, y, 2.0f);
    }
    g.setColour (col::textDim); g.setFont (semi (11.0f));
    g.drawText (name, label, juce::Justification::centred);
}

void MoonPhaseMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto moon = showText ? r.removeFromLeft (r.getHeight()) : r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    const auto lit = corr >= 0.3f ? col::cream : (corr >= 0 ? col::amber : col::coral);
    drawMoonPhase (g, moon.reduced (2), corr, lit, col::night0.brighter (0.15f));
    if (showText)
    {
        g.setColour (lit); g.setFont (heavy (14.0f));
        g.drawText (juce::String (corr, 2), r.withTrimmedLeft (6), juce::Justification::centredLeft);
    }
}

// ===========================================================================
void TideMirror::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (6);
    const float size = std::min (r.getWidth(), r.getHeight());
    auto circle = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
    const auto c = circle.getCentre(); const float rad = size * 0.5f;
    juce::ColourGradient bg (juce::Colour (0xff12305a), c.x, c.y, col::night0, c.x + rad, c.y, true);
    g.setGradientFill (bg); g.fillEllipse (circle);
    g.setColour (col::earth.withAlpha (0.25f));
    g.drawEllipse (circle, 1.5f); g.drawEllipse (circle.reduced (rad * 0.5f), 1.0f);
    g.drawLine (c.x, circle.getY(), c.x, circle.getBottom(), 1.0f);
    g.drawLine (circle.getX(), c.y, circle.getRight(), c.y, 1.0f);
    g.setColour (col::textFaint); g.setFont (semi (10.5f));
    g.drawText ("M", juce::Rectangle<float> (20, 14).withCentre ({ c.x, circle.getY() + 10 }), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (20, 14).withCentre ({ c.x - rad * 0.72f, c.y - rad * 0.72f }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (20, 14).withCentre ({ c.x + rad * 0.72f, c.y - rad * 0.72f }), juce::Justification::centred);

    float peak = 1e-4f;
    for (size_t i = 0; i < data.size(); i += 2) peak = std::max (peak, std::max (std::abs (data[i]), std::abs (data[i + 1])));
    const float targetGain = 0.85f / peak;
    gain += (targetGain - gain) * 0.1f;
    gain = juce::jlimit (0.5f, 60.0f, gain);
    const auto dot = correlation >= 0.2f ? col::mint : (correlation >= 0 ? col::amber : col::coral);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addEllipse (circle); g.reduceClipRegion (clip);
    for (size_t i = 0; i + 1 < data.size(); i += 2)
    {
        const float x = c.x + data[i] * gain * rad, y = c.y - data[i + 1] * gain * rad;
        g.setColour (dot.withAlpha (0.18f + 0.5f * (float) i / (float) data.size()));
        g.fillEllipse (x - 1.2f, y - 1.2f, 2.4f, 2.4f);
    }
}

void LoudnessTimeline::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::night0.withAlpha (0.5f)); g.fillRoundedRectangle (r, 10);
    auto plot = r.reduced (34, 10).withTrimmedLeft (-4);
    const float lo = -36, hi = 0;
    auto toY = [&] (float v) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); };
    g.setFont (semi (10.0f));
    for (float v = -30; v <= 0; v += 6)
    {
        g.setColour (col::line.withAlpha (0.6f)); g.drawHorizontalLine ((int) toY (v), plot.getX(), plot.getRight());
        g.setColour (col::textFaint); g.drawText (juce::String ((int) v), juce::Rectangle<float> (r.getX() + 4, toY (v) - 7, 26, 14), juce::Justification::centredRight);
    }
    auto drawHist = [&] (const std::vector<float>& h, juce::Colour c, float thick, bool fill)
    {
        if (h.size() < 2) return;
        juce::Path p; const size_t n = h.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float x = plot.getRight() - plot.getWidth() * (float) (n - 1 - i) / (float) (LiveData::kHistory - 1);
            const float y = toY (std::max (lo, h[i]));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (fill)
        {
            juce::Path f = p; f.lineTo (plot.getRight(), plot.getBottom());
            f.lineTo (plot.getRight() - plot.getWidth() * (float) (n - 1) / (float) (LiveData::kHistory - 1), plot.getBottom()); f.closeSubPath();
            g.setColour (c.withAlpha (0.12f)); g.fillPath (f);
        }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    drawHist (histIn, col::lilacDim, 1.3f, false);
    drawHist (histOut, col::mint, 2.0f, true);
    if (target > lo && target < hi)
    {
        const float y = toY (target);
        const float dashes[] = { 5, 4 };
        g.setColour (col::amber); g.drawDashedLine ({ plot.getX(), y, plot.getRight(), y }, dashes, 2, 1.5f);
        g.setFont (semi (10.5f)); g.drawText (ru ("цель"), juce::Rectangle<float> (plot.getX() + 4, y - 15, 60, 13), juce::Justification::centredLeft);
    }
    g.setColour (col::textFaint); g.setFont (semi (10.5f));
    g.drawText (ru ("60 с назад"), plot.withHeight (12).translated (0, plot.getHeight() - 12), juce::Justification::bottomLeft);
    g.drawText (ru ("сейчас"), plot.withHeight (12).translated (0, plot.getHeight() - 12), juce::Justification::bottomRight);
}

// ===========================================================================
void SpeechBubble::say (const juce::String& t, float seconds)
{
    text = t; remaining = seconds; typed = 0;
    relayout();
}

void SpeechBubble::setAnchor (Point<float> top, juce::Rectangle<float> lim, bool preferLeft)
{
    if (top.getDistanceFrom (anchor) > 0.5f || lim != limits || preferLeft != left) { anchor = top; limits = lim; left = preferLeft; relayout(); }
}

void SpeechBubble::relayout()
{
    juce::AttributedString as; as.append (text, semi (14.0f), col::night0);
    as.setWordWrap (juce::AttributedString::byWord); as.setJustification (juce::Justification::centredLeft);
    layout.createLayout (as, 300.0f);
    const float w = std::max (60.0f, layout.getWidth()) + 28.0f, h = layout.getHeight() + 22.0f;
    bubble = juce::Rectangle<float> (w, h).withCentre ({ anchor.x + (left ? -w * 0.25f : w * 0.25f), anchor.y - h * 0.5f - 22.0f });
    if (! limits.isEmpty()) bubble = bubble.constrainedWithin (limits);
    repaint();
}

void SpeechBubble::tick (float dt)
{
    const float prevA = alpha; const int prevTyped = (int) typed;
    if (remaining > 0) { remaining -= dt; alpha = std::min (1.0f, alpha + dt * 6.0f); typed = std::min ((float) text.length(), typed + dt * 55.0f); }
    else alpha = std::max (0.0f, alpha - dt * 3.0f);
    if (std::abs (prevA - alpha) > 0.001f || prevTyped != (int) typed) repaint (bubble.expanded (12, 30).getSmallestIntegerContainer());
}

void SpeechBubble::paint (juce::Graphics& g)
{
    if (alpha <= 0.01f || text.isEmpty()) return;
    g.setOpacity (alpha);
    const float lift = (1.0f - alpha) * 8.0f;
    auto b = bubble.translated (0, lift);
    g.setColour (juce::Colours::black.withAlpha (0.25f * alpha)); g.fillRoundedRectangle (b.translated (0, 3), 16);
    juce::Path p; p.addRoundedRectangle (b, 16);
    const float tx = juce::jlimit (b.getX() + 20, b.getRight() - 20, anchor.x);
    p.startNewSubPath (tx - 10, b.getBottom() - 1); p.lineTo (anchor.x + (anchor.x < tx ? 4.0f : -4.0f), std::min (anchor.y - 6, b.getBottom() + 16)); p.lineTo (tx + 10, b.getBottom() - 1); p.closeSubPath();
    g.setColour (col::cream.withAlpha (alpha)); g.fillPath (p);
    g.setColour (col::lilac.withAlpha (alpha)); g.strokePath (p, juce::PathStrokeType (1.2f));
    // typewriter: draw the full layout clipped by the typed fraction
    juce::AttributedString as; as.append (text.substring (0, (int) typed), semi (14.0f), col::night0.withAlpha (alpha));
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (as, 300.0f);
    tl.draw (g, b.reduced (14, 11));
}

// ===========================================================================
void BipToast::push (const juce::String& t, juce::Colour accent)
{
    if (! items.empty() && items.back().text == t) return;
    items.push_back ({ t, accent, 0.0f, juce::jlimit (3.0f, 8.0f, 2.0f + (float) t.length() * 0.05f) });
    while (items.size() > 3) items.pop_front();
}

void BipToast::tick (float dt)
{
    if (items.empty()) return;
    items.front().t += dt;
    if (items.front().t >= items.front().dur) items.pop_front();
    repaint();
}

void BipToast::paint (juce::Graphics& g)
{
    if (items.empty()) return;
    const auto& it = items.front();
    const float in = std::min (1.0f, it.t / 0.35f), out = std::min (1.0f, (it.dur - it.t) / 0.35f);
    const float k = std::min (in, out);
    const float e = 1 - std::pow (1 - k, 3.0f);
    auto r = getLocalBounds().toFloat().reduced (4);
    r = r.translated ((1 - e) * (r.getWidth() + 20), 0);
    g.setOpacity (e);
    drawGlassCard (g, r, 14, 0.97f);
    g.setColour (it.accent); g.fillRoundedRectangle (r.withWidth (4).reduced (0, 10), 2);
    // tiny sputnik
    const Point<float> s (r.getX() + 26, r.getCentreY());
    g.setColour (col::creamDim);
    for (int a = 0; a < 4; ++a) { const float ang = (float) a * PI * 0.5f + 0.6f; g.drawLine (s.x, s.y, s.x + std::cos (ang) * 12, s.y + std::sin (ang) * 12 + 3, 1); }
    g.setColour (juce::Colour (0xffd8d2e8)); g.fillEllipse (juce::Rectangle<float> (11, 11).withCentre (s));
    g.setColour (it.accent); g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre (s + Point<float> (2, -2)));
    juce::AttributedString as;
    as.append (ru ("Бип: "), heavy (13.0f), it.accent);
    as.append (it.text, semi (13.0f), col::text);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl; tl.createLayout (as, r.getWidth() - 62);
    tl.draw (g, r.withTrimmedLeft (48).reduced (6, 8));
}
} // namespace mooni::ui
