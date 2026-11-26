#include "AnalysisEngine.h"
#include "../core/Text.h"

namespace mooni
{
AnalysisEngine::AnalysisEngine() : juce::Thread ("Mooni analysis")
{
    for (auto& b : fifoBuf) b.assign ((size_t) kFifoSize, 0.0f);
    for (auto& s : scratch) s.assign (4096, 0.0f);
    startThread (juce::Thread::Priority::low);
}

AnalysisEngine::~AnalysisEngine()
{
    stopThread (3000);
}

void AnalysisEngine::prepare (double sampleRate, int)
{
    pendingRate = sampleRate;
    fifo.reset();
}

void AnalysisEngine::releaseResources() {}

void AnalysisEngine::pushAudio (const float* inL, const float* inR, const float* preL, const float* preR,
                                const float* outL, const float* outR, int n, bool playing, bool hasTransport) noexcept
{
    hostPlaying.store (playing, std::memory_order_relaxed);
    hostHasTransport.store (hasTransport, std::memory_order_relaxed);
    int s1, z1, s2, z2;
    fifo.prepareToWrite (n, s1, z1, s2, z2);
    if (z1 + z2 < n) drops.fetch_add (1, std::memory_order_relaxed);
    const float* src[6] = { inL, inR, preL, preR, outL, outR };
    for (int c = 0; c < 6; ++c)
    {
        if (z1 > 0) std::memcpy (fifoBuf[(size_t) c].data() + s1, src[c], sizeof (float) * (size_t) z1);
        if (z2 > 0) std::memcpy (fifoBuf[(size_t) c].data() + s2, src[c] + z1, sizeof (float) * (size_t) z2);
    }
    fifo.finishedWrite (z1 + z2);
}

void AnalysisEngine::startSession()  { ++cmdStart; }
void AnalysisEngine::finishSession() { ++cmdFinish; }
void AnalysisEngine::cancelSession() { ++cmdCancel; }
void AnalysisEngine::rediagnose()    { ++cmdRediagnose; }

void AnalysisEngine::initAtRate()
{
    const std::array<float, 3> x { xo0.load(), xo1.load(), xo2.load() };
    sessOut.prepare (rate, x); sessIn.prepare (rate, x);
    liveOut.prepare (rate, x); liveIn.prepare (rate, x);
    planner.prepare (rate);
    if (state == AnalysisState::Listening || state == AnalysisState::Ready) state = AnalysisState::Idle;
}

void AnalysisEngine::beginSessionInternal()
{
    const std::array<float, 3> x { xo0.load(), xo1.load(), xo2.load() };
    sessOut.prepare (rate, x); sessIn.prepare (rate, x);
    liveOut.reset(); liveIn.reset();
    planner.resetSession();
    state = AnalysisState::Listening;
    readyNotified = false;
    stoppedSeconds = 0;
    ++sessionId; ++sessions;
}

void AnalysisEngine::finaliseInternal (bool final)
{
    auto stats = sessOut.computeStats();
    if (! stats.isValid() || stats.secondsAnalysed < 3.0)
    {
        if (final) state = AnalysisState::TooShort;
        return;
    }
    DiagnosticsContext ctx;
    std::function<void (ReportPtr, bool)> cb;
    {
        std::lock_guard<std::mutex> g (ctxLock);
        if (contextProvider) ctx = contextProvider();
        cb = reportCallback;
    }
    auto rep = std::make_shared<Report> (diagnostics::diagnose (stats, ctx));
    const auto inStats = sessIn.computeStats();
    rep->inIntegrated = inStats.integrated;
    rep->inTruePeak = inStats.truePeakDb;
    rep->inSixth = inStats.sixth;
    rep->session = sessionId;
    lastStats = stats; hasLastStats = true;
    {
        std::lock_guard<std::mutex> g (reportLock);
        latest = rep;
    }
    ++reports;
    if (final) state = AnalysisState::Done;
    if (cb) cb (rep, final);
}

void AnalysisEngine::handleTransport (bool playing, bool hasTransport, bool signal)
{
    const bool active = state == AnalysisState::Listening || state == AnalysisState::Ready;
    const bool effectivelyPlaying = hasTransport ? playing : (signal || silentSeconds < 1.5);

    if (effectivelyPlaying && ! wasPlaying && autoListen && ! active && (signal || hasTransport))
        beginSessionInternal();

    if (active)
    {
        const bool stopped = hasTransport ? (! playing && stoppedSeconds > 0.25) : silentSeconds > 2.0;
        const bool endOfSong = silentSeconds > 3.5 && sessOut.activeSeconds() > 3.0;   // track ended, transport rolls on
        if (stopped || endOfSong)
            finaliseInternal (true);
    }
    wasPlaying = effectivelyPlaying;
}

void AnalysisEngine::run()
{
    double readinessClock = 0, refreshClock = 0;
    juce::int64 lastAudioMs = juce::Time::currentTimeMillis();

    while (! threadShouldExit())
    {
        const double pr = pendingRate.exchange (0.0);
        if (pr > 0.0) { rate = pr; initAtRate(); }
        if (rate <= 0.0) { wait (20); continue; }

        // commands
        if (const int c = cmdCancel.load(); c != seenCancel) { seenCancel = c; state = AnalysisState::Idle; }
        if (const int c = cmdStart.load(); c != seenStart) { seenStart = c; beginSessionInternal(); }
        if (const int c = cmdFinish.load(); c != seenFinish)
        {
            seenFinish = c;
            if (state == AnalysisState::Listening || state == AnalysisState::Ready) finaliseInternal (true);
        }
        if (const int c = cmdRediagnose.load(); c != seenRediagnose)
        {
            seenRediagnose = c;
            if (hasLastStats && (state == AnalysisState::Done || state == AnalysisState::Ready))
            {
                DiagnosticsContext ctx; std::function<void (ReportPtr, bool)> cb;
                { std::lock_guard<std::mutex> g (ctxLock); if (contextProvider) ctx = contextProvider(); cb = reportCallback; }
                ReportPtr prev; { std::lock_guard<std::mutex> g (reportLock); prev = latest; }
                auto rep = std::make_shared<Report> (diagnostics::diagnose (lastStats, ctx));
                if (prev) { rep->inIntegrated = prev->inIntegrated; rep->inTruePeak = prev->inTruePeak; rep->inSixth = prev->inSixth; rep->session = prev->session; rep->timeMs = prev->timeMs; }
                { std::lock_guard<std::mutex> g (reportLock); latest = rep; }
                ++reports;
                if (cb) cb (rep, state == AnalysisState::Done);
            }
        }

        // drain FIFO (bounded per pass so commands and publishing stay responsive)
        int total = 0; bool signal = false;
        const bool hasTransport = hostHasTransport.load();
        const int maxPerPass = (int) (rate * 0.25);
        while (total < maxPerPass)
        {
            int s1, z1, s2, z2;
            fifo.prepareToRead (2048, s1, z1, s2, z2);
            const int n = z1 + z2;
            if (n == 0) break;
            for (int c = 0; c < 6; ++c)
            {
                auto& dst = scratch[(size_t) c];
                if ((int) dst.size() < n) dst.resize ((size_t) n);
                std::memcpy (dst.data(), fifoBuf[(size_t) c].data() + s1, sizeof (float) * (size_t) z1);
                if (z2 > 0) std::memcpy (dst.data() + z1, fifoBuf[(size_t) c].data() + s2, sizeof (float) * (size_t) z2);
            }
            fifo.finishedRead (n);

            const float* inL = scratch[0].data(); const float* inR = scratch[1].data();
            const float* preL = scratch[2].data(); const float* preR = scratch[3].data();
            const float* outL = scratch[4].data(); const float* outR = scratch[5].data();

            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (inL[i]), std::abs (inR[i])));
            const bool chunkSignal = pk > 3.0e-5f;   // ~ -90 dBFS
            signal = signal || chunkSignal;
            silentSeconds = chunkSignal ? 0.0 : silentSeconds + n / rate;
            const bool playingNow = hostPlaying.load();
            stoppedSeconds = playingNow ? 0.0 : stoppedSeconds + n / rate;

            // transport decisions happen before this chunk is accumulated, so a session
            // that starts now includes it
            handleTransport (playingNow, hasTransport, chunkSignal);

            liveOut.process (outL, outR, n);
            liveIn.process (inL, inR, n);
            if (state == AnalysisState::Listening || state == AnalysisState::Ready)
            {
                sessOut.process (outL, outR, n);
                sessIn.process (inL, inR, n);
                planner.addAudio (preL, preR, n);
            }

            // vectorscope: most recent frames
            {
                std::lock_guard<std::mutex> g (liveLock);
                const int pts = LiveData::kScopePoints;
                const int start = std::max (0, n - pts * 2);
                int k = 0;
                for (int i = start; i + 1 < n && k < pts; i += 2, ++k)
                {
                    live.scope[(size_t) (2 * k)]     = 0.5f * (outL[i] - outR[i]);
                    live.scope[(size_t) (2 * k + 1)] = 0.5f * (outL[i] + outR[i]);
                }
            }
            total += n;
            if (threadShouldExit()) break;
        }

