#include "Report.h"

namespace mooni
{
const std::vector<GenreProfile>& genreProfiles()
{
    // Heuristic context values. Band levels are 1/6-octave power relative to the 500 Hz-2 kHz mean.
    static const std::vector<GenreProfile> profiles {
        //  key        name                                       LUFS  lowSh sub  slope  tol  minPLR minMicro maxGR
        { "auto",   juce::String::fromUTF8 ("Авто"),              -9.0f, 4.5f, 0.0f, -3.0f, 3.0f, 6.0f, 6.0f, 3.5f },
        { "trap",   juce::String::fromUTF8 ("Трэп / хип-хоп"),    -7.0f, 7.0f, 3.0f, -3.0f, 3.0f, 5.5f, 5.0f, 4.0f },
        { "heavy",  juce::String::fromUTF8 ("Жёсткое / агрессивное"), -5.5f, 6.0f, 2.0f, -2.6f, 3.5f, 4.0f, 3.5f, 6.0f },
        { "edm",    juce::String::fromUTF8 ("EDM / хаус"),        -7.5f, 6.0f, 1.0f, -2.8f, 3.0f, 6.0f, 5.5f, 4.0f },
        { "pop",    juce::String::fromUTF8 ("Поп"),               -9.0f, 4.0f, 0.0f, -3.0f, 2.5f, 7.0f, 7.0f, 3.0f },
        { "rock",   juce::String::fromUTF8 ("Рок / альтернатива"), -9.0f, 3.0f, -1.0f, -2.8f, 3.0f, 7.0f, 7.0f, 3.0f },
        { "acoustic", juce::String::fromUTF8 ("Акустика / джаз / лоу-фай"), -13.0f, 2.0f, -2.0f, -3.5f, 3.5f, 10.0f, 9.0f, 2.0f },
    };
    return profiles;
}

int genreIndexForKey (const juce::String& key)
{
    const auto& g = genreProfiles();
    for (size_t i = 0; i < g.size(); ++i) if (key == g[i].key) return (int) i;
    return 0;
}

// ---------------------------------------------------------------------------
template <typename Container>
static juce::String arrayToString (const Container& c)
{
    juce::StringArray s;
    for (auto v : c) s.add (juce::String (v, 2));
    return s.joinIntoString (",");
}

template <size_t N>
static void stringToArray (const juce::String& str, std::array<float, N>& out)
{
    auto parts = juce::StringArray::fromTokens (str, ",", "");
    for (size_t i = 0; i < N && (int) i < parts.size(); ++i) out[i] = parts[(int) i].getFloatValue();
}

static std::vector<float> stringToVector (const juce::String& str)
{
    std::vector<float> v;
    for (auto& p : juce::StringArray::fromTokens (str, ",", "")) v.push_back (p.getFloatValue());
    return v;
}

juce::ValueTree Report::toValueTree() const
{
    juce::ValueTree t ("Report");
    t.setProperty ("valid", valid, nullptr);
    t.setProperty ("time", timeMs, nullptr);
    t.setProperty ("label", label, nullptr);
    t.setProperty ("genre", genreProfile, nullptr);
    t.setProperty ("genreGuess", genreGuess, nullptr);
    t.setProperty ("genreConf", genreConfidence, nullptr);
    t.setProperty ("score", score, nullptr);
    t.setProperty ("verdict", verdict, nullptr);
    t.setProperty ("target", recTargetLufs, nullptr);
    t.setProperty ("targetReason", targetReason, nullptr);
    t.setProperty ("recWidth", arrayToString (recWidthPct), nullptr);
    t.setProperty ("recBalance", recBalanceDb, nullptr);
    t.setProperty ("targetSixth", arrayToString (targetSixth), nullptr);
    t.setProperty ("usedRef", usedReference, nullptr);
    t.setProperty ("fingerprint", paramFingerprint, nullptr);
    t.setProperty ("fromRef", fromReferenceFile, nullptr);
    t.setProperty ("session", session, nullptr);
    t.setProperty ("inI", inIntegrated, nullptr);
    t.setProperty ("inTP", inTruePeak, nullptr);
    t.setProperty ("inSixth", arrayToString (inSixth), nullptr);

    juce::ValueTree s ("Stats");
    const auto& m = stats;
    s.setProperty ("sr", m.sampleRate, nullptr);
    s.setProperty ("secs", m.secondsAnalysed, nullptr);
    s.setProperty ("secsTotal", m.secondsTotal, nullptr);
    s.setProperty ("I", m.integrated, nullptr);
    s.setProperty ("LRA", m.lra, nullptr);
    s.setProperty ("maxM", m.maxMomentary, nullptr);
    s.setProperty ("maxS", m.maxShortTerm, nullptr);
    s.setProperty ("TP", m.truePeakDb, nullptr);
    s.setProperty ("SP", m.samplePeakDb, nullptr);
    s.setProperty ("rms", m.rmsDb, nullptr);
    s.setProperty ("crest", m.crestDb, nullptr);
    s.setProperty ("plr", m.plr, nullptr);
    s.setProperty ("micro", m.microDynamicsDb, nullptr);
    s.setProperty ("punch", m.punchDb, nullptr);
    s.setProperty ("clips", m.clipEvents, nullptr);
    s.setProperty ("flat", m.flatTopRuns, nullptr);
    s.setProperty ("overs", m.truePeakOvers, nullptr);
    s.setProperty ("clipsMin", m.clipEventsPerMinute, nullptr);
    s.setProperty ("dc", m.dcOffsetDb, nullptr);
    s.setProperty ("onsets", m.onsets, nullptr);
    s.setProperty ("sixth", arrayToString (m.sixth), nullptr);
    s.setProperty ("fine", arrayToString (m.fine), nullptr);
    s.setProperty ("side", arrayToString (m.sideSixth), nullptr);
    s.setProperty ("infra", m.infrasonicRatioDb, nullptr);
    s.setProperty ("corr", m.correlation, nullptr);
    s.setProperty ("width", m.widthPct, nullptr);
    s.setProperty ("bal", m.balanceDb, nullptr);
    s.setProperty ("mono", m.monoLossDb, nullptr);
    s.setProperty ("negCorr", m.negativeCorrFraction, nullptr);
    s.setProperty ("worstCorr", m.worstCorrelation, nullptr);
    s.setProperty ("xo", arrayToString (m.xovers), nullptr);
    s.setProperty ("bw", arrayToString (m.bandWidthPct), nullptr);
    s.setProperty ("bc", arrayToString (m.bandCorr), nullptr);
    s.setProperty ("bm", arrayToString (m.bandMonoLossDb), nullptr);
    s.setProperty ("lowShare", m.lowEndShare, nullptr);
    s.setProperty ("phone", m.phoneLossDb, nullptr);
    {
        // keep at most ~10 minutes of history at 1 Hz resolution in saved state
        std::vector<float> h;
        const size_t step = std::max<size_t> (1, m.shortTermHistory.size() / 600);
        for (size_t i = 0; i < m.shortTermHistory.size(); i += step) h.push_back (m.shortTermHistory[i]);
        s.setProperty ("hist", arrayToString (h), nullptr);
    }
    t.appendChild (s, nullptr);

    juce::ValueTree subs ("Subscores");
    for (auto& sc : subscores)
    {
        juce::ValueTree c ("S");
        c.setProperty ("n", sc.name, nullptr); c.setProperty ("v", sc.value, nullptr);
        c.setProperty ("w", sc.weight, nullptr); c.setProperty ("note", sc.note, nullptr);
        subs.appendChild (c, nullptr);
    }
    t.appendChild (subs, nullptr);

    juce::ValueTree probs ("Problems");
    for (auto& p : problems)
    {
        juce::ValueTree c ("P");
        c.setProperty ("id", p.id, nullptr); c.setProperty ("cat", p.category, nullptr);
        c.setProperty ("sev", p.severity, nullptr); c.setProperty ("title", p.title, nullptr);
        c.setProperty ("what", p.what, nullptr); c.setProperty ("why", p.why, nullptr);
        c.setProperty ("fix", p.fix, nullptr); c.setProperty ("mod", (int) p.module, nullptr);
        c.setProperty ("lo", p.freqLo, nullptr); c.setProperty ("hi", p.freqHi, nullptr);
        c.setProperty ("meas", p.measured, nullptr);
        probs.appendChild (c, nullptr);
    }
    t.appendChild (probs, nullptr);

    juce::ValueTree eqs ("Eq");
    for (auto& e : eq)
    {
        juce::ValueTree c ("B");
        c.setProperty ("t", e.type, nullptr); c.setProperty ("f", e.freq, nullptr);
        c.setProperty ("g", e.gainDb, nullptr); c.setProperty ("q", e.q, nullptr);
        c.setProperty ("ch", e.channel, nullptr); c.setProperty ("why", e.reason, nullptr);
        eqs.appendChild (c, nullptr);
    }
    t.appendChild (eqs, nullptr);
    return t;
}

Report Report::fromValueTree (const juce::ValueTree& t)
{
    Report r;
    if (! t.hasType ("Report")) return r;
    r.valid = t["valid"];
    r.timeMs = (juce::int64) t["time"];
    r.label = t["label"].toString();
    r.genreProfile = t["genre"];
    r.genreGuess = t["genreGuess"].toString();
    r.genreConfidence = t["genreConf"];
    r.score = t["score"];
    r.verdict = t["verdict"].toString();
    r.recTargetLufs = t["target"];
    r.targetReason = t["targetReason"].toString();
    stringToArray (t["recWidth"].toString(), r.recWidthPct);
    r.recBalanceDb = t["recBalance"];
    stringToArray (t["targetSixth"].toString(), r.targetSixth);
    r.usedReference = t["usedRef"];
    r.paramFingerprint = t["fingerprint"].toString();
    r.fromReferenceFile = t["fromRef"];
    r.session = t["session"];
    r.inIntegrated = t.getProperty ("inI", -100.0f);
    r.inTruePeak = t.getProperty ("inTP", -100.0f);
    stringToArray (t["inSixth"].toString(), r.inSixth);

    auto s = t.getChildWithName ("Stats");
    auto& m = r.stats;
    m.sampleRate = s["sr"]; m.secondsAnalysed = s["secs"]; m.secondsTotal = s["secsTotal"];
    m.integrated = s["I"]; m.lra = s["LRA"]; m.maxMomentary = s["maxM"]; m.maxShortTerm = s["maxS"];
    m.truePeakDb = s["TP"]; m.samplePeakDb = s["SP"]; m.rmsDb = s["rms"]; m.crestDb = s["crest"];
    m.plr = s["plr"]; m.microDynamicsDb = s["micro"]; m.punchDb = s["punch"];
    m.clipEvents = s["clips"]; m.flatTopRuns = s["flat"]; m.truePeakOvers = s["overs"];
    m.clipEventsPerMinute = s["clipsMin"]; m.dcOffsetDb = s["dc"]; m.onsets = s["onsets"];
    stringToArray (s["sixth"].toString(), m.sixth);
    stringToArray (s["fine"].toString(), m.fine);
    stringToArray (s["side"].toString(), m.sideSixth);
    m.infrasonicRatioDb = s["infra"];
    m.correlation = s["corr"]; m.widthPct = s["width"]; m.balanceDb = s["bal"]; m.monoLossDb = s["mono"];
    m.negativeCorrFraction = s["negCorr"]; m.worstCorrelation = s["worstCorr"];
    stringToArray (s["xo"].toString(), m.xovers);
    stringToArray (s["bw"].toString(), m.bandWidthPct);
    stringToArray (s["bc"].toString(), m.bandCorr);
    stringToArray (s["bm"].toString(), m.bandMonoLossDb);
    m.lowEndShare = s["lowShare"]; m.phoneLossDb = s["phone"];
    m.shortTermHistory = stringToVector (s["hist"].toString());

    for (auto c : t.getChildWithName ("Subscores"))
        r.subscores.push_back ({ c["n"].toString(), (int) c["v"], (float) c["w"], c["note"].toString() });

    for (auto c : t.getChildWithName ("Problems"))
    {
        Problem p;
        p.id = c["id"].toString(); p.category = c["cat"].toString(); p.severity = c["sev"];
        p.title = c["title"].toString(); p.what = c["what"].toString(); p.why = c["why"].toString();
        p.fix = c["fix"].toString(); p.module = (Module) (int) c["mod"];
        p.freqLo = c["lo"]; p.freqHi = c["hi"]; p.measured = c["meas"];
        r.problems.push_back (p);
    }
    for (auto c : t.getChildWithName ("Eq"))
    {
        EqSuggestion e;
        e.type = c["t"]; e.freq = c["f"]; e.gainDb = c["g"]; e.q = c["q"]; e.channel = c["ch"];
        e.reason = c["why"].toString();
        r.eq.push_back (e);
    }
    return r;
}
} // namespace mooni
