#include "MooniCharacter.h"

namespace mooni::ui
{
using juce::Point;
static constexpr float PI = juce::MathConstants<float>::pi;

static float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
static float smooth (float x) { x = clamp01 (x); return x * x * (3 - 2 * x); }
static float elastic (float x) { x = clamp01 (x); if (x <= 0 || x >= 1) return x; return std::pow (2.0f, -10 * x) * std::sin ((x * 10 - 0.75f) * (2 * PI / 3)) + 1; }
static float bump (float t, float a, float b) { return smooth ((t - a) / 0.12f) * (1.0f - smooth ((t - b) / 0.2f)); }

Pose Pose::lerp (const Pose& a, const Pose& b, float t)
{
    Pose r = t < 0.5f ? a : b;
    auto L = [t] (float x, float y) { return x + (y - x) * t; };
    r.x = L (a.x, b.x); r.y = L (a.y, b.y); r.scale = L (a.scale, b.scale); r.sx = L (a.sx, b.sx); r.sy = L (a.sy, b.sy); r.rot = L (a.rot, b.rot);
    r.eyeOpen = L (a.eyeOpen, b.eyeOpen); r.squint = L (a.squint, b.squint); r.pupilX = L (a.pupilX, b.pupilX); r.pupilY = L (a.pupilY, b.pupilY);
    r.pupilSize = L (a.pupilSize, b.pupilSize); r.brow = L (a.brow, b.brow); r.browY = L (a.browY, b.browY);
    r.smile = L (a.smile, b.smile); r.mouthOpen = L (a.mouthOpen, b.mouthOpen); r.blush = L (a.blush, b.blush);
    r.tint = a.tint.interpolatedWith (b.tint, t); r.tintAmt = L (a.tintAmt, b.tintAmt);
    r.armPhase = L (a.armPhase, b.armPhase); r.antennaBend = L (a.antennaBend, b.antennaBend); r.starSpin = L (a.starSpin, b.starSpin);
    r.starGlow = L (a.starGlow, b.starGlow); r.star = a.star.interpolatedWith (b.star, t);
    r.phonesGlow = L (a.phonesGlow, b.phonesGlow); r.phonesLift = L (a.phonesLift, b.phonesLift); r.phonesRot = L (a.phonesRot, b.phonesRot);
    r.eclipse = L (a.eclipse, b.eclipse); r.inflate = L (a.inflate, b.inflate); r.ghost = L (a.ghost, b.ghost);
    r.glasses = L (a.glasses, b.glasses); r.shake = L (a.shake, b.shake); r.hover = L (a.hover, b.hover);
    return r;
}

// ===========================================================================
// Reaction library — add new reactions here; nothing else needs to change.
// ===========================================================================
struct ReactionDef
{
    float duration;
    std::function<void (MooniView&)> start;
    std::function<void (Pose&, float t, float u, MooniView&)> apply;   // t seconds, u = t / duration
};

struct ReactionLibrary
{
    static const std::map<juce::String, ReactionDef>& get()
    {
        static const std::map<juce::String, ReactionDef> lib = build();
        return lib;
    }

    static std::map<juce::String, ReactionDef> build()
    {
        std::map<juce::String, ReactionDef> m;
        const auto cream = col::cream, coral = col::coral, mint = col::mint, amber = col::amber, lilac = col::lilac;

        m["poke"] = { 0.6f, [] (MooniView& v) { v.burst (Particle::Spark, 4, 1.2f, col::amber, 0.5f, 0.08f, 0.0f); },
            [] (Pose& p, float, float u, MooniView&) { const float k = std::sin (u * PI); p.sy -= 0.18f * k; p.sx += 0.14f * k; p.eyeOpen = 1 - 0.8f * k; p.smile = 0.6f; p.blush = 0.6f; } };

        m["giggle"] = { 1.2f, [] (MooniView& v) { v.spawn (Particle::Heart, { 0.6f, -0.9f }, { 0.2f, -0.6f }, 1.1f, 0.14f, col::pink); },
            [] (Pose& p, float t, float, MooniView&) { p.eyes = Eyes::Happy; p.mouth = Mouth::Grin; p.mouthOpen = 0.5f; p.blush = 0.85f;
                                                      p.rot = 0.08f * std::sin (t * 18); p.y -= 0.06f * std::abs (std::sin (t * 12)); } };

        m["annoyed"] = { 1.6f, [] (MooniView& v) { v.spawn (Particle::Steam, { 1.0f, -0.3f }, { 0.4f, -0.6f }, 0.9f, 0.14f, juce::Colours::white); },
            [] (Pose& p, float t, float, MooniView&) { p.brow = -0.8f; p.squint = 0.35f; p.mouth = Mouth::Flat; p.smile = -0.2f;
                                                      p.arms = Arms::Hips; p.pupilX = 0.6f * std::sin (t * 2); p.tint = col::coral; p.tintAmt = 0.12f; } };

        m["hide"] = { 4.0f, nullptr,
            [] (Pose& p, float, float u, MooniView&) { const float k = smooth (u / 0.15f) * (1 - smooth ((u - 0.85f) / 0.15f));
                                                      p.x += 2.6f * k; p.eyeOpen = 0.7f; p.pupilX = -1.0f; p.brow = -0.5f; p.mouth = Mouth::Flat; } };

        m["listen_start"] = { 1.4f, [] (MooniView& v) { v.spawn (Particle::Note, { -1.1f, -0.9f }, { -0.2f, -0.5f }, 1.4f, 0.2f, col::earth); },
            [] (Pose& p, float t, float, MooniView&) { p.phonesGlow = 1; p.arms = Arms::CoverEars; p.eyeOpen = std::max (0.0f, 1 - t * 2.5f);
                                                      p.smile = 0.5f; p.phonesLift = -0.05f * std::sin (t * 8); } };

        m["aha"] = { 1.4f, [] (MooniView& v) { v.spawn (Particle::Bulb, { 0.0f, -1.9f }, { 0, -0.15f }, 1.3f, 0.32f, col::amber); v.burst (Particle::Spark, 6, 1.0f, col::amber, 0.8f, 0.07f, 0.0f); },
            [] (Pose& p, float t, float, MooniView&) { p.eyeOpen = 1.15f; p.pupilSize = 0.8f; p.brow = 0.7f; p.browY = 0.06f; p.mouth = Mouth::Open; p.mouthOpen = 0.4f;
                                                      p.arms = Arms::Up; p.armPhase = 0.3f; p.y -= 0.15f * std::sin (std::min (1.0f, t * 3) * PI); p.starGlow = 1; } };

        m["think"] = { 1.6f, [] (MooniView& v) { v.spawn (Particle::Question, { 0.8f, -1.3f }, { 0.1f, -0.3f }, 1.5f, 0.3f, col::lilac); },
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Chin; p.pupilX = 0.5f; p.pupilY = -0.8f; p.brow = 0.3f; p.mouth = Mouth::Flat;
                                                      p.antennaBend = 0.5f * std::sin (t * 4); p.starSpin = t * 3; } };

