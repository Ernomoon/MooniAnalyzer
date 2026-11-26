#include "PluginProcessor.h"
#include "Text.h"
#include "../gui/PluginEditor.h"

namespace mooni
{
using namespace params;

static std::atomic<int> genreAtomic { 0 };

MooniProcessor::MooniProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pBypass = apvts.getRawParameterValue (bypass);
    pEqOn = apvts.getRawParameterValue (eqOn);
    for (int i = 0; i < kEqBands; ++i)
    {
        pBandOn[(size_t) i] = apvts.getRawParameterValue (eqBandOn (i));
        pBandType[(size_t) i] = apvts.getRawParameterValue (eqBandType (i));
        pBandFreq[(size_t) i] = apvts.getRawParameterValue (eqBandFreq (i));
        pBandGain[(size_t) i] = apvts.getRawParameterValue (eqBandGain (i));
        pBandQ[(size_t) i] = apvts.getRawParameterValue (eqBandQ (i));
        pBandCh[(size_t) i] = apvts.getRawParameterValue (eqBandCh (i));
    }
    pImgOn = apvts.getRawParameterValue (imgOn);
    pImgBal = apvts.getRawParameterValue (imgBal);
    for (int i = 0; i < 3; ++i) pXo[(size_t) i] = apvts.getRawParameterValue (imgXover (i));
    for (int i = 0; i < kImgBands; ++i) pW[(size_t) i] = apvts.getRawParameterValue (imgWidth (i));
    pMaxOn = apvts.getRawParameterValue (maxOn);
    pDrive = apvts.getRawParameterValue (maxDrive);
    pCeil = apvts.getRawParameterValue (maxCeil);
    pRel = apvts.getRawParameterValue (maxRel);
    pClip = apvts.getRawParameterValue (maxClip);

    engine.setContextProvider ([this] { return makeContext(); });
    engine.setReportCallback ([this] (ReportPtr r, bool final)
    {
        {
            std::lock_guard<std::mutex> g (pendingLock);
            pendingReports.emplace_back (r, final);
        }
        triggerAsyncUpdate();
    });

    addListener (this);
    undo.reset (apvts.copyState());
    abStates[0] = apvts.copyState();
    setSettings (settings);
}

MooniProcessor::~MooniProcessor()
{
    removeListener (this);
    cancelPendingUpdate();
}

bool MooniProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void MooniProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    readParams();
    eq.prepare (sampleRate, kInternalBlock);
    imager.prepare (sampleRate, kInternalBlock);
    maximizer.prepare (sampleRate, kInternalBlock);
    latencySamples = maximizer.getLatencySamples();
    setLatencySamples (latencySamples.load());

    inTap.setSize (2, kInternalBlock);
    preTap.setSize (2, kInternalBlock);
    work.setSize (2, kInternalBlock);
    refBuf.setSize (2, kInternalBlock);

    engine.setCrossovers (pXo[0]->load(), pXo[1]->load(), pXo[2]->load());
    engine.prepare (sampleRate, samplesPerBlock);

    listenMix.setLength (0.03, sampleRate); listenMix.reset (0.0f);
    refMix.setLength (0.03, sampleRate);    refMix.reset (0.0f);
    fairGain.setTime (0.03, sampleRate);    fairGain.reset (1.0);
    for (auto& a : lhp) for (auto& b : a) b.reset();
    for (auto& a : llp) for (auto& b : a) b.reset();
    listenDesigned[0] = listenDesigned[1] = 0;
    reference.setHostRate (sampleRate);
}

void MooniProcessor::releaseResources() { engine.releaseResources(); }

