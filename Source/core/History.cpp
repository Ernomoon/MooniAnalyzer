#include "History.h"
#include "Text.h"

namespace mooni
{
void History::add (ReportPtr r, const juce::ValueTree& params)
{
    if (r == nullptr || ! r->valid) return;
    std::lock_guard<std::mutex> g (lock);
    for (auto& e : list)
        if (e.report && r->session > 0 && e.report->session == r->session)
        {
            // refreshed report of the same listening session
            auto copy = std::make_shared<Report> (*r);
            copy->label = e.report->label;
            e.report = copy;
            e.params = params.createCopy();
            ++changes;
            return;
        }
    Entry e;
    auto copy = std::make_shared<Report> (*r);
    const bool first = list.empty();
    copy->label = first ? ru ("Оригинал") : fmt ("Прослушивание {}", (int) list.size() + 1);
    e.report = copy;
    e.params = params.createCopy();
    e.isOriginal = first;
    list.push_back (e);
    while ((int) list.size() > kMaxEntries)
    {
        auto it = std::find_if (list.begin(), list.end(), [] (const Entry& x) { return ! x.isOriginal; });
        if (it == list.end()) break;
        list.erase (it);
    }
    ++changes;
}

std::vector<History::Entry> History::entries() const { std::lock_guard<std::mutex> g (lock); return list; }
int History::size() const { std::lock_guard<std::mutex> g (lock); return (int) list.size(); }

const History::Entry* History::original() const
{
    std::lock_guard<std::mutex> g (lock);
    for (auto& e : list) if (e.isOriginal) return &e;
    return nullptr;
}

ReportPtr History::previousReport (int session) const
{
    std::lock_guard<std::mutex> g (lock);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (it->report && it->report->session != session) return it->report;
    return nullptr;
}

void History::clear() { std::lock_guard<std::mutex> g (lock); list.clear(); ++changes; }

juce::ValueTree History::toValueTree() const
{
    std::lock_guard<std::mutex> g (lock);
    juce::ValueTree t ("History");
    for (auto& e : list)
    {
        juce::ValueTree c ("Entry");
        c.setProperty ("original", e.isOriginal, nullptr);
        c.appendChild (e.report->toValueTree(), nullptr);
        if (e.params.isValid())
        {
            juce::ValueTree p ("Params");
            p.appendChild (e.params.createCopy(), nullptr);
            c.appendChild (p, nullptr);
        }
        t.appendChild (c, nullptr);
    }
    return t;
}

void History::fromValueTree (const juce::ValueTree& t)
{
    std::lock_guard<std::mutex> g (lock);
    list.clear();
    for (auto c : t)
    {
        Entry e;
        e.isOriginal = c["original"];
        auto rep = std::make_shared<Report> (Report::fromValueTree (c.getChildWithName ("Report")));
        rep->session = -1 - (int) list.size();     // restored sessions never merge with new ones
        e.report = rep;
        auto p = c.getChildWithName ("Params");
        if (p.isValid() && p.getNumChildren() > 0) e.params = p.getChild (0).createCopy();
        if (e.report->valid) list.push_back (e);
    }
    ++changes;
}

void ParamUndo::push (const juce::ValueTree& state)
{
    if (index >= 0 && states[(size_t) index].isEquivalentTo (state)) return;
    states.resize ((size_t) (index + 1));
    states.push_back (state.createCopy());
    if (states.size() > 80) states.erase (states.begin());
    index = (int) states.size() - 1;
}
} // namespace mooni