        m["explode"] = { 3.6f, nullptr,
            [] (Pose& p, float t, float, MooniView& v)
            {
                const float boom = 1.0f, regrow = 2.4f;
                if (t < boom)
                {
                    const float k = t / boom;
                    p.inflate = 0.45f * k * k; p.shake = 0.1f * k; p.eyes = Eyes::Stars; p.eyeOpen = 1.2f; p.mouth = Mouth::Open; p.mouthOpen = 0.3f + 0.5f * k;
                    p.tint = col::pink; p.tintAmt = 0.3f * k; p.brow = 0.9f; p.starGlow = 1;
                }
                else if (t < regrow)
                {
                    if (! v.reactionFlag)
                    {
                        v.reactionFlag = true;
                        v.burst (Particle::Fragment, 14, 2.8f, col::cream, 1.8f, 0.2f, 1.2f);
                        v.burst (Particle::Star, 12, 2.2f, col::amber, 1.4f, 0.12f, 0.3f);
                        for (auto cc : { col::mint, col::pink, col::lilac, col::amber, col::earth })
                            v.burst (Particle::Confetti, 8, 2.6f, cc, 2.0f, 0.09f, 0.9f);
                        v.spawn (Particle::Text, { 0, -1.3f }, { 0, -0.35f }, 1.3f, 0.55f, col::amber, juce::String::fromUTF8 ("БУМ!"));
                        v.worldShake = 7.0f;
                    }
                    p.exploded = true; p.phonesLift = 0.6f + 0.25f * std::sin ((t - boom) * 3); p.phonesRot = (t - boom) * 1.8f;
                    v.steamTimer -= 0.016f;
                    if (v.steamTimer <= 0) { v.steamTimer = 0.12f; v.spawn (Particle::Smoke, { 0, -0.2f }, { (v.rng.nextFloat() - 0.5f) * 0.4f, -0.4f }, 1.4f, 0.35f, juce::Colour (0xff4a4370)); }
                }
                else
                {
                    const float k = (t - regrow) / (3.6f - regrow);
                    p.scale *= std::max (0.05f, elastic (k)); p.eyes = Eyes::Stars; p.mouth = Mouth::Grin; p.mouthOpen = 0.7f; p.arms = Arms::Up; p.blush = 0.8f; p.starGlow = 1;
                }
            } };

        m["excited"] = { 2.4f, [mint] (MooniView& v) { v.burst (Particle::Star, 8, 1.4f, mint, 1.2f, 0.12f, 0.2f); },
            [] (Pose& p, float t, float, MooniView&) { p.eyes = Eyes::Stars; p.mouth = Mouth::Grin; p.mouthOpen = 0.6f; p.arms = Arms::Up; p.blush = 0.7f;
                                                      p.y -= 0.25f * std::abs (std::sin (t * 6)); p.sy += 0.06f * std::sin (t * 12); p.starGlow = 1; p.starSpin = t * 4; } };

        m["happy"] = { 1.6f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.eyes = Eyes::Happy; p.smile = 0.9f; p.blush = 0.6f; p.y -= 0.1f * std::abs (std::sin (t * 5)); p.arms = Arms::Wave; p.armPhase = t * 10; } };

        m["meh"] = { 2.6f, [] (MooniView& v) { v.spawn (Particle::Dust, { 0.3f, 0.45f }, { 0.6f, 0.1f }, 1.0f, 0.12f, juce::Colours::white.withAlpha (0.7f)); },
            [] (Pose& p, float t, float, MooniView&) { p.eyeOpen = 0.45f; p.pupilX = 0.8f; p.pupilY = 0.1f; p.mouth = Mouth::Flat; p.smile = -0.05f; p.brow = 0.1f;
                                                      p.arms = Arms::Shrug; p.sy -= 0.05f * smooth (t / 0.6f); p.rot = -0.06f; } };

        m["annoyed_mix"] = { 2.4f, [] (MooniView& v) { v.spawn (Particle::Exclaim, { 0.9f, -1.3f }, { 0.1f, -0.3f }, 1.2f, 0.32f, col::orange); },
            [] (Pose& p, float t, float, MooniView&) { p.brow = -0.7f; p.squint = 0.3f; p.mouth = Mouth::Wavy; p.smile = -0.4f; p.arms = Arms::Hips;
                                                      p.tint = col::orange; p.tintAmt = 0.12f; p.rot = 0.05f * std::sin (t * 3); } };

        m["furious"] = { 3.0f, nullptr,
            [coral] (Pose& p, float t, float, MooniView& v)
            {
                p.brow = -1.0f; p.squint = 0.45f; p.mouth = Mouth::Zigzag; p.mouthOpen = 0.6f; p.tint = coral; p.tintAmt = 0.45f * smooth (t / 0.4f);
                p.shake = 0.06f; p.y -= 0.18f * std::abs (std::sin (t * 9)); p.arms = Arms::Up; p.armPhase = std::sin (t * 20) * 0.5f;
                p.phonesLift = t > 1.2f && t < 2.2f ? 0.9f * std::sin ((t - 1.2f) * PI) : 0.0f; p.star = coral; p.starGlow = 1;
                v.steamTimer -= 0.033f;
                if (v.steamTimer <= 0) { v.steamTimer = 0.09f; for (int s : { -1, 1 }) v.spawn (Particle::Steam, { 1.05f * (float) s, -0.35f }, { 0.5f * (float) s, -0.9f }, 0.8f, 0.16f, juce::Colours::white); }
                v.worldShake = 3.0f;
            } };