void MooniProcessor::readParams()
{
    const bool byp = pBypass->load() > 0.5f;
    eq.setEnabled (pEqOn->load() > 0.5f && ! byp);
    for (int i = 0; i < kEqBands; ++i)
    {
        dsp::EqBandParams b;
        b.enabled = pBandOn[(size_t) i]->load() > 0.5f;
        b.type = (int) pBandType[(size_t) i]->load();
        b.freq = pBandFreq[(size_t) i]->load();
        b.gainDb = pBandGain[(size_t) i]->load();
        b.q = pBandQ[(size_t) i]->load();
        b.channel = (int) pBandCh[(size_t) i]->load();
        eq.setBand (i, b);
    }
    imager.setEnabled (pImgOn->load() > 0.5f && ! byp);
    imager.setCrossovers (pXo[0]->load(), pXo[1]->load(), pXo[2]->load());
    imager.setWidths ({ pW[0]->load(), pW[1]->load(), pW[2]->load(), pW[3]->load() });
    imager.setBalanceDb (pImgBal->load());

    dsp::MaximizerParams mp;
    mp.enabled = pMaxOn->load() > 0.5f && ! byp;
    mp.driveDb = pDrive->load();
    mp.ceilingDb = pCeil->load();
    mp.releaseMs = pRel->load();
    mp.clipPct = pClip->load();
    maximizer.setParams (mp);
}

void MooniProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (numOut == 0 || n == 0) return;

    bool playing = false, hasTransport = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hasTransport = true;
            playing = pos->getIsPlaying();
        }

    readParams();
    engine.setCrossovers (pXo[0]->load(), pXo[1]->load(), pXo[2]->load());
    const bool byp = pBypass->load() > 0.5f;
    engine.freezeMatchFlag = byp;

    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const int mon = monitor.load();
    const bool fair = fairAtomic.load() && byp;
    fairGain.target = fair ? std::pow (10.0, juce::jlimit (-24.0f, 18.0f, -engine.matchGainDb.load()) / 20.0) : 1.0;
    appliedFairGainDb = fair ? -engine.matchGainDb.load() : 0.0f;

    // band-listen filters (designed at block rate)
    const bool listen = bandListen.load();
    listenMix.target = listen ? 1.0f : 0.0f;
    const float lo = bandListenLo.load(), hi = bandListenHi.load();
    if (listen && (lo != listenDesigned[0] || hi != listenDesigned[1]))
    {
        const double sr = currentRate.load();
        listenHp = dsp::BiquadCoeffs::make (dsp::FilterType::LowCut, sr, std::max (15.0f, lo), 0.7071, 0);
        listenLp = dsp::BiquadCoeffs::make (dsp::FilterType::HighCut, sr, std::min ((float) sr * 0.45f, hi), 0.7071, 0);
        listenDesigned[0] = lo; listenDesigned[1] = hi;
    }
    const bool refOn = referenceListen.load() && reference.getStatus() == ReferenceTrack::Status::Ready;
    refMix.target = refOn ? 1.0f : 0.0f;
    const float refGain = std::pow (10.0f, referenceGainDb.load() / 20.0f);

    float grMin = 0.0f, clipMin = 0.0f, outPk = 0.0f, inPk = 0.0f;
    for (int off = 0; off < n; off += kInternalBlock)
    {
        const int k = std::min (kInternalBlock, n - off);
        float* l = L + off;
        float* r = R != nullptr ? R + off : work.getWritePointer (1);
        if (R == nullptr) std::copy (l, l + k, r);

        float* il = inTap.getWritePointer (0); float* ir = inTap.getWritePointer (1);
        std::copy (l, l + k, il); std::copy (r, r + k, ir);
        for (int i = 0; i < k; ++i) inPk = std::max (inPk, std::max (std::abs (il[i]), std::abs (ir[i])));

        eq.process (l, r, k);
        imager.process (l, r, k);
        float* pl = preTap.getWritePointer (0); float* pr = preTap.getWritePointer (1);
        std::copy (l, l + k, pl); std::copy (r, r + k, pr);
        maximizer.process (l, r, k);
        grMin = std::min (grMin, maximizer.getBlockGainReductionDb());
        clipMin = std::min (clipMin, maximizer.getBlockClipReductionDb());

        engine.pushAudio (il, ir, pl, pr, l, r, k, playing, hasTransport);

        // ---- monitoring (after the analysis taps) ----
        if (fair || fairGain.isSmoothing (1e-5))
            for (int i = 0; i < k; ++i) { const float g = (float) fairGain.next(); l[i] *= g; r[i] *= g; }

        if (mon == (int) MonitorMode::Mono)
            for (int i = 0; i < k; ++i) { const float m = 0.5f * (l[i] + r[i]); l[i] = r[i] = m; }
        else if (mon == (int) MonitorMode::Side)
            for (int i = 0; i < k; ++i) { const float s = 0.5f * (l[i] - r[i]); l[i] = r[i] = s; }

        if (listen || listenMix.isActive())
        {
            for (int i = 0; i < k; ++i)
            {
                float a = l[i], b = r[i];
                a = llp[0][1].process (listenLp, llp[0][0].process (listenLp, lhp[0][1].process (listenHp, lhp[0][0].process (listenHp, a))));
                b = llp[1][1].process (listenLp, llp[1][0].process (listenLp, lhp[1][1].process (listenHp, lhp[1][0].process (listenHp, b))));
                const float g = listenMix.next();
                l[i] += g * (a - l[i]); r[i] += g * (b - r[i]);
            }
        }
        else { for (auto& x : lhp) for (auto& y : x) y.reset(); for (auto& x : llp) for (auto& y : x) y.reset(); }

        if (refOn || refMix.isActive())
        {
            float* rl = refBuf.getWritePointer (0); float* rr = refBuf.getWritePointer (1);
            const bool rendered = (playing || ! hasTransport) && reference.render (rl, rr, k, refGain);
            if (! rendered) { std::fill (rl, rl + k, 0.0f); std::fill (rr, rr + k, 0.0f); }
            for (int i = 0; i < k; ++i)
            {
                const float g = refMix.next();
                l[i] += g * (rl[i] - l[i]); r[i] += g * (rr[i] - r[i]);
            }
        }

        for (int i = 0; i < k; ++i) outPk = std::max (outPk, std::max (std::abs (l[i]), std::abs (r[i])));
        if (R == nullptr) for (int i = 0; i < k; ++i) l[i] = 0.5f * (l[i] + r[i]);
    }

    meterGrDb = grMin;
    meterClipDb = clipMin;
    meterOutPeak = outPk > 0 ? 20.0f * std::log10 (outPk) : -100.0f;
    meterInPeak = inPk > 0 ? 20.0f * std::log10 (inPk) : -100.0f;
}

