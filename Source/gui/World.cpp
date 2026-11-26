#include "World.h"

namespace mooni::ui
{
using juce::Point;
static constexpr float PI = juce::MathConstants<float>::pi;

WorldView::WorldView()
{
    setInterceptsMouseClicks (true, false);
    juce::Random r (7);
    for (int i = 0; i < 46; ++i)
        twinkles.push_back ({ { r.nextFloat() * 1200.0f, r.nextFloat() * 560.0f }, 0.8f + r.nextFloat() * 1.6f, r.nextFloat() * 6.28f, 0.6f + r.nextFloat() * 2.0f });
}

void WorldView::resized() { buildSky(); }

void WorldView::buildSky()
{
    const int w = std::max (1, getWidth()), h = std::max (1, getHeight());
    sky = juce::Image (juce::Image::RGB, w, h, true);
    juce::Graphics g (sky);
    juce::ColourGradient bg (col::night0, 0, 0, juce::Colour (0xff1a1240), 0, (float) h, false);
    bg.addColour (0.6, juce::Colour (0xff120e30));
    g.setGradientFill (bg);
    g.fillAll();
    // nebulae
    auto neb = [&] (float x, float y, float r, juce::Colour c, float a)
    {
        juce::ColourGradient gr (c.withAlpha (a), x, y, c.withAlpha (0.0f), x + r, y, true);
        g.setGradientFill (gr); g.fillEllipse (x - r, y - r, r * 2, r * 2);
    };
    neb (900, 160, 380, col::lilac, 0.10f);
    neb (1050, 520, 300, col::pink, 0.06f);
    neb (220, 620, 420, col::earth, 0.05f);
    neb (600, 300, 260, juce::Colour (0xff6a4cff), 0.05f);
    // milky band
    for (int i = 0; i < 600; ++i)
    {
        const float u = rng.nextFloat();
        const float x = u * (float) w, y = 80.0f + u * 260.0f + (rng.nextFloat() - 0.5f) * 110.0f;
        g.setColour (juce::Colours::white.withAlpha (0.04f + rng.nextFloat() * 0.08f));
        g.fillEllipse (x, y, 1.2f, 1.2f);
    }
    // stars
    for (int i = 0; i < 260; ++i)
    {
        const float x = rng.nextFloat() * (float) w, y = rng.nextFloat() * (float) h;
        const float s = rng.nextFloat() < 0.92f ? 0.8f + rng.nextFloat() * 1.0f : 1.8f + rng.nextFloat() * 1.4f;
        const auto c = rng.nextFloat() < 0.15f ? col::lilac : (rng.nextFloat() < 0.1f ? col::amber : juce::Colours::white);
        g.setColour (c.withAlpha (0.35f + rng.nextFloat() * 0.5f));
        g.fillEllipse (x, y, s, s);
    }
}

Point<float> WorldView::earthCentre() const
{
    switch (room)
    {
        case 0:  return { 300.0f, 150.0f };
        case 4:  return { 120.0f, 100.0f };
        default: return { 1150.0f, 92.0f };
    }
}
float WorldView::earthRadius() const { return room == 0 ? 44.0f : 18.0f; }

Point<float> WorldView::bipPos() const
{
    const auto e = earthCentre();
    const float r = earthRadius();
    return e + Point<float> (std::cos (t * 0.7f) * r * 2.3f, std::sin (t * 0.7f) * r * 0.9f);
}

void WorldView::tick (float dt, float lvl, float w, float g, bool sig, bool reducedMotion)
{
    t += dt; ++frame;
    level += (lvl - level) * std::min (1.0f, dt * 8.0f);
    width = w; gr = g; signal = sig; reduced = reducedMotion;

    // sound rings Earth -> horn (observatory)
    ringTimer -= dt;
    if (room == 0 && signal && ringTimer <= 0 && ! reduced) { ringTimer = 0.45f - 0.25f * level; rings.push_back ({ 0.0f, 0.6f + 0.4f * level }); }
    for (auto& r : rings) { r.r += dt * 0.55f; r.alpha -= dt * 0.35f; }
    rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.r > 1.0f || r.alpha <= 0; }), rings.end());

    shootTimer -= dt;
    if (shootTimer <= 0 && ! reduced) { shootTimer = 9.0f + rng.nextFloat() * 14.0f; shooting = { { 200.0f + rng.nextFloat() * 800.0f, 30.0f + rng.nextFloat() * 120.0f }, { 520.0f, 160.0f }, 0.0f }; }
    if (shooting.life >= 0) { shooting.life += dt; shooting.p += shooting.v * dt; if (shooting.life > 0.9f) shooting.life = -1; }

    // dirty regions only
    const auto e = earthCentre(); const float er = earthRadius();
    repaint (juce::Rectangle<float> (er * 6.5f, er * 4.0f).withCentre (e).getSmallestIntegerContainer());
    if (room == 0) repaint (0, 60, 420, 640);
    if (room == 2) repaint (0, getHeight() - 200, getWidth(), 140);
    if (room == 3) repaint (0, 60, getWidth(), getHeight() - 120);
    if (room == 4) repaint (juce::Rectangle<int> (900, 40, 300, 150));
    if (shooting.life >= 0) repaint (juce::Rectangle<float> (shooting.p.x - 160, shooting.p.y - 60, 200, 90).getSmallestIntegerContainer());
    if (frame % 3 == 0 && ! reduced)
        for (int i = 0; i < (int) twinkles.size(); i += 2) repaint (juce::Rectangle<float> (8, 8).withCentre (twinkles[(size_t) i].p).getSmallestIntegerContainer());
}