        m["disgust"] = { 2.4f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.tint = juce::Colour (0xff8ddf6e); p.tintAmt = 0.35f; p.mouth = Mouth::Tongue; p.squint = 0.5f;
                                                      p.brow = 0.4f; p.pupilY = 0.5f; p.rot = -0.1f + 0.04f * std::sin (t * 7); p.sx -= 0.05f; p.arms = Arms::Facepalm; } };

        m["bass_shake"] = { 2.2f, nullptr,
            [] (Pose& p, float t, float, MooniView& v) { p.shake = 0.12f; p.eyes = Eyes::Spiral; p.mouth = Mouth::Zigzag; p.mouthOpen = 0.3f;
                                                        p.y += 0.05f * std::sin (t * 60); p.sy += 0.06f * std::sin (t * 30); v.worldShake = 2.0f;
                                                        if (v.rng.nextFloat() < 0.15f) v.spawn (Particle::Dust, { (v.rng.nextFloat() - 0.5f) * 2.4f, 1.05f }, { 0, -0.4f }, 0.6f, 0.1f, col::creamDim); } };

        m["harsh_ears"] = { 2.2f, nullptr,
            [] (Pose& p, float t, float, MooniView& v) { p.arms = Arms::CoverEars; p.eyeOpen = 0.15f; p.squint = 0.6f; p.mouth = Mouth::Zigzag; p.brow = 0.6f;
                                                        p.shake = 0.02f; p.sy -= 0.06f; if (std::fmod (t, 0.4f) < 0.03f) v.spawn (Particle::Spark, { 1.2f, -0.7f }, { 0.8f, -0.4f }, 0.5f, 0.1f, col::amber); } };

        m["clip_zap"] = { 1.8f, [] (MooniView& v) { v.burst (Particle::Spark, 10, 2.2f, col::amber, 0.7f, 0.1f, 0.0f); v.spawn (Particle::Text, { 0.9f, -1.5f }, { 0.2f, -0.4f }, 1.1f, 0.3f, col::amber, juce::String::fromUTF8 ("ЩЁЛК!")); },
            [] (Pose& p, float t, float, MooniView&) { p.eyes = t < 0.9f ? Eyes::Cross : Eyes::Normal; p.mouth = Mouth::Open; p.mouthOpen = 0.5f; p.shake = t < 0.5f ? 0.08f : 0.0f;
                                                      p.antennaBend = std::sin (t * 40) * 0.6f * (1 - t / 1.8f); p.star = col::amber; p.starGlow = 1; p.tint = juce::Colour (0xff3a3150); p.tintAmt = 0.25f * (1 - t / 1.8f); } };

        m["phase_ghost"] = { 2.8f, [] (MooniView& v) { v.spawn (Particle::Question, { 0, -1.6f }, { 0, -0.2f }, 2.2f, 0.34f, col::lilac); },
            [] (Pose& p, float t, float, MooniView&) { p.ghost = 0.6f * std::sin (std::min (1.0f, t / 2.6f) * PI); p.pupilX = std::sin (t * 4); p.brow = 0.6f; p.mouth = Mouth::Wavy; p.smile = 0; } };

        m["squashed"] = { 2.2f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { const float k = smooth (t / 0.3f) * (1 - smooth ((t - 1.8f) / 0.4f));
                                                      p.sy -= 0.38f * k; p.sx += 0.3f * k; p.y += 0.35f * k; p.eyeOpen = 0.5f; p.mouth = Mouth::Open; p.mouthOpen = 0.2f; p.brow = 0.6f; p.arms = Arms::Up; } };

        m["quiet_lean"] = { 2.2f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.rot = -0.25f * smooth (t / 0.4f); p.arms = Arms::CoverEars; p.armPhase = 1; p.pupilX = -0.8f; p.brow = 0.5f; p.mouth = Mouth::Open; p.mouthOpen = 0.15f; } };

        m["muddy"] = { 2.2f, [] (MooniView& v) { for (int i = 0; i < 6; ++i) v.spawn (Particle::Dust, { (v.rng.nextFloat() - 0.5f) * 2.0f, (v.rng.nextFloat() - 0.5f) * 1.0f }, { (v.rng.nextFloat() - 0.5f) * 0.4f, -0.1f }, 1.8f, 0.25f, juce::Colour (0xff8a7d6a)); },
            [] (Pose& p, float t, float, MooniView&) { p.squint = 0.3f; p.pupilX = std::sin (t * 2.5f); p.arms = Arms::Wave; p.armPhase = t * 14; p.mouth = Mouth::Wavy; p.brow = 0.3f; } };

        m["too_wide"] = { 2.0f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { const float k = std::sin (std::min (1.0f, t / 1.8f) * PI); p.sx += 0.55f * k; p.sy -= 0.18f * k; p.eyes = Eyes::Spiral; p.arms = Arms::Shrug; p.mouth = Mouth::Open; p.mouthOpen = 0.3f; } };

        m["apply_fix"] = { 1.7f, [lilac] (MooniView& v) { v.burst (Particle::Spark, 8, 1.6f, lilac, 0.9f, 0.09f, 0.0f); },
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Hammer; p.armPhase = std::sin (t * 14); p.eyes = t > 1.0f ? Eyes::Happy : Eyes::Normal; p.smile = 0.7f;
                                                      p.y -= 0.08f * std::abs (std::sin (t * 7)); p.brow = 0.2f; } };

        m["stretch"] = { 1.2f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { const float k = std::sin (std::min (1.0f, t / 1.2f) * PI); p.sx += 0.25f * k; p.sy -= 0.08f * k; p.eyes = Eyes::Happy; p.arms = Arms::Shrug; } };

        m["squish"] = { 1.2f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { const float k = std::sin (std::min (1.0f, t / 1.2f) * PI); p.sy -= 0.2f * k; p.sx += 0.15f * k; p.eyeOpen = 0.6f; p.mouth = Mouth::Open; p.mouthOpen = 0.2f; } };

        m["telescope"] = { 2.4f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Telescope; p.glasses = 1; p.squint = 0.3f; p.pupilX = 0.7f; p.pupilY = -0.4f; p.smile = 0.5f; p.rot = -0.08f + 0.03f * std::sin (t * 2); } };

        m["reanalyze"] = { 1.6f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Hips; p.brow = -0.3f; p.smile = 0.4f; p.phonesGlow = 1; p.rot = 0.05f * std::sin (t * 6); p.y -= 0.12f * bump (t, 0.2f, 1.0f); } };

        m["yawn"] = { 2.0f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.mouth = Mouth::Open; p.mouthOpen = 0.9f * std::sin (std::min (1.0f, t / 1.6f) * PI); p.eyeOpen = 0.3f; p.arms = Arms::Up; p.armPhase = 0.8f; p.sy += 0.06f; } };

        m["wake"] = { 1.2f, [] (MooniView& v) { v.spawn (Particle::Exclaim, { 0.7f, -1.4f }, { 0.1f, -0.4f }, 0.9f, 0.3f, col::amber); },
            [] (Pose& p, float t, float, MooniView&) { p.eyeOpen = 1.25f; p.pupilSize = 0.6f; p.brow = 1; p.browY = 0.08f; p.mouth = Mouth::Open; p.mouthOpen = 0.5f;
                                                      p.y -= 0.4f * std::sin (std::min (1.0f, t / 0.5f) * PI); p.arms = Arms::Up; p.armPhase = 0.6f; } };

        m["dance"] = { 3.2f, nullptr,
            [] (Pose& p, float t, float, MooniView& v) { p.rot = 0.18f * std::sin (t * 7); p.x += 0.15f * std::sin (t * 3.5f); p.y -= 0.12f * std::abs (std::sin (t * 7));
                                                        p.arms = Arms::Wave; p.armPhase = t * 7; p.eyes = Eyes::Happy; p.mouth = Mouth::Grin; p.mouthOpen = 0.5f; p.blush = 0.7f;
                                                        v.noteTimer -= 0.033f; if (v.noteTimer <= 0) { v.noteTimer = 0.35f; v.spawn (Particle::Note, { (v.rng.nextFloat() - 0.5f) * 2.0f, -1.2f }, { 0, -0.6f }, 1.2f, 0.2f, col::mint); } } };

        m["facepalm"] = { 2.4f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Facepalm; p.eyeOpen = 0.3f; p.mouth = Mouth::Flat; p.smile = -0.3f; p.rot = 0.1f * smooth (t / 0.3f); p.sy -= 0.04f; } };

        m["tap_foot"] = { 2.6f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Hips; p.pupilX = -0.6f; p.pupilY = -0.6f; p.mouth = Mouth::Flat; p.y -= 0.03f * std::abs (std::sin (t * 10)); } };

        m["wave"] = { 1.6f, nullptr,
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Wave; p.armPhase = t * 12; p.smile = 0.8f; p.eyes = Eyes::Happy; p.blush = 0.5f; } };

        m["flinch"] = { 0.5f, nullptr,
            [] (Pose& p, float, float u, MooniView&) { const float k = std::sin (u * PI); p.eyeOpen = 1 - 0.9f * k; p.sy -= 0.1f * k; p.shake = 0.04f * k; p.mouth = Mouth::Zigzag; } };

        m["confused"] = { 1.8f, [] (MooniView& v) { v.spawn (Particle::Question, { 0.7f, -1.4f }, { 0.1f, -0.3f }, 1.6f, 0.3f, col::lilac); },
            [] (Pose& p, float t, float, MooniView&) { p.rot = 0.15f * smooth (t / 0.3f); p.brow = 0.7f; p.pupilX = std::sin (t * 3) * 0.8f; p.mouth = Mouth::Wavy; p.smile = 0; } };

        m["mono_squint"] = { 1.5f, nullptr,
            [] (Pose& p, float, float, MooniView&) { p.squint = 0.6f; p.eyeOpen = 0.6f; p.pupilX = 0; p.brow = -0.2f; p.mouth = Mouth::Flat; p.arms = Arms::Chin; } };

        m["bow"] = { 1.8f, [amber] (MooniView& v) { v.burst (Particle::Star, 6, 1.0f, amber, 1.2f, 0.1f, 0.1f); },
            [] (Pose& p, float t, float, MooniView&) { p.rot = 0.35f * std::sin (std::min (1.0f, t / 1.5f) * PI); p.eyes = Eyes::Happy; p.smile = 0.8f; p.blush = 0.6f; } };

        m["reference_wow"] = { 2.0f, [cream] (MooniView& v) { v.burst (Particle::Star, 5, 0.9f, cream, 1.0f, 0.08f, 0.0f); },
            [] (Pose& p, float t, float, MooniView&) { p.arms = Arms::Telescope; p.glasses = 1; p.eyeOpen = 1.2f; p.brow = 0.6f; p.mouth = Mouth::Open; p.mouthOpen = 0.35f; p.rot = -0.05f * std::sin (t * 3); } };

        return m;
    }
};

