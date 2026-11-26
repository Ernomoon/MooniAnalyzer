#pragma once
#include "../analysis/Report.h"
#include <juce_data_structures/juce_data_structures.h>
#include <mutex>

namespace mooni
{
/** The logbook: every analysis report with the processing settings it was made with.
    Lives on the message thread (reports arrive via AsyncUpdater). */
class History
{
public:
    struct Entry
    {
        ReportPtr report;
        juce::ValueTree params;    // APVTS state at the moment the report was finalised
        bool isOriginal = false;   // first analysis of this project — never dropped
    };

    static constexpr int kMaxEntries = 40;

    /** Adds a report; a refresh of the same session replaces its previous entry. */
    void add (ReportPtr r, const juce::ValueTree& params);
    std::vector<Entry> entries() const;
    int size() const;
    const Entry* original() const;     // not thread-safe pointer: message thread only
    ReportPtr previousReport (int session) const;   // latest report from a different session
    void clear();
    int changeCounter() const noexcept { return changes; }

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

private:
    mutable std::mutex lock;
    std::vector<Entry> list;
    int changes = 0;
};

/** Snapshot-based undo/redo of all processing parameters. */
class ParamUndo
{
public:
    void push (const juce::ValueTree& state);       // after a change is complete
    bool canUndo() const { return index > 0; }
    bool canRedo() const { return index + 1 < (int) states.size(); }
    juce::ValueTree undo() { return canUndo() ? states[(size_t) --index].createCopy() : juce::ValueTree(); }
    juce::ValueTree redo() { return canRedo() ? states[(size_t) ++index].createCopy() : juce::ValueTree(); }
    void reset (const juce::ValueTree& state) { states.clear(); states.push_back (state.createCopy()); index = 0; }
    int position() const { return index; }
    int count() const { return (int) states.size(); }

private:
    std::vector<juce::ValueTree> states;
    int index = -1;
};
} // namespace mooni