// ---------------------------------------------------------------------------
DiagnosticsContext MooniProcessor::makeContext() const
{
    DiagnosticsContext c;
    c.genreChoice = genreAtomic.load();
    c.imagerOn = pImgOn->load() > 0.5f && pBypass->load() < 0.5f;
    for (int i = 0; i < 4; ++i) c.currentWidthParams[(size_t) i] = pW[(size_t) i]->load();
    c.currentBalanceParam = pImgBal->load();
    c.fingerprint = fingerprint();
    static thread_local std::shared_ptr<const MixStats> refHold;
    refHold = useRefAtomic.load() && reference.getStatus() == ReferenceTrack::Status::Ready ? reference.getStats() : nullptr;
    c.reference = refHold.get();
    return c;
}

juce::String MooniProcessor::fingerprint() const
{
    juce::String s;
    // bypass is deliberately not part of the fingerprint: it does not change the processing settings
    s << (pEqOn->load() > 0.5f ? "E" : "-");
    for (int i = 0; i < kEqBands; ++i)
        if (pBandOn[(size_t) i]->load() > 0.5f)
            s << i << ":" << (int) pBandType[(size_t) i]->load() << "," << juce::roundToInt (pBandFreq[(size_t) i]->load())
              << "," << juce::String (pBandGain[(size_t) i]->load(), 1) << "," << juce::String (pBandQ[(size_t) i]->load(), 2)
              << "," << (int) pBandCh[(size_t) i]->load() << ";";
    s << (pImgOn->load() > 0.5f ? "I" : "-");
    if (pImgOn->load() > 0.5f)
        for (int i = 0; i < 4; ++i) s << juce::roundToInt (pW[(size_t) i]->load()) << ",";
    s << (pMaxOn->load() > 0.5f ? "M" : "-");
    if (pMaxOn->load() > 0.5f) s << juce::String (pDrive->load(), 1) << "," << juce::String (pCeil->load(), 1) << "," << juce::roundToInt (pClip->load());
    return s;
}