juce::StringArray MooniView::reactionIds()
{
    juce::StringArray s;
    for (auto& [k, v] : ReactionLibrary::get()) s.add (k);
    return s;
}

// ===========================================================================
MooniView::MooniView()
{
    setInterceptsMouseClicks (true, false);
    setRepaintsOnMouseActivity (false);
    current = moodPose (Mood::Neutral);
    from = to = pos;
}

void MooniView::setAnchor (Point<float> c, float r, bool animate)
{
    if (! animate || reducedMotion) { pos = from = to = c; radius = fromR = toR = r; flyT = 1.0f; return; }
    if (c.getDistanceFrom (to) < 0.5f && std::abs (r - toR) < 0.5f) return;
    from = pos; fromR = radius; to = c; toR = r; flyT = 0.0f;
}

bool MooniView::play (const juce::String& id, bool force)
{
    const auto& lib = ReactionLibrary::get();
    auto it = lib.find (id);
    if (it == lib.end()) return false;
    if (active.isNotEmpty() && ! force && activeT < activeDur * 0.6f && id != "poke") return false;
    active = id; activeT = 0; activeDur = it->second.duration;
    steamTimer = 0; noteTimer = 0;
    reactionFlag = false;
    if (it->second.start) it->second.start (*this);
    return true;
}

Pose MooniView::moodPose (Mood m) const
{
    Pose p;
    switch (m)
    {
        case Mood::Happy:     p.smile = 0.75f; p.blush = 0.5f; p.star = col::mint; break;
        case Mood::Excited:   p.smile = 1.0f; p.mouth = Mouth::Grin; p.mouthOpen = 0.35f; p.blush = 0.65f; p.star = col::mint; p.starGlow = 0.9f; break;
        case Mood::Meh:       p.eyeOpen = 0.62f; p.mouth = Mouth::Flat; p.smile = 0.0f; p.star = col::amber; break;
        case Mood::Annoyed:   p.brow = -0.45f; p.squint = 0.18f; p.mouth = Mouth::Wavy; p.smile = -0.2f; p.star = col::orange; break;
        case Mood::Furious:   p.brow = -0.85f; p.squint = 0.35f; p.mouth = Mouth::Zigzag; p.mouthOpen = 0.3f; p.tint = col::coral; p.tintAmt = 0.18f; p.star = col::coral; break;
        case Mood::Listening: p.eyeOpen = 0.08f; p.smile = 0.5f; p.phonesGlow = 1.0f; p.star = col::earth; p.starGlow = 0.8f; break;
        case Mood::Thinking:  p.pupilY = -0.7f; p.pupilX = 0.4f; p.mouth = Mouth::Flat; p.arms = Arms::Chin; p.star = col::lilac; break;
        case Mood::Sleepy:    p.eyeOpen = 0.35f; p.smile = 0.1f; p.star = col::lilacDim; break;
        case Mood::Asleep:    p.eyeOpen = 0.0f; p.mouth = Mouth::Snore; p.smile = 0.1f; p.star = col::lilacDim; p.starGlow = 0.1f; p.eclipse = 0.85f; p.sy = 0.96f; break;
        case Mood::Neutral:   default: p.smile = 0.35f; p.star = col::lilac; break;
    }
    return p;
}

void MooniView::spawn (Particle::Type t, Point<float> localR, Point<float> velR, float life, float sizeR, juce::Colour c, const juce::String& text, float gravity)
{
    if (reducedMotion && particles.size() > 12) return;
    if (particles.size() > 260) return;
    Particle p;
    p.type = t;
    p.pos = pos + Point<float> (localR.x * radius, localR.y * radius);
    p.vel = { velR.x * radius, velR.y * radius };
    p.life = 0; p.maxLife = life; p.size = sizeR * radius; p.colour = c; p.text = text;
    p.rot = rng.nextFloat() * 6.28f; p.spin = (rng.nextFloat() - 0.5f) * 6.0f; p.gravity = gravity * radius;
    particles.push_back (p);
}

void MooniView::burst (Particle::Type t, int count, float speed, juce::Colour c, float life, float size, float gravity)
{
    for (int i = 0; i < count; ++i)
    {
        const float a = rng.nextFloat() * 2 * PI, s = speed * (0.5f + rng.nextFloat());
        spawn (t, { std::cos (a) * 0.8f, std::sin (a) * 0.8f - 0.2f }, { std::cos (a) * s, std::sin (a) * s - 0.5f }, life * (0.7f + 0.6f * rng.nextFloat()), size * (0.7f + 0.6f * rng.nextFloat()), c, {}, gravity);
    }
}

void MooniView::tick (float dt)
{
    dt = std::min (dt, 0.1f);
    time += dt;
    worldShake *= 0.85f;

    // flight between rooms: low-gravity arc
    if (flyT < 1.0f)
    {
        flyT = std::min (1.0f, flyT + dt / 0.85f);
        const float e = smooth (flyT);
        pos = from + (to - from) * e;
        pos.y -= std::sin (flyT * PI) * std::min (140.0f, from.getDistanceFrom (to) * 0.35f);
        radius = fromR + (toR - fromR) * e;
        if (! reducedMotion && rng.nextFloat() < 0.6f) spawn (Particle::Dust, { 0, 0.9f }, { 0, 0.2f }, 0.6f, 0.12f, col::lilac.withAlpha (0.5f));
    }
    else { pos = to; radius = toR; }

    // ---- base: mood + modifiers ----
    Mood m = asleep ? Mood::Asleep : (listening ? Mood::Listening : mood);
    Pose target = moodPose (m);

    // idle life
    const float motion = reducedMotion ? 0.3f : 1.0f;
    target.hover = 0.05f + 0.04f * std::sin (time * 1.6f) * motion;
    target.y -= target.hover;
    target.sy += 0.015f * std::sin (time * 2.2f) * motion;
    target.antennaBend += 0.25f * std::sin (time * 1.3f) * motion;
    target.starSpin = time * 0.6f * motion;

    blinkTimer -= dt;
    if (blinkTimer <= 0 && blinkT < 0) { blinkT = 0; blinkTimer = 2.0f + rng.nextFloat() * 3.5f; }
    float blink = 0;
    if (blinkT >= 0) { blinkT += dt; blink = std::sin (clamp01 (blinkT / 0.16f) * PI); if (blinkT > 0.16f) blinkT = -1; }

    lookTimer -= dt;
    if (lookTimer <= 0) { lookTimer = 2.0f + rng.nextFloat() * 4.0f; idleLook = { (rng.nextFloat() - 0.5f) * 1.4f, (rng.nextFloat() - 0.5f) * 0.8f }; }
    Point<float> look = idleLook;
    if (lookValid && m != Mood::Asleep)
    {
        auto d = lookTarget - pos;
        const float len = std::max (1.0f, d.getDistanceFromOrigin());
        look = { d.x / len * std::min (1.0f, len / (radius * 3)), d.y / len * std::min (1.0f, len / (radius * 3)) };
    }
    target.pupilX += look.x; target.pupilY += look.y;

    if (squish > 0.01f) { target.sy -= 0.32f * squish; target.sx += 0.22f * squish; target.y += 0.3f * squish; target.eyeOpen = std::min (target.eyeOpen, 1.0f - 0.5f * squish); target.brow += 0.4f * squish; }
    if (std::abs (stretch - 1) > 0.01f) { target.sx *= stretch; target.sy *= 1.0f / std::sqrt (stretch); }
    target.glasses = glassesOn ? 1.0f : 0.0f;

    if (listening && ! asleep)
    {
        nodImpulse *= std::pow (0.02f, dt);
        target.rot += 0.1f * nodImpulse; target.y += 0.06f * nodImpulse;
        noteTimer -= dt;
        if (noteTimer <= 0 && ! reducedMotion) { noteTimer = 0.9f + rng.nextFloat() * 0.8f; spawn (Particle::Note, { (rng.nextFloat() > 0.5f ? 1.1f : -1.1f), -0.6f }, { (rng.nextFloat() - 0.5f) * 0.5f, -0.5f }, 1.6f, 0.18f, col::earth.brighter (0.3f)); }
    }
    if (asleep)
    {
        zzzTimer -= dt;
        if (zzzTimer <= 0) { zzzTimer = 1.1f; spawn (Particle::Zzz, { 0.6f, -0.9f }, { 0.25f, -0.35f }, 2.2f, 0.22f, col::lilac, "z"); }
    }

    // smooth towards the target (keeps everything springy, never jumpy)
    const float k = 1.0f - std::exp (-dt * 9.0f);
    current = Pose::lerp (current, target, k);
    current.mouth = target.mouth; current.arms = target.arms; current.eyes = target.eyes;
    current.eyeOpen = juce::jlimit (0.0f, 1.3f, current.eyeOpen * (1.0f - blink));

    // ---- active reaction (evaluated here, never in paint) ----
    shownPose = current;
    if (active.isNotEmpty())
    {
        activeT += dt;
        const auto& lib = ReactionLibrary::get();
        auto it = lib.find (active);
        if (it == lib.end() || activeT >= activeDur) active.clear();
        else
        {
            Pose r = current;
            it->second.apply (r, activeT, activeT / std::max (0.01f, activeDur), *this);
            const float env = smooth (activeT / 0.15f) * (1.0f - smooth ((activeT - (activeDur - 0.22f)) / 0.22f));
            Pose blended = Pose::lerp (current, r, env);
            if (env > 0.5f) { blended.mouth = r.mouth; blended.arms = r.arms; blended.eyes = r.eyes; }
            blended.exploded = r.exploded;
            if (active == "explode") blended.scale = r.scale;   // regrowth must not be smoothed away
            shownPose = blended;
        }
    }

    updateParticles (dt);

    // dirty region: mooni + particles + previous frame
    auto b = getBodyBounds().expanded (radius * 1.2f, radius * 1.4f);
    for (auto& p : particles) b = b.getUnion (juce::Rectangle<float> (p.size * 4, p.size * 4).withCentre (p.pos));
    const auto dirty = b.getSmallestIntegerContainer().getIntersection (getLocalBounds());
    repaint (dirty.getUnion (lastDirty));
    lastDirty = dirty;
}