        const juce::int64 now = juce::Time::currentTimeMillis();
        double dt = total / rate;
        if (total > 0) lastAudioMs = now;
        else if (now - lastAudioMs > 300)
        {
            // host stopped calling processBlock: advance clocks with wall time and treat as stopped
            dt = 0.05;
            silentSeconds += dt; stoppedSeconds += dt;
            handleTransport (false, hasTransport, false);
        }

        // readiness / smart refresh
        readinessClock += dt; refreshClock += dt;
        if ((state == AnalysisState::Listening || state == AnalysisState::Ready) && readinessClock > 0.5)
        {
            readinessClock = 0;
            const auto rd = sessOut.readiness();
            if (! readyNotified && rd.ready)
            {
                readyNotified = true;
                state = AnalysisState::Ready;
                ++readyEvents;
                if ((AnalysisMode) mode.load() == AnalysisMode::Smart) { finaliseInternal (false); refreshClock = 0; }
            }
            else if (readyNotified && (AnalysisMode) mode.load() == AnalysisMode::Smart && refreshClock > 8.0)
            {
                refreshClock = 0;
                finaliseInternal (false);
            }
        }

        if (now - lastPublish > 30) { lastPublish = now; publishLive(); }
        wait (8);
    }
}

void AnalysisEngine::publishLive()
{
    const bool active = state == AnalysisState::Listening || state == AnalysisState::Ready;
    const auto& lm = liveOut.meter();
    const auto& li = liveIn.meter();

    // display spectrum: power at log-spaced points, smoothed over ~1/12 octave, +3 dB/oct tilt
    auto toDisplay = [this] (const std::vector<float>& power, std::array<float, LiveData::kSpectrumPoints>& out)
    {
        const double binHz = rate / MixAccumulator::kFftSize;
        for (int i = 0; i < LiveData::kSpectrumPoints; ++i)
        {
            const double f = LiveData::spectrumFreq (i);
            const double lo = f * 0.9439, hi = f * 1.0595;
            const int b0 = std::max (1, (int) std::floor (lo / binHz)), b1 = std::min ((int) power.size() - 1, (int) std::ceil (hi / binHz));
            double p = 0; int n = 0;
            for (int b = b0; b <= b1; ++b) { p += power[(size_t) b]; ++n; }
            if (n == 0) { const int b = std::clamp ((int) std::round (f / binHz), 1, (int) power.size() - 1); p = power[(size_t) b]; n = 1; }
            const double db = 10.0 * std::log10 (std::max (1.0e-14, p / n)) + 3.0 * std::log2 (f / 1000.0);
            out[(size_t) i] = f > rate * 0.5 ? -120.0f : (float) db;
        }
    };

    const auto rd = active ? sessOut.readiness() : MixAccumulator::Readiness {};
    const float inST = li.getShortTerm(), outST = lm.getShortTerm();

    std::lock_guard<std::mutex> g (liveLock);
    live.state = state;
    live.mode = (AnalysisMode) mode.load();
    live.transportPlaying = hostPlaying.load();
    live.signalPresent = silentSeconds < 0.5;
    live.momentary = lm.getMomentary();
    live.shortTerm = outST;
    live.integrated = active || state == AnalysisState::Done ? sessOut.meter().getIntegrated() : lm.getIntegrated();
    live.integratedIn = active || state == AnalysisState::Done ? sessIn.meter().getIntegrated() : li.getIntegrated();
    live.lra = sessOut.meter().getLoudnessRange();
    live.samplePeak = liveOut.currentPeakDb();
    live.truePeak = liveOut.currentTruePeakDb();
    const auto nowMs = juce::Time::currentTimeMillis();
    if (live.truePeak > tpHold || nowMs - (juce::int64) tpHoldAge > 3000) { tpHold = live.truePeak; tpHoldAge = (double) nowMs; }
    live.truePeakHold = tpHold;
    live.correlation = liveOut.recentCorrelation();
    live.widthPct = liveOut.recentWidthPct();
    live.confidence = rd.confidence;
    live.activeSeconds = active ? rd.activeSeconds : sessOut.activeSeconds();
    live.loudStab = rd.loudnessStability; live.specStab = rd.spectrumStability; live.stereoStab = rd.stereoStability;
    live.overs = sessOut.truePeakOverCount();
    live.clipRuns = sessOut.flatRunCount();
    live.onsetsTotal += liveOut.consumeOnsets();
    live.sessionId = sessionId;
    live.fifoDrops = drops.load();
    live.planCaptured = planner.capturedSeconds();
    toDisplay (liveOut.liveSpectrumPower(), live.specOut);
    toDisplay (liveIn.liveSpectrumPower(), live.specIn);

    auto tail = [] (const std::vector<float>& h, std::vector<float>& out)
    {
        const size_t n = std::min<size_t> (h.size(), LiveData::kHistory);
        out.assign (h.end() - (long) n, h.end());
    };
    tail (lm.getShortTermHistory(), live.histOut);
    tail (li.getShortTermHistory(), live.histIn);

    if (! freezeMatchFlag.load() && inST > -60.0f && outST > -60.0f)
    {
        const float target = inST - outST;
        float m = matchGainDb.load();
        m += (target - m) * 0.08f;
        matchGainDb = juce::jlimit (-30.0f, 30.0f, m);
    }
    live.matchGainDb = matchGainDb.load();
}

void AnalysisEngine::getLive (LiveData& out) const
{
    std::lock_guard<std::mutex> g (liveLock);
    out = live;
}

ReportPtr AnalysisEngine::getLatestReport() const
{
    std::lock_guard<std::mutex> g (reportLock);
    return latest;
}
} // namespace mooni