std::array<dsp::EqBandParams, dsp::ParametricEQ::kNumBands> MooniProcessor::currentEqBands() const
{
    std::array<dsp::EqBandParams, dsp::ParametricEQ::kNumBands> a {};
    for (int i = 0; i < kEqBands; ++i)
    {
        auto& b = a[(size_t) i];
        b.enabled = pBandOn[(size_t) i]->load() > 0.5f;
        b.type = (int) pBandType[(size_t) i]->load();
        b.freq = pBandFreq[(size_t) i]->load();
        b.gainDb = pBandGain[(size_t) i]->load();
        b.q = pBandQ[(size_t) i]->load();
        b.channel = (int) pBandCh[(size_t) i]->load();
    }
    return a;
}

void MooniProcessor::setSettings (const Settings& s)
{
    settings = s;
    genreAtomic = s.genreChoice;
    useRefAtomic = s.useReferenceAsTarget;
    fairAtomic = s.fairCompare;
    engine.setMode (s.mode);
    engine.setAutoListen (s.autoListen);
}

void MooniProcessor::setValue (const juce::String& id, float v)
{
    if (auto* p = param (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

// ---------------------------------------------------------------------------
void MooniProcessor::audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int parameterIndex)
{
    lastGestureIndex = parameterIndex;
    if (suppressSnapshots.load()) return;
    gestureSnapshotPending = true;
    triggerAsyncUpdate();
}

void MooniProcessor::handleAsyncUpdate()
{
    std::vector<std::pair<ReportPtr, bool>> reps;
    { std::lock_guard<std::mutex> g (pendingLock); reps.swap (pendingReports); }
    for (auto& [r, final] : reps)
    {
        juce::ignoreUnused (final);
        history.add (r, apvts.copyState());
    }
    if (gestureSnapshotPending.exchange (false))
    {
        pushUndoSnapshot();
        const auto& ps = getParameters();
        const int idx = lastGestureIndex.load();
        if (idx >= 0 && idx < ps.size())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[idx]))
                lastGestureParam = rp->getParameterID();
        ++paramGestureCount;
    }
}

void MooniProcessor::pushUndoSnapshot() { undo.push (apvts.copyState()); }

bool MooniProcessor::undoStep()
{
    auto st = undo.undo();
    if (! st.isValid()) return false;
    suppressSnapshots = true;
    apvts.replaceState (st);
    suppressSnapshots = false;
    return true;
}

bool MooniProcessor::redoStep()
{
    auto st = undo.redo();
    if (! st.isValid()) return false;
    suppressSnapshots = true;
    apvts.replaceState (st);
    suppressSnapshots = false;
    return true;
}