juce::Rectangle<float> MooniView::getBodyBounds() const
{
    return juce::Rectangle<float> (radius * 2.6f, radius * 2.9f).withCentre (pos + Point<float> (0, -radius * 0.1f));
}

void MooniView::updateParticles (float dt)
{
    for (auto& p : particles)
    {
        p.life += dt;
        p.vel.y += p.gravity * dt;
        p.pos += p.vel * dt;
        p.rot += p.spin * dt;
        if (p.type == Particle::Steam || p.type == Particle::Smoke) p.size *= 1.0f + dt * 1.2f;
        if (p.type == Particle::Zzz) p.size *= 1.0f + dt * 0.35f;
    }
    particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life >= p.maxLife; }), particles.end());
}

bool MooniView::hitTest (int x, int y)
{
    const auto b = juce::Rectangle<float> (radius * 2.2f, radius * 2.2f).withCentre (pos);
    return b.contains ((float) x, (float) y) && (peekLine <= 0 || (float) y < peekLine);
}

void MooniView::mouseDown (const juce::MouseEvent&)
{
    if (onPoked) onPoked();
}

// ===========================================================================
// Drawing
// ===========================================================================
void MooniView::paint (juce::Graphics& g)
{
    Pose p = shownPose;
    if (p.shake > 0 && ! reducedMotion) { p.x += (rng.nextFloat() - 0.5f) * p.shake * 2.0f; p.y += (rng.nextFloat() - 0.5f) * p.shake * 2.0f; }

    // ground shadow (not when peeking)
    if (peekLine <= 0)
    {
        const float h = p.hover + std::max (0.0f, -p.y);
        auto sh = juce::Rectangle<float> (radius * 1.5f / (1.0f + h), radius * 0.22f).withCentre (pos + Point<float> (p.x * radius, radius * 1.13f));
        g.setColour (juce::Colours::black.withAlpha (0.28f / (1.0f + h * 2.0f)));
        g.fillEllipse (sh);
    }

    drawMooni (g, pos, radius, p, time, peekLine);
    drawParticles (g);
}

static juce::Colour tinted (juce::Colour c, const Pose& p)
{
    return p.tintAmt > 0 ? c.interpolatedWith (p.tint, juce::jlimit (0.0f, 1.0f, p.tintAmt)) : c;
}