bool WorldView::hitTest (int x, int y)
{
    const Point<float> p ((float) x, (float) y);
    return p.getDistanceFrom (earthCentre()) < earthRadius() * 1.2f || p.getDistanceFrom (bipPos()) < 14.0f;
}

void WorldView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (p.getDistanceFrom (bipPos()) < 14.0f) { if (onBipClicked) onBipClicked(); }
    else if (onEarthClicked) onEarthClicked();
}

void WorldView::paint (juce::Graphics& g)
{
    if (sky.isValid()) g.drawImageAt (sky, 0, 0);

    for (auto& tw : twinkles)
    {
        const float a = 0.25f + 0.6f * (0.5f + 0.5f * std::sin (t * tw.speed + tw.phase));
        g.setColour (juce::Colours::white.withAlpha (reduced ? 0.5f : a));
        g.fillEllipse (juce::Rectangle<float> (tw.r * 2, tw.r * 2).withCentre (tw.p));
        if (tw.r > 2.0f) { g.setColour (juce::Colours::white.withAlpha (a * 0.4f)); g.drawLine (tw.p.x - 4, tw.p.y, tw.p.x + 4, tw.p.y, 0.6f); g.drawLine (tw.p.x, tw.p.y - 4, tw.p.x, tw.p.y + 4, 0.6f); }
    }
    if (shooting.life >= 0)
    {
        const float a = std::sin (shooting.life / 0.9f * PI);
        juce::ColourGradient gr (juce::Colours::white.withAlpha (0.9f * a), shooting.p.x, shooting.p.y, juce::Colours::white.withAlpha (0.0f), shooting.p.x - 140, shooting.p.y - 43, false);
        g.setGradientFill (gr);
        juce::Path tail; tail.startNewSubPath (shooting.p); tail.lineTo (shooting.p.x - 140, shooting.p.y - 43);
        g.strokePath (tail, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    switch (room)
    {
        case 0: drawObservatory (g); break;
        case 1: drawCraterField (g); break;
        case 2: drawTides (g); break;
        case 3: drawGravity (g); break;
        case 4: drawReferencePlanet (g); break;
        case 5: drawLogbook (g); break;
        default: break;
    }
    drawEarth (g);
    drawBip (g);
}

void WorldView::drawEarth (juce::Graphics& g)
{
    const auto c = earthCentre(); const float r = earthRadius();
    drawGlow (g, c, r * (1.9f + 0.5f * level), col::earth, 0.22f + 0.35f * level);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path disc; disc.addEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
    juce::ColourGradient sea (juce::Colour (0xff5fb8ff), c.x - r * 0.4f, c.y - r * 0.5f, juce::Colour (0xff1d4fb0), c.x + r * 0.6f, c.y + r * 0.8f, true);
    g.setGradientFill (sea); g.fillPath (disc);
    g.reduceClipRegion (disc);
    // drifting continents
    const float drift = std::fmod (t * 0.05f, 2.0f);
    g.setColour (juce::Colour (0xff6fdc9e).withAlpha (0.9f));
    for (int k = -1; k <= 1; ++k)
    {
        const float ox = (drift + (float) k * 2.0f - 1.0f) * r * 2.0f;
        juce::Path land;
        land.addEllipse (c.x + ox - r * 0.6f, c.y - r * 0.5f, r * 0.7f, r * 0.45f);
        land.addEllipse (c.x + ox - r * 0.2f, c.y - r * 0.15f, r * 0.5f, r * 0.7f);
        land.addEllipse (c.x + ox + r * 0.4f, c.y + r * 0.1f, r * 0.45f, r * 0.35f);
        g.fillPath (land);
    }
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    for (int k = 0; k < 3; ++k)
    {
        const float yy = c.y - r * 0.55f + (float) k * r * 0.5f, xx = c.x - r + std::fmod (t * (8.0f + (float) k * 3.0f) + (float) k * 30.0f, r * 3.0f) - r * 0.3f;
        g.fillRoundedRectangle (xx, yy, r * 0.8f, r * 0.1f, r * 0.05f);
    }
    // terminator shade
    juce::ColourGradient shade (juce::Colours::transparentBlack, c.x - r * 0.2f, c.y, col::night0.withAlpha (0.75f), c.x + r * 1.0f, c.y + r * 0.4f, false);
    g.setGradientFill (shade); g.fillPath (disc);
}

void WorldView::drawBip (juce::Graphics& g)
{
    const auto p = bipPos();
    const bool behind = std::sin (t * 0.7f) < 0;
    juce::ignoreUnused (behind);
    g.setColour (col::creamDim);
    for (int k = 0; k < 4; ++k)
    {
        const float a = (float) k * PI * 0.5f + 0.6f + std::sin (t * 2) * 0.1f;
        g.drawLine (p.x, p.y, p.x + std::cos (a) * 13, p.y + std::sin (a) * 13 + 4, 1.0f);
    }
    g.setColour (juce::Colour (0xffd8d2e8)); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (p));
    g.setColour (col::coral.withAlpha (0.5f + 0.5f * std::sin (t * 6))); g.fillEllipse (juce::Rectangle<float> (3, 3).withCentre (p + Point<float> (2, -2)));
}

void WorldView::drawObservatory (juce::Graphics& g)
{
    // sound rings travelling from Earth to the horn
    const auto e = earthCentre(); const Point<float> horn (150, 380);
    for (auto& r : rings)
    {
        const auto c = e + (horn - e) * r.r;
        g.setColour (col::earth.withAlpha (r.alpha * (1.0f - r.r)));
        g.drawEllipse (juce::Rectangle<float> (16 + r.r * 26, 16 + r.r * 26).withCentre (c), 1.6f);
    }

    // lunar hill
    juce::Path hill;
    hill.startNewSubPath (0, 520);
    hill.cubicTo (90, 470, 260, 455, 420, 505);
    hill.lineTo (420, 720); hill.lineTo (0, 720); hill.closeSubPath();
    juce::ColourGradient gr (juce::Colour (0xff3d3478), 200, 460, juce::Colour (0xff1c1744), 200, 720, false);
    g.setGradientFill (gr); g.fillPath (hill);
    g.setColour (col::lilac.withAlpha (0.35f));
    juce::Path ridge; ridge.startNewSubPath (0, 520); ridge.cubicTo (90, 470, 260, 455, 420, 505);
    g.strokePath (ridge, juce::PathStrokeType (2.0f));
    drawCrater (g, { 70, 560 }, 26, juce::Colour (0xff3a3172), 0.8f);
    drawCrater (g, { 330, 545 }, 16, juce::Colour (0xff3a3172), 0.8f);
    drawCrater (g, { 250, 640 }, 34, juce::Colour (0xff2c255d), 0.8f);

    // the listening horn on a tripod
    g.setColour (juce::Colour (0xff6c5aa8));
    g.drawLine (120, 470, 100, 520, 4); g.drawLine (140, 470, 160, 522, 4); g.drawLine (130, 470, 130, 520, 4);
    juce::Path hornP;
    hornP.startNewSubPath (122, 470); hornP.lineTo (138, 470);
    hornP.quadraticTo (180, 420, 230, 340); hornP.lineTo (172, 312);
    hornP.quadraticTo (150, 400, 122, 470); hornP.closeSubPath();
    juce::ColourGradient brass (col::amber, 160, 330, juce::Colour (0xffb87a2e), 140, 470, false);
    g.setGradientFill (brass); g.fillPath (hornP);
    g.setColour (col::night1.withAlpha (0.6f)); g.strokePath (hornP, juce::PathStrokeType (1.5f));
    auto mouthE = juce::Rectangle<float> (70, 34).withCentre ({ 201, 326 });
    g.setColour (juce::Colour (0xff6b4a1f));
    g.addTransform (juce::AffineTransform());
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-0.45f, 201, 326));
        g.fillEllipse (mouthE);
        g.setColour (col::amber.brighter (0.3f)); g.drawEllipse (mouthE, 2.0f);
        if (signal) { g.setColour (col::earth.withAlpha (0.25f + 0.4f * level)); g.fillEllipse (mouthE.reduced (8, 5)); }
    }

    // signpost
    g.setColour (juce::Colour (0xff5b4b98)); g.fillRect (334.0f, 455.0f, 3.0f, 40.0f);
    g.setColour (col::night2); g.fillRoundedRectangle (282, 438, 104, 22, 5);
    g.setColour (col::lilac); g.drawRoundedRectangle (282, 438, 104, 22, 5, 1);
    g.setFont (heavy (10.5f)); g.setColour (col::cream);
    g.drawText (juce::String::fromUTF8 ("СТАНЦИЯ «УХО-1»"), 282, 438, 104, 22, juce::Justification::centred);

    // blinking antenna dish
    g.setColour (col::lilacDim); g.drawLine (370, 500, 370, 470, 2);
    juce::Path dish; dish.addCentredArc (370, 466, 14, 8, -0.4f, -PI * 0.5f, PI * 0.5f, true);
    g.strokePath (dish, juce::PathStrokeType (2.5f));
    g.setColour ((std::fmod (t, 1.6f) < 0.2f ? col::coral : col::coral.withAlpha (0.25f)));
    g.fillEllipse (367, 452, 6, 6);
}

