#pragma once
#include "Theme.h"
#include "../core/PluginProcessor.h"
#include <deque>

namespace mooni::ui
{
/** Shared state passed to every room. */
struct UiContext
{
    explicit UiContext (MooniProcessor& p) : proc (p) {}
    MooniProcessor& proc;
    LiveData live;
    ReportPtr report;                 // latest valid report (engine or logbook)
    float time = 0;
    bool reducedMotion = false;
    std::function<void (int room)> goRoom;
    std::function<void (const juce::String& id, const juce::String& detail)> action;   // → Mooni's brain
    std::function<void (const juce::String& text, juce::Colour accent)> toast;
    std::function<void (float lo, float hi)> listenBand;                               // band monitor toggle
    bool isStale() const { return report && report->paramFingerprint.isNotEmpty() && report->paramFingerprint != proc.fingerprint(); }
};

int roomForModule (Module m);

// ---------------------------------------------------------------------------
class ScoreDial : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setReport (ReportPtr r, bool stale);
    void tick (float dt);
    void paint (juce::Graphics&) override;
private:
    ReportPtr report; bool stale = false;
    float shown = 0, target = 0, pulse = 0;
};

class SubscoreBars : public juce::Component
{
public:
    void setReport (ReportPtr r) { report = std::move (r); repaint(); }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
private:
    ReportPtr report; int hover = -1;
};

// ---------------------------------------------------------------------------
class ProblemCard : public juce::Component
{
public:
    ProblemCard (UiContext&, const Problem&, int index);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    int preferredHeight (int width) const;
    std::function<void()> onToggle;
    bool expanded = false;
private:
    UiContext& ctx;
    Problem problem;
    int index;
    juce::TextButton openBtn, listenBtn, fixBtn;
};

class ProblemList : public juce::Component
{
public:
    explicit ProblemList (UiContext&);
    void setReport (ReportPtr r);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void layoutCards();
    UiContext& ctx;
    juce::Viewport view;
    juce::Component content;
    std::vector<std::unique_ptr<ProblemCard>> cards;
    ReportPtr report;
};

// ---------------------------------------------------------------------------
struct Metric { juce::String label, value, unit, tip; juce::Colour colour; };
class MetricGrid : public juce::Component, public juce::TooltipClient
{
public:
    void setMetrics (std::vector<Metric> m, int columns) { metrics = std::move (m); cols = columns; repaint(); }
    void paint (juce::Graphics&) override;
    juce::String getTooltip() override;
private:
    std::vector<Metric> metrics; int cols = 3;
};

class VMeter : public juce::Component
{
public:
    VMeter (juce::String label, float lo, float hi) : name (std::move (label)), minV (lo), maxV (hi) {}
    void setValue (float v, float hold, float targetMark = -1000.0f) { value = v; holdV = hold; target = targetMark; repaint(); }
    void setInverted (bool b) { inverted = b; }    // gain reduction meters grow downward
    void setColours (juce::Colour lo, juce::Colour hi) { cLo = lo; cHi = hi; }
    void paint (juce::Graphics&) override;
private:
    juce::String name; float minV, maxV, value = -100, holdV = -100, target = -1000;
    bool inverted = false;
    juce::Colour cLo = col::mint, cHi = col::coral;
};

class MoonPhaseMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setCorrelation (float c) { if (std::abs (c - corr) > 0.005f) { corr = c; repaint(); } }
    void setShowText (bool b) { showText = b; }
    void paint (juce::Graphics&) override;
private:
    float corr = 1; bool showText = true;
};

class TideMirror : public juce::Component   // vectorscope (M up, S sideways)
{
public:
    void setData (const std::array<float, LiveData::kScopePoints * 2>& d, float corr) { data = d; correlation = corr; repaint(); }
    void paint (juce::Graphics&) override;
private:
    std::array<float, LiveData::kScopePoints * 2> data {};
    float correlation = 1, gain = 1;
};

class LoudnessTimeline : public juce::Component
{
public:
    void setData (const std::vector<float>& out, const std::vector<float>& in, float targetLufs) { histOut = out; histIn = in; target = targetLufs; repaint(); }
    void paint (juce::Graphics&) override;
private:
    std::vector<float> histOut, histIn; float target = -9;
};

// ---------------------------------------------------------------------------
class SpeechBubble : public juce::Component
{
public:
    void say (const juce::String& text, float seconds);
    void setAnchor (juce::Point<float> mooniTop, juce::Rectangle<float> limits, bool preferLeft);
    void tick (float dt);
    void paint (juce::Graphics&) override;
    bool hitTest (int, int) override { return alpha > 0.05f; }
    void mouseUp (const juce::MouseEvent&) override { remaining = std::min (remaining, 0.3f); }
private:
    void relayout();
    juce::String text; float remaining = 0, typed = 0, alpha = 0;
    juce::Point<float> anchor; juce::Rectangle<float> limits; bool left = false;
    juce::TextLayout layout; juce::Rectangle<float> bubble;
};

class BipToast : public juce::Component
{
public:
    void push (const juce::String& text, juce::Colour accent);
    void tick (float dt);
    void paint (juce::Graphics&) override;
    bool hitTest (int, int) override { return ! items.empty(); }
    void mouseUp (const juce::MouseEvent&) override { if (! items.empty()) items.front().t = std::max (items.front().t, items.front().dur - 0.35f); }
private:
    struct Item { juce::String text; juce::Colour accent; float t = 0, dur = 4.5f; };
    std::deque<Item> items;
};
} // namespace mooni::ui