void MooniView::drawMooni (juce::Graphics& g, Point<float> centre, float R0, const Pose& p, float t, float peekLine)
{
    juce::Graphics::ScopedSaveState save (g);
    if (peekLine > 0) g.reduceClipRegion (juce::Rectangle<float> (centre.x - R0 * 4, centre.y - R0 * 5, R0 * 8, peekLine - (centre.y - R0 * 5)).toNearestInt());

    const float R = R0 * p.scale;
    const auto c = centre + Point<float> (p.x * R0, p.y * R0);
    g.addTransform (juce::AffineTransform::rotation (p.rot, c.x, c.y));

    const float bx = R * p.sx * (1.0f + p.inflate), by = R * p.sy * (1.0f + p.inflate);
    auto P = [&] (float ux, float uy) { return Point<float> (c.x + ux * bx, c.y + uy * by); };

    const auto creamC = tinted (col::cream, p), shadeC = tinted (juce::Colour (0xffcdb9e6), p);
    const auto ink = col::night1;
    const juce::Colour phoneBand (0xff3a2d73), phoneCup (0xff46378f);

    // --- phase ghosts ---
    if (p.ghost > 0.01f && ! p.exploded)
        for (int s : { -1, 1 })
        {
            g.setColour (col::lilac.withAlpha (0.28f * std::min (1.0f, p.ghost * 2.0f)));
            g.fillEllipse (juce::Rectangle<float> (bx * 2, by * 2).withCentre (c + Point<float> ((float) s * p.ghost * R * 0.9f, 0)));
        }

    // --- feet ---
    for (int s : { -1, 1 })
    {
        auto f = juce::Rectangle<float> (R * 0.34f, R * 0.18f).withCentre (Point<float> (c.x + (float) s * R * 0.34f * p.sx, c.y + by * 0.98f + R * 0.02f));
        g.setColour (creamC.darker (0.18f)); g.fillEllipse (f);
        g.setColour (ink.withAlpha (0.35f)); g.drawEllipse (f, std::max (1.0f, R * 0.02f));
    }

    // --- arms (computed hand targets) ---
    auto drawArm = [&] (int side, Point<float> hand, Point<float> ctrl)
    {
        const auto shoulder = P (0.78f * (float) side, 0.28f);
        juce::Path arm; arm.startNewSubPath (shoulder); arm.quadraticTo (ctrl, hand);
        g.setColour (ink.withAlpha (0.55f));
        g.strokePath (arm, juce::PathStrokeType (R * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (creamC.darker (0.06f));
        g.strokePath (arm, juce::PathStrokeType (R * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (creamC);
        g.fillEllipse (juce::Rectangle<float> (R * 0.2f, R * 0.2f).withCentre (hand));
        g.setColour (ink.withAlpha (0.45f));
        g.drawEllipse (juce::Rectangle<float> (R * 0.2f, R * 0.2f).withCentre (hand), std::max (1.0f, R * 0.02f));
    };
    auto armTargets = [&] (int s, Point<float>& hand, Point<float>& ctrl)
    {
        const float fs = (float) s;
        switch (p.arms)
        {
            case Arms::Up:        hand = P (1.05f * fs, -0.85f - 0.15f * std::sin (p.armPhase * 6 + fs)); ctrl = P (1.25f * fs, -0.1f); break;
            case Arms::CoverEars: hand = P (1.05f * fs, -0.12f - 0.1f * p.armPhase); ctrl = P (1.2f * fs, 0.3f); break;
            case Arms::Facepalm:  if (s > 0) { hand = P (0.32f, -0.12f); ctrl = P (0.9f, 0.6f); } else { hand = P (-1.0f, 0.62f); ctrl = P (-1.0f, 0.4f); } break;
            case Arms::Hips:      hand = P (0.9f * fs, 0.52f); ctrl = P (1.35f * fs, 0.2f); break;
            case Arms::Chin:      if (s > 0) { hand = P (0.15f, 0.62f); ctrl = P (0.8f, 0.85f); } else { hand = P (-1.0f, 0.62f); ctrl = P (-1.0f, 0.4f); } break;
            case Arms::Wave:      if (s > 0) { hand = P (1.2f, -0.5f + 0.15f * std::sin (p.armPhase)); ctrl = P (1.3f, 0.1f); } else { hand = P (-1.05f, 0.6f); ctrl = P (-1.0f, 0.4f); } break;
            case Arms::Telescope: hand = P (0.45f + 0.15f * fs, 0.15f); ctrl = P (0.9f * fs, 0.6f); break;
            case Arms::Hammer:    if (s > 0) { hand = P (1.15f, -0.6f + 0.45f * p.armPhase); ctrl = P (1.35f, 0.0f); } else { hand = P (-1.0f, 0.6f); ctrl = P (-1.0f, 0.4f); } break;
            case Arms::Shrug:     hand = P (1.3f * fs, -0.05f); ctrl = P (1.1f * fs, 0.5f); break;
            case Arms::Read:      hand = P (0.35f * fs, 0.7f); ctrl = P (0.9f * fs, 0.75f); break;
            case Arms::Idle: default: hand = P (1.02f * fs, 0.62f + 0.03f * std::sin (t * 2 + fs)); ctrl = P (1.0f * fs, 0.42f); break;
        }
    };

    if (! p.exploded)
    {
        // --- body ---
        auto body = juce::Rectangle<float> (bx * 2, by * 2).withCentre (c);
        juce::ColourGradient grad (creamC.brighter (0.08f), c.x - bx * 0.5f, c.y - by * 0.6f, shadeC, c.x + bx * 0.7f, c.y + by * 0.9f, true);
        g.setGradientFill (grad);
        g.fillEllipse (body);
        g.setColour (ink.withAlpha (0.55f));
        g.drawEllipse (body, std::max (1.2f, R * 0.035f));
        // rim light
        juce::Path rim; rim.addCentredArc (c.x, c.y, bx * 0.93f, by * 0.93f, 0, PI * 0.55f, PI * 0.95f, true);
        g.setColour (col::lilac.withAlpha (0.55f));
        g.strokePath (rim, juce::PathStrokeType (R * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // crater freckles
        const Point<float> craters[] = { { -0.48f, -0.46f }, { -0.66f, -0.06f }, { 0.52f, -0.58f }, { 0.4f, 0.58f }, { -0.2f, 0.7f } };
        const float cr[] = { 0.12f, 0.07f, 0.07f, 0.1f, 0.06f };
        for (int i = 0; i < 5; ++i) drawCrater (g, P (craters[i].x, craters[i].y), R * cr[i], creamC.darker (0.05f), 0.55f);

        // cheeks
        if (p.blush > 0.02f)
            for (int s : { -1, 1 })
            {
                g.setColour (col::pink.withAlpha (0.45f * juce::jlimit (0.0f, 1.0f, p.blush)));
                g.fillEllipse (juce::Rectangle<float> (R * 0.26f, R * 0.13f).withCentre (P (0.56f * (float) s, 0.2f)));
            }

        // --- eyes ---
        for (int s : { -1, 1 })
        {
            const auto ec = P (0.33f * (float) s, -0.1f);
            const float ew = R * 0.29f, eh = R * 0.36f;
            auto er = juce::Rectangle<float> (ew, eh).withCentre (ec);
            if (p.eyes == Eyes::Happy || (p.eyeOpen < 0.08f && p.mouth != Mouth::Snore && p.smile > 0.3f))
            {
                juce::Path arc; arc.addCentredArc (ec.x, ec.y + eh * 0.1f, ew * 0.42f, eh * 0.3f, 0, -PI * 0.5f, PI * 0.5f, true);
                g.setColour (ink); g.strokePath (arc, juce::PathStrokeType (R * 0.055f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                continue;
            }
            if (p.eyeOpen < 0.06f)
            {
                juce::Path l; l.startNewSubPath (ec.x - ew * 0.4f, ec.y + eh * 0.05f); l.quadraticTo (ec.x, ec.y + eh * 0.22f, ec.x + ew * 0.4f, ec.y + eh * 0.05f);
                g.setColour (ink); g.strokePath (l, juce::PathStrokeType (R * 0.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                continue;
            }
            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path eyeP; eyeP.addEllipse (er);
                g.reduceClipRegion (eyeP);
                g.setColour (juce::Colour (0xfffffdf8)); g.fillEllipse (er);
                const auto pc = ec + Point<float> (p.pupilX * ew * 0.22f, p.pupilY * eh * 0.2f);
                if (p.eyes == Eyes::Stars)
                {
                    drawStar (g, pc, ew * 0.42f, ew * 0.18f, t * 2.0f, col::amber);
                }
                else if (p.eyes == Eyes::Spiral)
                {
                    juce::Path sp;
                    for (int k = 0; k < 40; ++k) { const float a = (float) k * 0.45f + t * 8.0f * (float) s, rr = ew * 0.012f * (float) k; const auto q = pc + Point<float> (std::cos (a) * rr, std::sin (a) * rr); if (k == 0) sp.startNewSubPath (q); else sp.lineTo (q); }
                    g.setColour (ink); g.strokePath (sp, juce::PathStrokeType (R * 0.03f));
                }
                else if (p.eyes == Eyes::Cross)
                {
                    g.setColour (ink);
                    const float d = ew * 0.25f;
                    g.drawLine (pc.x - d, pc.y - d, pc.x + d, pc.y + d, R * 0.05f); g.drawLine (pc.x - d, pc.y + d, pc.x + d, pc.y - d, R * 0.05f);
                }
                else
                {
                    const float pr = ew * 0.3f * p.pupilSize;
                    g.setColour (ink); g.fillEllipse (juce::Rectangle<float> (pr * 2, pr * 2.2f).withCentre (pc));
                    g.setColour (juce::Colours::white); g.fillEllipse (juce::Rectangle<float> (pr * 0.7f, pr * 0.7f).withCentre (pc + Point<float> (-pr * 0.35f, -pr * 0.45f)));
                }
                // lids
                const float lidTop = er.getY() + eh * (1.0f - juce::jlimit (0.0f, 1.0f, p.eyeOpen)) - (p.eyeOpen > 1.0f ? 0.0f : 0.0f);
                g.setColour (creamC.darker (0.04f));
                g.fillRect (juce::Rectangle<float> (er.getX() - 2, er.getY() - 2, ew + 4, lidTop - er.getY() + 2));
                if (p.squint > 0.01f) g.fillRect (juce::Rectangle<float> (er.getX() - 2, er.getBottom() - eh * p.squint, ew + 4, eh * p.squint + 2));
                if (p.eyeOpen < 0.98f)
                {
                    g.setColour (ink.withAlpha (0.8f));
                    g.drawLine (er.getX(), lidTop, er.getRight(), lidTop, std::max (1.2f, R * 0.035f));
                }
            }
            g.setColour (ink.withAlpha (0.85f));
            g.drawEllipse (er, std::max (1.2f, R * 0.03f));
        }

        // brows
        for (int s : { -1, 1 })
        {
            const float fs = (float) s;
            const float inner = -0.44f - p.browY + (p.brow < 0 ? -p.brow * 0.1f : -p.brow * 0.05f);
            const float outer = -0.48f - p.browY - (p.brow < 0 ? -p.brow * 0.02f : p.brow * 0.06f);
            if (std::abs (p.brow) < 0.12f && p.browY < 0.02f) continue;
            g.setColour (ink);
            g.drawLine (P (0.18f * fs, inner).x, P (0.18f * fs, inner).y, P (0.48f * fs, outer).x, P (0.48f * fs, outer).y, R * 0.065f);
        }

        // mouth
        {
            const auto mc = P (0.0f, 0.33f);
            const float w = R * 0.38f;
            juce::PathStrokeType st (R * 0.055f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour (ink);
            switch (p.mouth)
            {
                case Mouth::Smile: case Mouth::Tongue:
                {
                    juce::Path m; m.startNewSubPath (mc.x - w * 0.5f, mc.y); m.quadraticTo (mc.x, mc.y + p.smile * R * 0.24f, mc.x + w * 0.5f, mc.y);
                    g.strokePath (m, st);
                    if (p.mouth == Mouth::Tongue)
                    {
                        g.setColour (col::pink); g.fillEllipse (juce::Rectangle<float> (w * 0.36f, R * 0.2f).withCentre (mc + Point<float> (w * 0.1f, R * 0.12f)));
                        g.setColour (ink.withAlpha (0.6f)); g.drawEllipse (juce::Rectangle<float> (w * 0.36f, R * 0.2f).withCentre (mc + Point<float> (w * 0.1f, R * 0.12f)), R * 0.02f);
                    }
                    break;
                }
                case Mouth::Open: case Mouth::Snore:
                {
                    const float o = p.mouth == Mouth::Snore ? 0.08f + 0.06f * std::sin (t * 2.0f) : p.mouthOpen;
                    auto r = juce::Rectangle<float> (w * 0.48f, R * (0.08f + o * 0.22f)).withCentre (mc + Point<float> (0, R * 0.03f));
                    g.fillEllipse (r);
                    g.setColour (col::pink.darker (0.2f)); g.fillEllipse (r.withTrimmedTop (r.getHeight() * 0.55f).reduced (r.getWidth() * 0.15f, 0));
                    break;
                }
                case Mouth::Grin:
                {
                    juce::Path m;
                    const float depth = R * (0.12f + 0.16f * p.mouthOpen);
                    m.startNewSubPath (mc.x - w * 0.55f, mc.y - R * 0.03f); m.lineTo (mc.x + w * 0.55f, mc.y - R * 0.03f);
                    m.quadraticTo (mc.x + w * 0.45f, mc.y + depth * 1.4f, mc.x, mc.y + depth * 1.3f);
                    m.quadraticTo (mc.x - w * 0.45f, mc.y + depth * 1.4f, mc.x - w * 0.55f, mc.y - R * 0.03f);
                    m.closeSubPath();
                    g.fillPath (m);
                    {
                        juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (m);
                        g.setColour (juce::Colours::white); g.fillRect (juce::Rectangle<float> (mc.x - w * 0.6f, mc.y - R * 0.05f, w * 1.2f, R * 0.07f));
                        g.setColour (col::pink); g.fillEllipse (juce::Rectangle<float> (w * 0.6f, depth).withCentre (mc + Point<float> (0, depth * 1.2f)));
                    }
                    break;
                }
                case Mouth::Flat:
                    g.drawLine (mc.x - w * 0.36f, mc.y + R * 0.04f, mc.x + w * 0.36f, mc.y + R * 0.04f + p.smile * R * 0.05f, R * 0.055f);
                    break;
                case Mouth::Zigzag:
                {
                    auto r = juce::Rectangle<float> (w * 1.05f, R * (0.1f + 0.12f * p.mouthOpen)).withCentre (mc + Point<float> (0, R * 0.03f));
                    g.fillRoundedRectangle (r, R * 0.04f);
                    juce::Path z; const int n = 6;
                    for (int i = 0; i <= n; ++i) { const float x = r.getX() + r.getWidth() * (float) i / n; const float y = (i % 2 == 0) ? r.getY() + 1 : r.getCentreY(); if (i == 0) z.startNewSubPath (x, y); else z.lineTo (x, y); }
                    for (int i = n; i >= 0; --i) { const float x = r.getX() + r.getWidth() * (float) i / n; const float y = (i % 2 == 0) ? r.getBottom() - 1 : r.getCentreY(); z.lineTo (x, y); }
                    g.setColour (juce::Colours::white); g.strokePath (z, juce::PathStrokeType (R * 0.02f));
                    break;
                }
                case Mouth::Wavy:
                {
                    juce::Path m;
                    for (int i = 0; i <= 16; ++i) { const float u = (float) i / 16.0f; const auto q = Point<float> (mc.x - w * 0.45f + u * w * 0.9f, mc.y + R * 0.04f + std::sin (u * PI * 3) * R * 0.035f - p.smile * R * 0.05f * std::sin (u * PI)); if (i == 0) m.startNewSubPath (q); else m.lineTo (q); }
                    g.strokePath (m, st);
                    break;
                }
            }
        }

        // glasses
        if (p.glasses > 0.05f)
        {
            g.setColour (ink.withAlpha (p.glasses));
            for (int s : { -1, 1 }) g.drawEllipse (juce::Rectangle<float> (R * 0.4f, R * 0.4f).withCentre (P (0.33f * (float) s, -0.1f)), R * 0.04f);
            g.drawLine (P (-0.13f, -0.12f).x, P (-0.13f, -0.12f).y, P (0.13f, -0.12f).x, P (0.13f, -0.12f).y, R * 0.04f);
            g.setColour (juce::Colours::white.withAlpha (0.25f * p.glasses));
            for (int s : { -1, 1 }) g.fillEllipse (juce::Rectangle<float> (R * 0.12f, R * 0.08f).withCentre (P (0.33f * (float) s - 0.08f, -0.2f)));
        }

        // telescope prop
        if (p.arms == Arms::Telescope)
        {
            juce::Path tube; tube.addRoundedRectangle (-R * 0.15f, -R * 0.1f, R * 1.1f, R * 0.22f, R * 0.08f);
            tube.applyTransform (juce::AffineTransform::rotation (-0.5f).translated (P (0.5f, 0.12f)));
            g.setColour (juce::Colour (0xff5a48a8)); g.fillPath (tube);
            g.setColour (col::amber); g.strokePath (tube, juce::PathStrokeType (R * 0.03f));
        }
        if (p.arms == Arms::Read)
        {
            auto book = juce::Rectangle<float> (R * 0.9f, R * 0.5f).withCentre (P (0.0f, 0.72f));
            g.setColour (juce::Colour (0xffe8dcc4)); g.fillRoundedRectangle (book, R * 0.04f);
            g.setColour (col::lilacDim); g.drawLine (book.getCentreX(), book.getY(), book.getCentreX(), book.getBottom(), R * 0.03f);
        }
        if (p.arms == Arms::Hammer)
        {
            Point<float> hand, ctrl; armTargets (1, hand, ctrl);
            juce::Path w; w.addRoundedRectangle (-R * 0.04f, -R * 0.45f, R * 0.08f, R * 0.45f, R * 0.03f); w.addEllipse (-R * 0.12f, -R * 0.6f, R * 0.24f, R * 0.2f);
            w.applyTransform (juce::AffineTransform::rotation (0.4f + 0.5f * p.armPhase).translated (hand));
            g.setColour (col::lilacDim); g.fillPath (w);
        }

        for (int s : { -1, 1 }) { Point<float> hand, ctrl; armTargets (s, hand, ctrl); drawArm (s, hand, ctrl); }
    }

    // --- headphones (always; float away when exploded) ---
    {
        juce::Graphics::ScopedSaveState ss (g);
        const auto hc = c + Point<float> (0, -p.phonesLift * R);
        g.addTransform (juce::AffineTransform::rotation (p.phonesRot, hc.x, hc.y));
        const float hbx = R * 1.06f * p.sx * (1.0f + p.inflate), hby = R * 1.08f * p.sy * (1.0f + p.inflate);
        juce::Path band; band.addCentredArc (hc.x, hc.y, hbx, hby, 0, -PI * 0.42f, PI * 0.42f, true);
        g.setColour (ink); g.strokePath (band, juce::PathStrokeType (R * 0.17f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (phoneBand); g.strokePath (band, juce::PathStrokeType (R * 0.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path hl; hl.addCentredArc (hc.x, hc.y, hbx - R * 0.02f, hby - R * 0.02f, 0, -PI * 0.3f, -PI * 0.05f, true);
        g.setColour (col::lilac.withAlpha (0.5f)); g.strokePath (hl, juce::PathStrokeType (R * 0.03f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int s : { -1, 1 })
        {
            auto cup = juce::Rectangle<float> (R * 0.28f, R * 0.5f).withCentre (hc + Point<float> ((float) s * hbx * 0.96f, -R * 0.02f));
            if (p.phonesGlow > 0.05f) drawGlow (g, cup.getCentre(), R * 0.5f, col::mint, 0.35f * p.phonesGlow);
            g.setColour (ink); g.fillRoundedRectangle (cup.expanded (R * 0.025f), R * 0.12f);
            g.setColour (phoneCup); g.fillRoundedRectangle (cup, R * 0.11f);
            g.setColour (p.phonesGlow > 0.5f ? col::mint : col::coral);
            g.fillRoundedRectangle (cup.reduced (R * 0.06f, R * 0.12f).translated ((float) -s * R * 0.03f, 0), R * 0.05f);
        }

        // antenna + star
        const auto base = hc + Point<float> (0, -hby);
        const auto tip = base + Point<float> (p.antennaBend * R * 0.35f, -R * 0.45f);
        juce::Path ant; ant.startNewSubPath (base); ant.quadraticTo (base + Point<float> (-p.antennaBend * R * 0.2f, -R * 0.25f), tip);
        g.setColour (ink); g.strokePath (ant, juce::PathStrokeType (R * 0.045f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawGlow (g, tip, R * 0.45f, p.star, 0.25f + 0.35f * p.starGlow);
        drawStar (g, tip, R * 0.17f, R * 0.075f, p.starSpin, p.star);
    }

    // --- eclipse shadow (sleep / bypass) ---
    if (p.eclipse > 0.01f && ! p.exploded)
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path bodyP; bodyP.addEllipse (juce::Rectangle<float> (bx * 2, by * 2).withCentre (c));
        g.reduceClipRegion (bodyP);
        const float off = (1.0f - p.eclipse) * bx * 2.1f;
        g.setColour (col::night0.withAlpha (0.78f));
        g.fillEllipse (juce::Rectangle<float> (bx * 2.1f, by * 2.1f).withCentre (c + Point<float> (-off, -off * 0.2f)));
    }
}

void MooniView::drawParticles (juce::Graphics& g)
{
    for (auto& p : particles)
    {
        const float u = p.life / p.maxLife;
        const float a = (1.0f - u) * std::min (1.0f, u * 8.0f + 0.2f);
        const auto col = p.colour.withMultipliedAlpha (a);
        const float s = p.size;
        switch (p.type)
        {
            case Particle::Steam: case Particle::Smoke: case Particle::Dust:
                g.setColour (col.withMultipliedAlpha (p.type == Particle::Dust ? 0.6f : 0.75f));
                g.fillEllipse (juce::Rectangle<float> (s * 2, s * 2).withCentre (p.pos));
                break;
            case Particle::Sweat:
            {
                juce::Path d; d.startNewSubPath (p.pos.x, p.pos.y - s); d.quadraticTo (p.pos.x + s, p.pos.y + s * 0.6f, p.pos.x, p.pos.y + s);
                d.quadraticTo (p.pos.x - s, p.pos.y + s * 0.6f, p.pos.x, p.pos.y - s);
                g.setColour (col::earth.withMultipliedAlpha (a)); g.fillPath (d);
                break;
            }
            case Particle::Zzz: case Particle::Text: case Particle::Question: case Particle::Exclaim:
            {
                g.setColour (col);
                g.setFont (display (s * (p.type == Particle::Text ? 1.0f : 1.6f)));
                const juce::String txt = p.type == Particle::Question ? "?" : p.type == Particle::Exclaim ? "!" : p.text;
                g.drawText (txt, juce::Rectangle<float> (s * 8, s * 2.5f).withCentre (p.pos), juce::Justification::centred);
                break;
            }
            case Particle::Note:
            {
                g.setColour (col);
                g.fillEllipse (juce::Rectangle<float> (s * 1.1f, s * 0.85f).withCentre (p.pos));
                g.fillRect (juce::Rectangle<float> (p.pos.x + s * 0.4f, p.pos.y - s * 1.6f, s * 0.18f, s * 1.6f));
                juce::Path f; f.startNewSubPath (p.pos.x + s * 0.5f, p.pos.y - s * 1.6f); f.quadraticTo (p.pos.x + s * 1.2f, p.pos.y - s * 1.2f, p.pos.x + s * 0.9f, p.pos.y - s * 0.7f);
                g.strokePath (f, juce::PathStrokeType (s * 0.18f));
                break;
            }
            case Particle::Spark:
                drawStar (g, p.pos, s, s * 0.35f, p.rot, col);
                break;
            case Particle::Star:
                drawGlow (g, p.pos, s * 2.0f, p.colour, 0.3f * a);
                drawStar (g, p.pos, s, s * 0.45f, p.rot, col);
                break;
            case Particle::Confetti:
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::rotation (p.rot, p.pos.x, p.pos.y));
                g.setColour (col); g.fillRect (juce::Rectangle<float> (s * 1.4f, s * 0.6f).withCentre (p.pos));
                break;
            }
            case Particle::Fragment:
            {
                juce::Path f;
                for (int i = 0; i < 6; ++i) { const float ang = p.rot + (float) i * PI / 3.0f, r = s * (0.75f + 0.3f * (float) ((i * 7) % 3) / 2.0f); const auto q = p.pos + Point<float> (std::cos (ang) * r, std::sin (ang) * r); if (i == 0) f.startNewSubPath (q); else f.lineTo (q); }
                f.closeSubPath();
                g.setColour (col); g.fillPath (f);
                g.setColour (col::night1.withAlpha (0.5f * a)); g.strokePath (f, juce::PathStrokeType (1.0f));
                g.setColour (col.darker (0.3f)); g.fillEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.4f).withCentre (p.pos));
                break;
            }
            case Particle::Heart:
            {
                juce::Path h; const float x = p.pos.x, y = p.pos.y;
                h.startNewSubPath (x, y + s * 0.7f); h.cubicTo (x - s * 1.4f, y - s * 0.2f, x - s * 0.5f, y - s * 1.1f, x, y - s * 0.35f);
                h.cubicTo (x + s * 0.5f, y - s * 1.1f, x + s * 1.4f, y - s * 0.2f, x, y + s * 0.7f);
                g.setColour (col); g.fillPath (h);
                break;
            }
            case Particle::Bulb:
            {
                drawGlow (g, p.pos, s * 2.2f, col::amber, 0.45f * a);
                g.setColour (col); g.fillEllipse (juce::Rectangle<float> (s * 1.2f, s * 1.2f).withCentre (p.pos));
                g.setColour (col::lilacDim.withMultipliedAlpha (a)); g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.6f, s * 0.45f).withCentre (p.pos + Point<float> (0, s * 0.75f)), s * 0.1f);
                break;
            }
        }
    }
}
} // namespace mooni::ui
