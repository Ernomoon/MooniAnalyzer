#include "EqGraph.h"
#include "../analysis/Diagnostics.h"

namespace mooni::ui
{
using juce::Point;
using namespace params;

static bool hasGain (int type) { return type == (int) dsp::FilterType::Bell || type == (int) dsp::FilterType::LowShelf || type == (int) dsp::FilterType::HighShelf; }
static juce::Colour channelColour (int ch) { return ch == 1 ? col::lilac : ch == 2 ? col::mint : col::cream; }

EqGraph::EqGraph (UiContext& c) : ctx (c)
{
    setTooltip (ru ("Перетаскивай кратерят: влево-вправо — частота, вверх-вниз — усиление. Колесо — добротность (Q). "
                    "Двойной клик по пустому месту — новая полоса. Правый клик — тип, канал, прослушка. Alt+клик — подслушать полосу."));
}

juce::Rectangle<float> EqGraph::plot() const { return getLocalBounds().toFloat().reduced (44, 18).withTrimmedBottom (14); }
float EqGraph::xForFreq (float f) const { auto p = plot(); return p.getX() + p.getWidth() * std::log (f / kMinF) / std::log (kMaxF / kMinF); }
float EqGraph::freqForX (float x) const { auto p = plot(); return kMinF * std::pow (kMaxF / kMinF, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
float EqGraph::yForDb (float db) const { auto p = plot(); return p.getCentreY() - db / kRange * p.getHeight() * 0.5f; }
float EqGraph::dbForY (float y) const { auto p = plot(); return juce::jlimit (-kRange, kRange, (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kRange); }

Point<float> EqGraph::nodePos (int band) const
{
    const auto b = ctx.proc.currentEqBands()[(size_t) band];
    return { xForFreq (b.freq), yForDb (hasGain (b.type) ? b.gainDb : 0.0f) };
}

int EqGraph::bandAt (Point<float> p) const
{
    const auto bands = ctx.proc.currentEqBands();
    int best = -1; float bestD = 20.0f;
    for (int i = 0; i < kEqBands; ++i)
    {
        if (! bands[(size_t) i].enabled) continue;
        const float d = p.getDistanceFrom (nodePos (i));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void EqGraph::setParam (const juce::String& id, float v)
{
    if (auto* prm = ctx.proc.param (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void EqGraph::refresh (float dt)
{
    time += dt;
    squeak = std::max (0.0f, squeak - dt * 2.0f);
    specOut = ctx.live.specOut;
    specIn = ctx.live.specIn;
    repaint();
}

void EqGraph::paint (juce::Graphics& g)
{
    auto p = plot();
    const double sr = ctx.proc.getCurrentSampleRate() > 0 ? ctx.proc.getCurrentSampleRate() : 48000.0;

    // lunar surface background & grid
    g.setColour (col::night0.withAlpha (0.55f)); g.fillRoundedRectangle (p.expanded (6), 12);
    g.setFont (semi (10.5f));
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        const float x = xForFreq (f);
        g.setColour (col::line.withAlpha (0.55f)); g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        g.setColour (col::textFaint);
        g.drawText (f >= 1000 ? juce::String (juce::roundToInt (f / 1000.0f)) + ru ("к") : juce::String ((int) f), juce::Rectangle<float> (40, 12).withCentre ({ x, p.getBottom() + 12 }), juce::Justification::centred);
    }
    for (float db = -18; db <= 18; db += 6)
    {
        const float y = yForDb (db);
        g.setColour ((db == 0 ? col::lilacDim : col::line).withAlpha (db == 0 ? 0.8f : 0.45f)); g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
        g.setColour (col::textFaint);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (p.getX() - 40, y - 7, 34, 14), juce::Justification::centredRight);
    }
    // zone names along the top (the crater's regions)
    {
        const std::pair<float, const char*> zones[] = { { 40, "саб" }, { 120, "бас" }, { 330, "низ. середина" }, { 1000, "середина" }, { 3500, "верх. середина" }, { 9000, "верх" }, { 15000, "воздух" } };
        g.setFont (semi (10.0f)); g.setColour (col::textFaint.withAlpha (0.8f));
        for (auto& z : zones) g.drawText (juce::String::fromUTF8 (z.second), juce::Rectangle<float> (90, 12).withCentre ({ xForFreq (z.first), p.getY() + 9 }), juce::Justification::centred);
    }

    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (p.expanded (4).toNearestInt());

    // live spectrum, normalised to its own 500 Hz–2 kHz level (same scale as the band analysis)
    auto norm = [] (const std::array<float, LiveData::kSpectrumPoints>& s)
    {
        double sum = 0; int n = 0;
        for (int i = 0; i < LiveData::kSpectrumPoints; ++i) { const float f = LiveData::spectrumFreq (i); if (f >= 500 && f <= 2000 && s[(size_t) i] > -110) { sum += s[(size_t) i]; ++n; } }
        return n > 0 ? (float) (sum / n) : 0.0f;
    };
    auto spectrumPath = [&] (const std::array<float, LiveData::kSpectrumPoints>& s, float offset, bool closed)
    {
        juce::Path path; bool started = false;
        for (int i = 0; i < LiveData::kSpectrumPoints; ++i)
        {
            const float f = LiveData::spectrumFreq (i);
            if (s[(size_t) i] < -110) continue;
            const float x = xForFreq (f), y = yForDb (juce::jlimit (-kRange - 6, kRange + 6, (s[(size_t) i] - offset) * 0.6f));
            if (! started) { path.startNewSubPath (x, closed ? p.getBottom() + 5 : y); if (closed) path.lineTo (x, y); started = true; } else path.lineTo (x, y);
        }
        if (closed && started) { path.lineTo (path.getCurrentPosition().x, p.getBottom() + 5); path.closeSubPath(); }
        return path;
    };
    if (ctx.live.signalPresent)
    {
        const float off = norm (specOut);
        auto fill = spectrumPath (specOut, off, true);
        juce::ColourGradient sg (col::mint.withAlpha (0.22f), 0, p.getY(), col::mint.withAlpha (0.03f), 0, p.getBottom(), false);
        g.setGradientFill (sg); g.fillPath (fill);
        g.setColour (col::mint.withAlpha (0.45f)); g.strokePath (spectrumPath (specOut, off, false), juce::PathStrokeType (1.2f));
        if (showInput) { g.setColour (col::lilac.withAlpha (0.5f)); g.strokePath (spectrumPath (specIn, norm (specIn), false), juce::PathStrokeType (1.0f)); }
    }

    // long-term measured mix vs target (relative 1/6-octave levels, same 0.6 display scale)
    if (ctx.report && ctx.report->valid)
    {
        const auto rel = diagnostics::relativeBands (ctx.report->stats.sixth);
        juce::Path meas, targ; bool sm = false, st = false;
        for (int i = 0; i < BandGrid::kSixthBands; ++i)
        {
            const float f = BandGrid::sixthCentre (i);
            if (f > sr * 0.45) break;
            const float x = xForFreq (f);
            const float ym = yForDb (juce::jlimit (-kRange, kRange, rel[(size_t) i] * 0.6f)), yt = yForDb (juce::jlimit (-kRange, kRange, ctx.report->targetSixth[(size_t) i] * 0.6f));
            if (! sm) { meas.startNewSubPath (x, ym); sm = true; } else meas.lineTo (x, ym);
            if (! st) { targ.startNewSubPath (x, yt); st = true; } else targ.lineTo (x, yt);
        }
        const float dashes[] = { 4.0f, 4.0f };
        juce::Path dashed; juce::PathStrokeType (1.6f).createDashedStroke (dashed, targ, dashes, 2);
        g.setColour (col::lilac.withAlpha (0.75f)); g.fillPath (dashed);
        g.setColour (col::cream.withAlpha (0.55f)); g.strokePath (meas, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }

    // EQ curves: current (mid & side) and recommended
    const auto bands = ctx.proc.currentEqBands();
    const bool eqOnNow = ctx.proc.value (eqOn) > 0.5f;
    auto curve = [&] (const std::array<dsp::EqBandParams, kEqBands>& bs, bool side, const std::vector<EqSuggestion>* extra)
    {
        juce::Path path;
        for (int i = 0; i <= 300; ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / 300.0f;
            const float f = freqForX (x);
            double db = dsp::ParametricEQ::pathResponseDb (bs, f, sr, side);
            if (extra)
                for (const auto& e : *extra)
                {
                    if (side ? e.channel == 1 : e.channel == 2) continue;
                    dsp::EqBandParams b; b.enabled = true; b.type = e.type; b.freq = e.freq; b.gainDb = e.gainDb; b.q = e.q;
                    db += dsp::ParametricEQ::bandResponseDb (b, f, sr);
                }
            const float y = yForDb ((float) juce::jlimit (-kRange - 3.0, kRange + 3.0, db));
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        return path;
    };

    const bool recAvailable = showRec && ctx.report && ctx.report->valid && ! ctx.report->eq.empty() && ! ctx.isStale();
    if (recAvailable)
    {
        auto rec = curve (bands, false, &ctx.report->eq);
        juce::Path dashed; const float d[] = { 7.0f, 5.0f };
        juce::PathStrokeType (2.4f).createDashedStroke (dashed, rec, d, 2);
        drawGlow (g, { xForFreq (ctx.report->eq.front().freq), yForDb (ctx.report->eq.front().gainDb) }, 40, col::amber, 0.12f);
        g.setColour (col::amber.withAlpha (0.9f)); g.fillPath (dashed);
        for (const auto& e : ctx.report->eq)
        {
            const Point<float> c (xForFreq (e.freq), yForDb (hasGain (e.type) ? e.gainDb : 0.0f));
            g.setColour (col::amber.withAlpha (0.25f)); g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (c));
            g.setColour (col::amber); g.drawEllipse (juce::Rectangle<float> (16, 16).withCentre (c), 1.5f);
        }
    }
    auto side = curve (bands, true, nullptr);
    auto mid = curve (bands, false, nullptr);
    const bool hasSide = std::any_of (bands.begin(), bands.end(), [] (auto& b) { return b.enabled && b.channel != 0; });
    if (hasSide) { g.setColour (col::mint.withAlpha (eqOnNow ? 0.8f : 0.3f)); const float d[] = { 3, 3 }; juce::Path ds; juce::PathStrokeType (1.6f).createDashedStroke (ds, side, d, 2); g.fillPath (ds); }
    // fill under the current curve like moon regolith
    {
        juce::Path fill = mid; fill.lineTo (p.getRight(), yForDb (0)); fill.lineTo (p.getX(), yForDb (0)); fill.closeSubPath();
        g.setColour (col::cream.withAlpha (eqOnNow ? 0.10f : 0.04f)); g.fillPath (fill);
    }
    g.setColour (col::cream.withAlpha (eqOnNow ? 1.0f : 0.35f));
    g.strokePath (mid, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Kratermites
    for (int i = 0; i < kEqBands; ++i)
        if (bands[(size_t) i].enabled) drawMite (g, nodePos (i), i, bands[(size_t) i], i == dragBand || i == hoverBand);

    // readout
    if (hoverBand >= 0 || dragBand >= 0)
    {
        const int b = dragBand >= 0 ? dragBand : hoverBand;
        const auto& bp = bands[(size_t) b];
        auto np = nodePos (b);
        juce::String txt = hz (bp.freq);
        if (hasGain (bp.type)) txt << "  " << signedNum (bp.gainDb, 1) << ru (" дБ");
        txt << "  Q " << juce::String (bp.q, 2);
        auto box = juce::Rectangle<float> (150, 22).withCentre (np + Point<float> (0, np.y < p.getCentreY() ? 34.0f : -34.0f)).constrainedWithin (p);
        g.setColour (col::night0.withAlpha (0.92f)); g.fillRoundedRectangle (box, 8);
        g.setColour (channelColour (bp.channel)); g.setFont (heavy (12.0f)); g.drawText (txt, box, juce::Justification::centred);
    }
    else if (mousePos.x > 0 && p.contains (mousePos))
    {
        g.setColour (col::textDim); g.setFont (semi (11.0f));
        g.drawText (hz (freqForX (mousePos.x)) + "  " + signedNum (dbForY (mousePos.y), 1) + ru (" дБ"),
                    juce::Rectangle<float> (130, 16).withPosition (mousePos + Point<float> (10, -20)), juce::Justification::centredLeft);
    }

    // legend
    g.setFont (semi (11.0f));
    auto leg = juce::Rectangle<float> (p.getX() + 10, p.getBottom() - 22, 470, 16);
    auto item = [&] (juce::Colour c, const juce::String& t, bool dashed)
    {
        auto r = leg.removeFromLeft (dashed ? 118.0f : 108.0f);
        g.setColour (c);
        if (dashed) { const float d[] = { 4, 3 }; g.drawDashedLine ({ r.getX(), r.getCentreY(), r.getX() + 16, r.getCentreY() }, d, 2, 2.0f); }
        else g.fillRect (r.getX(), r.getCentreY() - 1, 16.0f, 2.0f);
        g.setColour (col::textDim); g.drawText (t, r.withTrimmedLeft (21), juce::Justification::centredLeft);
    };
    item (col::cream, ru ("твой EQ"), false);
    item (col::amber, ru ("рекомендация"), true);
    item (col::cream.withAlpha (0.55f), ru ("микс (среднее)"), false);
    item (col::lilac, ctx.report && ctx.report->usedReference ? ru ("эталон") : ru ("ориентир стиля"), true);
}

void EqGraph::drawMite (juce::Graphics& g, Point<float> c, int band, const dsp::EqBandParams& b, bool activeMite)
{
    const float r = activeMite ? 13.0f : 11.0f;
    const auto colour = channelColour (b.channel);
    // fuzzy body
    juce::Path fur;
    for (int i = 0; i <= 24; ++i)
    {
        const float a = (float) i / 24.0f * juce::MathConstants<float>::twoPi;
        const float rr = r * (1.0f + 0.12f * std::sin (a * 7 + time * (activeMite ? 9.0f : 2.0f) + (float) band));
        const auto q = c + Point<float> (std::cos (a) * rr, std::sin (a) * rr);
        if (i == 0) fur.startNewSubPath (q); else fur.lineTo (q);
    }
    fur.closeSubPath();
    if (activeMite) drawGlow (g, c, r * 2.6f, colour, 0.35f);
    g.setColour (colour.darker (0.1f)); g.fillPath (fur);
    g.setColour (col::night1); g.strokePath (fur, juce::PathStrokeType (1.4f));
    // eyes follow the mouse
    Point<float> look { 0, 0 };
    if (mousePos.x > 0) { auto d = mousePos - c; const float len = std::max (1.0f, d.getDistanceFromOrigin()); look = d / len * 1.6f; }
    for (int s : { -1, 1 })
    {
        const auto e = c + Point<float> ((float) s * r * 0.36f, -r * 0.18f);
        g.setColour (juce::Colours::white); g.fillEllipse (juce::Rectangle<float> (r * 0.5f, r * 0.55f).withCentre (e));
        g.setColour (col::night1); g.fillEllipse (juce::Rectangle<float> (r * 0.24f, r * 0.26f).withCentre (e + look));
    }
    // mouth: squeaks while dragged
    g.setColour (col::night1);
    if (activeMite && squeak > 0) g.fillEllipse (juce::Rectangle<float> (r * 0.35f, r * 0.3f * (0.6f + squeak)).withCentre (c + Point<float> (0, r * 0.38f)));
    else g.drawLine (c.x - r * 0.18f, c.y + r * 0.38f, c.x + r * 0.18f, c.y + r * 0.38f, 1.4f);
    // band number badge
    g.setColour (col::night0); g.fillEllipse (juce::Rectangle<float> (13, 13).withCentre (c + Point<float> (r * 0.85f, -r * 0.85f)));
    g.setColour (colour); g.setFont (heavy (9.5f));
    g.drawText (juce::String (band + 1), juce::Rectangle<float> (13, 13).withCentre (c + Point<float> (r * 0.85f, -r * 0.85f)), juce::Justification::centred);
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const int b = bandAt (e.position);
    if (b != hoverBand) { hoverBand = b; setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor); }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b < 0) return;
    if (e.mods.isPopupMenu()) { showBandMenu (b); return; }
    if (e.mods.isAltDown())
    {
        const auto bp = ctx.proc.currentEqBands()[(size_t) b];
        const float w = hasGain (bp.type) || bp.type == (int) dsp::FilterType::Notch ? std::pow (2.0f, 0.8f / std::max (0.3f, bp.q)) : 2.0f;
        if (ctx.listenBand) ctx.listenBand (bp.freq / w, bp.freq * w);
        return;
    }
    dragBand = b;
    dragStart = nodePos (b);
    for (const auto& id : { eqBandFreq (b), eqBandGain (b) }) if (auto* p = ctx.proc.param (id)) p->beginChangeGesture();
    squeak = 1.0f;
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragBand < 0) return;
    const auto bp = ctx.proc.currentEqBands()[(size_t) dragBand];
    // Shift = fine: the node moves at 1/5 of the mouse speed
    const auto target = e.mods.isShiftDown() ? dragStart + (e.position - e.mouseDownPosition) * 0.2f : e.position;
    setParam (eqBandFreq (dragBand), juce::jlimit (kMinF, kMaxF, freqForX (target.x)));
    if (hasGain (bp.type)) setParam (eqBandGain (dragBand), dbForY (target.y));
    squeak = std::min (1.0f, squeak + 0.15f);
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (const auto& id : { eqBandFreq (dragBand), eqBandGain (dragBand) }) if (auto* p = ctx.proc.param (id)) p->endChangeGesture();
    dragBand = -1;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b >= 0)
    {
        ctx.proc.setValue (eqBandGain (b), 0.0f);
        return;
    }
    if (! plot().contains (e.position)) return;
    const auto bands = ctx.proc.currentEqBands();
    for (int i = 0; i < kEqBands; ++i)
        if (! bands[(size_t) i].enabled)
        {
            const float f = freqForX (e.position.x);
            const int type = f < 40 ? (int) dsp::FilterType::LowCut : (f > 15000 ? (int) dsp::FilterType::HighCut : (int) dsp::FilterType::Bell);
            ctx.proc.suppressSnapshots = true;
            ctx.proc.setValue (eqBandType (i), (float) type);
            ctx.proc.setValue (eqBandFreq (i), f);
            ctx.proc.setValue (eqBandGain (i), type == (int) dsp::FilterType::Bell ? dbForY (e.position.y) : 0.0f);
            ctx.proc.setValue (eqBandQ (i), type == (int) dsp::FilterType::Bell ? 1.0f : 0.71f);
            ctx.proc.setValue (eqBandCh (i), 0.0f);
            ctx.proc.setValue (eqBandOn (i), 1.0f);
            ctx.proc.setValue (eqOn, 1.0f);
            ctx.proc.suppressSnapshots = false;
            ctx.proc.pushUndoSnapshot();
            return;
        }
    if (ctx.toast) ctx.toast (ru ("Все 8 полос заняты. Выключи ненужную (правый клик по кратерёнку)."), col::amber);
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = bandAt (e.position);
    if (b < 0) return;
    const auto bp = ctx.proc.currentEqBands()[(size_t) b];
    ctx.proc.setValue (eqBandQ (b), juce::jlimit (0.1f, 18.0f, bp.q * std::pow (1.15f, w.deltaY * 8.0f)));
}

void EqGraph::showBandMenu (int band)
{
    juce::PopupMenu m, types, chans;
    const auto bp = ctx.proc.currentEqBands()[(size_t) band];
    const auto tn = filterTypeNames();
    for (int i = 0; i < tn.size(); ++i) types.addItem (100 + i, tn[i], true, bp.type == i);
    const auto cn = channelNames();
    for (int i = 0; i < cn.size(); ++i) chans.addItem (200 + i, cn[i], true, bp.channel == i);
    m.addSectionHeader (ru ("Кратерёнок №") + juce::String (band + 1));
    m.addSubMenu (ru ("Тип фильтра"), types);
    m.addSubMenu (ru ("Канал"), chans);
    m.addItem (1, ru ("Подслушать эту полосу"));
    m.addItem (2, ru ("Обнулить усиление"));
    m.addSeparator();
    m.addItem (3, ru ("Выключить полосу"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, band, bp] (int r)
    {
        if (r >= 100 && r < 200) ctx.proc.setValue (eqBandType (band), (float) (r - 100));
        else if (r >= 200) ctx.proc.setValue (eqBandCh (band), (float) (r - 200));
        else if (r == 1 && ctx.listenBand) { const float w = std::pow (2.0f, 0.8f / std::max (0.3f, bp.q)); ctx.listenBand (bp.freq / w, bp.freq * w); }
        else if (r == 2) ctx.proc.setValue (eqBandGain (band), 0.0f);
        else if (r == 3) ctx.proc.setValue (eqBandOn (band), 0.0f);
    });
}
} // namespace mooni::ui
