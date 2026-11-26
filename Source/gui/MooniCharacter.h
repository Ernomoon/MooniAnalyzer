#pragma once
#include "Theme.h"
#include <functional>
#include <map>

namespace mooni::ui
{
enum class Mood { Neutral, Happy, Excited, Meh, Annoyed, Furious, Listening, Thinking, Sleepy, Asleep };
enum class Mouth { Smile, Open, Grin, Flat, Zigzag, Wavy, Tongue, Snore };
enum class Arms { Idle, Wave, CoverEars, Up, Facepalm, Hips, Chin, Telescope, Hammer, Shrug, Read };
enum class Eyes { Normal, Stars, Spiral, Cross, Happy };

/** Every drawable degree of freedom of Mooni. Units: body radius R, origin = body centre. */
struct Pose
{
    float x = 0, y = 0, scale = 1, sx = 1, sy = 1, rot = 0;
    float eyeOpen = 1, squint = 0, pupilX = 0, pupilY = 0, pupilSize = 1;
    Eyes eyes = Eyes::Normal;
    float brow = 0, browY = 0;           // brow < 0 angry, > 0 worried / surprised
    Mouth mouth = Mouth::Smile; float smile = 0.35f, mouthOpen = 0;
    float blush = 0.25f;
    juce::Colour tint = juce::Colours::transparentBlack; float tintAmt = 0;
    Arms arms = Arms::Idle; float armPhase = 0;
    float antennaBend = 0, starSpin = 0, starGlow = 0.5f;
    juce::Colour star { 0xffb9a5f2 };
    float phonesGlow = 0, phonesLift = 0, phonesRot = 0;
    float eclipse = 0, inflate = 0, ghost = 0, glasses = 0, shake = 0;
    bool exploded = false;
    float hover = 0;                       // height above ground (R units)

    static Pose lerp (const Pose& a, const Pose& b, float t);
};

struct Particle
{
    enum Type { Steam, Sweat, Zzz, Note, Spark, Star, Confetti, Fragment, Text, Question, Exclaim, Dust, Heart, Smoke, Bulb };
    Type type = Spark;
    juce::Point<float> pos, vel;
    float life = 0, maxLife = 1, size = 6, rot = 0, spin = 0, gravity = 0;
    juce::Colour colour;
    juce::String text;
};

class MooniView : public juce::Component
{
public:
    MooniView();

    /** Where Mooni should be (canvas coordinates) and how big. Moves in a low-gravity arc. */
    void setAnchor (juce::Point<float> centre, float radius, bool animate = true);
    /** Only draw Mooni above this y (canvas). 0 disables. Used to "peek" over panels. */
    void setPeekLine (float y) { peekLine = y; }
    void setMood (Mood m) { mood = m; }
    Mood getMood() const { return mood; }

    /** Start a reaction by id (see MooniCharacter.cpp registry). Returns false if unknown. */
    bool play (const juce::String& reactionId, bool force = false);
    bool isBusy() const { return active.isNotEmpty(); }
    juce::String currentReaction() const { return active; }
    static juce::StringArray reactionIds();

    void setLookTarget (juce::Point<float> canvasPos, bool valid) { lookTarget = canvasPos; lookValid = valid; }
    void setSquish (float amount01) { squish = juce::jlimit (0.0f, 1.0f, amount01); }
    void setStretch (float factor)  { stretch = juce::jlimit (0.4f, 1.8f, factor); }
    void setListening (bool b) { listening = b; }
    void setAsleep (bool b) { asleep = b; }
    void setGlasses (bool b) { glassesOn = b; }
    void nod() { nodImpulse = 1.0f; }
    void setReducedMotion (bool b) { reducedMotion = b; }

    void tick (float dt);
    juce::Rectangle<float> getBodyBounds() const;
    juce::Point<float> getCentre() const { return pos; }
    float getRadius() const { return radius; }
    float getShakeForWorld() const { return worldShake; }

    std::function<void()> onPoked;

    // particle API (also used by reactions)
    void spawn (Particle::Type t, juce::Point<float> localR, juce::Point<float> velR, float life, float sizeR,
                juce::Colour c, const juce::String& text = {}, float gravity = 0.0f);
    void burst (Particle::Type t, int count, float speed, juce::Colour c, float life = 1.2f, float size = 0.12f, float gravity = 0.4f);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Draw Mooni with an explicit pose (used for icons / about page as well). */
    static void drawMooni (juce::Graphics&, juce::Point<float> centre, float R, const Pose&, float timeSec, float peekLine = 0.0f);

private:
    Pose moodPose (Mood m) const;
    void updateParticles (float dt);
    void drawParticles (juce::Graphics&);

    Mood mood = Mood::Neutral;
    juce::Point<float> pos { 200, 300 }, from, to;
    float radius = 90, fromR = 90, toR = 90, flyT = 1.0f;
    float peekLine = 0;

    Pose current, shownPose;
    bool reactionFlag = false;
    float time = 0;
    juce::String active; float activeT = 0, activeDur = 0; Pose activeBase;

    // idle state
    float blinkTimer = 2.5f, blinkT = -1, lookTimer = 2.0f;
    juce::Point<float> idleLook;
    juce::Point<float> lookTarget; bool lookValid = false;
    float squish = 0, stretch = 1, nodImpulse = 0, worldShake = 0;
    bool listening = false, asleep = false, glassesOn = false, reducedMotion = false;
    float zzzTimer = 0, noteTimer = 0, steamTimer = 0;

    std::vector<Particle> particles;
    juce::Rectangle<int> lastDirty;
    juce::Random rng;

    friend struct ReactionLibrary;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MooniView)
};
} // namespace mooni::ui
