#pragma once
#include "MooniCharacter.h"
#include "../core/PluginProcessor.h"
#include <deque>

namespace mooni::ui
{
/** Turns real events (analysis, parameters, user actions) into Mooni's reactions and lines. */
class MooniBrain
{
public:
    using SayFn = std::function<void (const juce::String& text, float seconds)>;

    MooniBrain (MooniProcessor&, MooniView&, SayFn say);

    void update (const LiveData& live, float dt);
    void onRoomChanged (int room);
    void onPoked();
    void onAction (const juce::String& id, const juce::String& detail = {});
    void onEarthClicked();
    void onBipClicked();
    void sayTip();

    static Mood moodForScore (int score);
    static juce::String gradeWord (int score);

private:
    juce::String pick (const juce::String& key);
    void say (const juce::String& text, float secs = 0.0f);
    void sayKey (const juce::String& key, float secs = 0.0f) { say (pick (key), secs); }
    void queue (const juce::String& reaction, const juce::String& line = {});
    void reactToReport (const Report& r, ReportPtr previous);
    bool chatty (int minLevel) const;

    MooniProcessor& proc;
    MooniView& mooni;
    SayFn sayFn;
    juce::Random rng;

    std::map<juce::String, juce::StringArray> lines;
    std::map<juce::String, int> lastPick;
    std::deque<std::pair<juce::String, juce::String>> pending;

    int seenSessions = 0, seenReady = 0, seenReports = 0, seenGestures = 0, seenRefChanges = 0, seenOvers = 0, seenOnsets = 0;
    int lastReactedSession = -1, lastReactedScore = -1;
    bool wasBypassed = false, wasListening = false;
    float idleSeconds = 0, silenceWhileListening = 0, overCooldown = 0, gestureCooldown = 0, pokeWindow = 0;
    int pokes = 0;
    bool idleQuipDone = false, silenceQuipDone = false;
    int room = 0;
    float sinceStart = 0;
};
} // namespace mooni::ui