void WorldView::drawCraterField (juce::Graphics& g)
{
    juce::ignoreUnused (g);
}

void WorldView::drawTides (juce::Graphics& g)
{
    const float h = (float) getHeight(), w = (float) getWidth();
    for (int layer = 0; layer < 3; ++layer)
    {
        juce::Path sea;
        const float base = h - 74.0f - (float) layer * 16.0f;   // just above the bottom bar
        sea.startNewSubPath (0, h);
        for (int x = 0; x <= (int) w; x += 12)
        {
            const float u = (float) x / w;
            const int band = juce::jlimit (0, 3, (int) (u * 4));
            const float amp = (2.0f + bandWidths[(size_t) band] * 0.06f) * (1.0f + (float) layer * 0.4f);
            const float y = base - amp * std::sin ((float) x * 0.018f + t * (1.2f + (float) layer * 0.5f) + (float) layer);
            sea.lineTo ((float) x, y);
        }
        sea.lineTo (w, h); sea.closeSubPath();
        g.setColour (col::earth.withAlpha (0.10f + 0.07f * (float) (2 - layer)));
        g.fillPath (sea);
    }
}

void WorldView::drawGravity (juce::Graphics& g)
{
    // a gravity well grid that sags with gain reduction
    const float sag = juce::jlimit (0.0f, 1.0f, gr / 10.0f);
    const Point<float> well (1010, 560);
    g.setColour (col::lilac.withAlpha (0.08f));
    for (int i = 0; i <= 24; ++i)
    {
        juce::Path line;
        for (int k = 0; k <= 40; ++k)
        {
            const float x = (float) k * 30.0f, y = 80.0f + (float) i * 26.0f;
            const float d = Point<float> (x, y).getDistanceFrom (well);
            const float dip = sag * 60.0f * std::exp (-d * d / 60000.0f);
            if (k == 0) line.startNewSubPath (x, y + dip); else line.lineTo (x, y + dip);
        }
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }
}

void WorldView::drawReferencePlanet (juce::Graphics& g)
{
    const Point<float> c (1060, 110);
    drawGlow (g, c, 70, col::pink, 0.15f);
    juce::ColourGradient pl (juce::Colour (0xffffb38a), c.x - 20, c.y - 20, juce::Colour (0xff9c4f8a), c.x + 30, c.y + 30, true);
    g.setGradientFill (pl); g.fillEllipse (juce::Rectangle<float> (64, 64).withCentre (c));
    juce::Path ring; ring.addCentredArc (c.x, c.y, 58, 14, -0.25f, -PI * 0.5f, PI * 0.5f, true);
    g.setColour (col::cream.withAlpha (0.7f)); g.strokePath (ring, juce::PathStrokeType (3.0f));
    g.setColour (col::cream.withAlpha (0.5f + 0.3f * std::sin (t * 2)));
    g.setFont (semi (11.0f));
    g.drawText (juce::String::fromUTF8 ("планета «Эталон»"), juce::Rectangle<float> (140, 16).withCentre (c + Point<float> (0, 48)), juce::Justification::centred);
}

void WorldView::drawLogbook (juce::Graphics& g) { juce::ignoreUnused (g); }
} // namespace mooni::ui