void MooniProcessor::restoreParams (const juce::ValueTree& state)
{
    if (! state.isValid()) return;
    suppressSnapshots = true;
    apvts.replaceState (state.createCopy());
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::applyEqRecommendations (const Report& r)
{
    if (r.eq.empty()) return;
    suppressSnapshots = true;
    auto bands = currentEqBands();
    std::array<bool, kEqBands> touched {};

    auto setBand = [&] (int i, const dsp::EqBandParams& b)
    {
        setValue (eqBandType (i), (float) b.type);
        setValue (eqBandFreq (i), b.freq);
        setValue (eqBandGain (i), b.gainDb);
        setValue (eqBandQ (i), b.q);
        setValue (eqBandCh (i), (float) b.channel);
        setValue (eqBandOn (i), 1.0f);
        bands[(size_t) i] = b; bands[(size_t) i].enabled = true;
        touched[(size_t) i] = true;
    };

    for (const auto& s : r.eq)
    {
        // 1) merge into a matching enabled band (same type/channel, within ~1/3 octave)
        int match = -1;
        for (int i = 0; i < kEqBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            if (b.enabled && b.type == s.type && b.channel == s.channel && std::abs (std::log2 (b.freq / s.freq)) < 0.35f
                && std::max (b.q, s.q) / std::max (0.05f, std::min (b.q, s.q)) < 2.5f) { match = i; break; }
        }
        if (match >= 0)
        {
            auto b = bands[(size_t) match];
            if (s.type == (int) dsp::FilterType::LowCut || s.type == (int) dsp::FilterType::HighCut)
                b.freq = s.type == (int) dsp::FilterType::LowCut ? std::max (b.freq, s.freq) : std::min (b.freq, s.freq);
            else
                b.gainDb = juce::jlimit (-12.0f, 12.0f, b.gainDb + s.gainDb);
            setBand (match, b);
            continue;
        }
        // 2) a free band (prefer one whose default type matches)
        int freeBand = -1;
        for (int i = 0; i < kEqBands && freeBand < 0; ++i)
            if (! bands[(size_t) i].enabled && ! touched[(size_t) i] && eqDefault (i).type == s.type) freeBand = i;
        for (int i = 0; i < kEqBands && freeBand < 0; ++i)
            if (! bands[(size_t) i].enabled && ! touched[(size_t) i]) freeBand = i;
        if (freeBand < 0)
        {
            // 3) replace the least significant untouched bell
            float least = 1e9f;
            for (int i = 0; i < kEqBands; ++i)
                if (! touched[(size_t) i] && std::abs (bands[(size_t) i].gainDb) < least && bands[(size_t) i].type == (int) dsp::FilterType::Bell)
                { least = std::abs (bands[(size_t) i].gainDb); freeBand = i; }
        }
        if (freeBand < 0) continue;
        dsp::EqBandParams b;
        b.type = s.type; b.freq = s.freq; b.gainDb = s.gainDb; b.q = s.q; b.channel = s.channel;
        setBand (freeBand, b);
    }
    setValue (eqOn, 1.0f);
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::applyStereoRecommendations (const Report& r)
{
    suppressSnapshots = true;
    for (int i = 0; i < kImgBands; ++i) setValue (imgWidth (i), r.recWidthPct[(size_t) i]);
    for (int i = 0; i < 3; ++i) setValue (imgXover (i), r.stats.xovers[(size_t) i]);
    setValue (imgBal, r.recBalanceDb);
    setValue (imgOn, 1.0f);
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::applyMaximizerDrive (float driveDb, float targetLufs)
{
    suppressSnapshots = true;
    setValue (maxDrive, driveDb);
    setValue (maxTarget, targetLufs);
    setValue (maxOn, 1.0f);
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::resetEq()
{
    suppressSnapshots = true;
    for (int i = 0; i < kEqBands; ++i)
    {
        const auto d = eqDefault (i);
        setValue (eqBandOn (i), 0.0f); setValue (eqBandType (i), (float) d.type); setValue (eqBandFreq (i), d.freq);
        setValue (eqBandGain (i), d.gain); setValue (eqBandQ (i), d.q); setValue (eqBandCh (i), 0.0f);
    }
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::resetStereo()
{
    suppressSnapshots = true;
    for (int i = 0; i < kImgBands; ++i) setValue (imgWidth (i), 100.0f);
    for (int i = 0; i < 3; ++i) setValue (imgXover (i), kDefaultXover[i]);
    setValue (imgBal, 0.0f);
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::abSwitch()
{
    abStates[abSlot] = apvts.copyState();
    abSlot ^= 1;
    if (! abStates[abSlot].isValid()) abStates[abSlot] = apvts.copyState();
    suppressSnapshots = true;
    apvts.replaceState (abStates[abSlot].createCopy());
    suppressSnapshots = false;
    pushUndoSnapshot();
}

void MooniProcessor::abCopyToOther()
{
    abStates[abSlot ^ 1] = apvts.copyState();
}

// ---------------------------------------------------------------------------
void MooniProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("MooniState");
    root.setProperty ("version", MOONI_VERSION_STRING, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree s ("Settings");
    s.setProperty ("genre", settings.genreChoice, nullptr);
    s.setProperty ("autoListen", settings.autoListen, nullptr);
    s.setProperty ("mode", (int) settings.mode, nullptr);
    s.setProperty ("reducedMotion", settings.reducedMotion, nullptr);
    s.setProperty ("chatter", settings.chatter, nullptr);
    s.setProperty ("fair", settings.fairCompare, nullptr);
    s.setProperty ("useRef", settings.useReferenceAsTarget, nullptr);
    s.setProperty ("matchRef", settings.matchReferenceLoudness, nullptr);
    s.setProperty ("uiScale", settings.uiScale, nullptr);
    s.setProperty ("room", lastRoom, nullptr);
    s.setProperty ("abSlot", abSlot, nullptr);
    s.setProperty ("reference", reference.getFile().getFullPathName(), nullptr);
    root.appendChild (s, nullptr);
    root.appendChild (history.toValueTree(), nullptr);
    for (int i = 0; i < 2; ++i)
        if (abStates[i].isValid())
        {
            juce::ValueTree ab ("AB" + juce::String (i));
            ab.appendChild (abStates[i].createCopy(), nullptr);
            root.appendChild (ab, nullptr);
        }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void MooniProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("MooniState")) return;

    auto p = root.getChildWithName ("PARAMS");
    if (p.isValid())
    {
        suppressSnapshots = true;
        apvts.replaceState (p.createCopy());
        suppressSnapshots = false;
    }

    auto s = root.getChildWithName ("Settings");
    Settings st;
    st.genreChoice = s.getProperty ("genre", 0);
    st.autoListen = s.getProperty ("autoListen", true);
    st.mode = (AnalysisMode) (int) s.getProperty ("mode", 0);
    st.reducedMotion = s.getProperty ("reducedMotion", false);
    st.chatter = s.getProperty ("chatter", 1);
    st.fairCompare = s.getProperty ("fair", true);
    st.useReferenceAsTarget = s.getProperty ("useRef", true);
    st.matchReferenceLoudness = s.getProperty ("matchRef", true);
    st.uiScale = s.getProperty ("uiScale", 1.0f);
    setSettings (st);
    lastRoom = s.getProperty ("room", 0);

    history.fromValueTree (root.getChildWithName ("History"));
    for (int i = 0; i < 2; ++i)
    {
        auto ab = root.getChildWithName ("AB" + juce::String (i));
        abStates[i] = ab.isValid() && ab.getNumChildren() > 0 ? ab.getChild (0).createCopy() : juce::ValueTree();
    }
    abSlot = juce::jlimit (0, 1, (int) s.getProperty ("abSlot", 0));

    const juce::File refFile (s.getProperty ("reference", "").toString());
    if (refFile.existsAsFile()) reference.load (refFile, currentRate.load());
    undo.reset (apvts.copyState());
}

juce::AudioProcessorEditor* MooniProcessor::createEditor() { return new MooniEditor (*this); }
} // namespace mooni

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mooni::MooniProcessor(); }
