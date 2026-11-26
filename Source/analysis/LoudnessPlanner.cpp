#include "LoudnessPlanner.h"
#include "../core/Text.h"
#include <map>

namespace mooni
{
LoudnessPlanner::LoudnessPlanner() : juce::Thread ("Mooni planner") {}
LoudnessPlanner::~LoudnessPlanner() { stopThread (4000); }

void LoudnessPlanner::prepare (double sampleRate)
{
    std::lock_guard<std::mutex> g (lock);
    sr = sampleRate;
    chunkLen = (int) std::round (sr * 3.0);
    pool.clear();
    current = {};
    chunkMeter.prepare (sr);
    sessionMeter.prepare (sr);
}

void LoudnessPlanner::resetSession()
{
    std::lock_guard<std::mutex> g (lock);
    pool.clear(); current = {};
    chunkMeter.reset(); sessionMeter.reset();
}

void LoudnessPlanner::addAudio (const float* left, const float* right, int n)
{
    std::lock_guard<std::mutex> g (lock);
    sessionMeter.process (left, right, n);
    int offset = 0;
    while (offset < n)
    {
        if (current.l.empty()) { current.l.reserve ((size_t) chunkLen); current.r.reserve ((size_t) chunkLen); }
        const int take = std::min (n - offset, chunkLen - (int) current.l.size());
        current.l.insert (current.l.end(), left + offset, left + offset + take);
        current.r.insert (current.r.end(), right + offset, right + offset + take);
        chunkMeter.process (left + offset, right + offset, take);
        offset += take;
        if ((int) current.l.size() >= chunkLen)
        {
            current.loudness = chunkMeter.getIntegrated();
            chunkMeter.reset();
            if (current.loudness > -60.0f)
            {
                if ((int) pool.size() < kMaxChunks) pool.push_back (std::move (current));
                else
                {
                    auto quietest = std::min_element (pool.begin(), pool.end(), [] (auto& a, auto& b) { return a.loudness < b.loudness; });
                    if (quietest->loudness < current.loudness) *quietest = std::move (current);
                }
            }
            current = {};
        }
    }
}

double LoudnessPlanner::capturedSeconds() const
{
    std::lock_guard<std::mutex> g (lock);
    return (double) pool.size() * 3.0;
}

double LoudnessPlanner::sessionSeconds() const
{
    std::lock_guard<std::mutex> g (lock);
    return sessionMeter.getGatedSeconds();
}

float LoudnessPlanner::sourceIntegrated() const
{
    std::lock_guard<std::mutex> g (lock);
    return sessionMeter.getIntegrated();
}

bool LoudnessPlanner::requestPlan (const Request& r)
{
    if (busy.load()) return false;
    {
        std::lock_guard<std::mutex> g (lock);
        if (pool.size() < 2) return false;
        pending = r;
    }
    busy = true;
    stopThread (100);
    startThread (juce::Thread::Priority::low);
    return true;
}

LoudnessPlanner::Result LoudnessPlanner::getResult() const
{
    std::lock_guard<std::mutex> g (lock);
    return last;
}

void LoudnessPlanner::run()
{
    Request req; std::vector<Chunk> chunks; std::vector<float> blocks; double rate;
    {
        std::lock_guard<std::mutex> g (lock);
        req = pending;
        chunks = pool;
        blocks = sessionMeter.getBlockLoudness();
        rate = sr;
    }
    // keep the 15 loudest chunks: they decide how hard the limiter works
    std::sort (chunks.begin(), chunks.end(), [] (auto& a, auto& b) { return a.loudness > b.loudness; });
    if (chunks.size() > 15) chunks.resize (15);

    auto res = plan (req, std::move (chunks), std::move (blocks), rate);
    {
        std::lock_guard<std::mutex> g (lock);
        last = res;
    }
    ++results;
    busy = false;
}

LoudnessPlanner::Result LoudnessPlanner::plan (const Request& req, std::vector<Chunk> chunks, std::vector<float> sessionBlocks, double rate)
{
    Result res;
    res.targetLufs = req.targetLufs;
    res.timeMs = juce::Time::currentTimeMillis();
    res.secondsUsed = (double) chunks.size() * 3.0;
    res.secondsSession = (double) std::count_if (sessionBlocks.begin(), sessionBlocks.end(), [] (float b) { return b > -70.0f; }) * 0.1;
    res.sourceLufs = dsp::LoudnessMeter::gatedIntegrated (sessionBlocks);
    if (chunks.empty() || res.sourceLufs < -69.0f) { res.error = ru ("Недостаточно звука для расчёта."); return res; }

    struct Sim { float predicted; float avgGr, maxGr, p95Gr, clip; };
    const int blockLen = (int) std::round (rate * 0.1);

    auto simulate = [&] (float drive) -> Sim
    {
        dsp::Maximizer mx;
        dsp::MaximizerParams p;
        p.enabled = true; p.driveDb = drive; p.ceilingDb = req.ceilingDb; p.releaseMs = req.releaseMs; p.clipPct = req.clipPct;
        mx.setParams (p);
        mx.prepare (rate, blockLen);

        std::map<int, std::pair<double, int>> deltaByLevel;   // pre level (1 dB bins) -> sum delta, count
        std::vector<float> grs; double clipSum = 0; int clipN = 0;
        std::vector<float> bl ((size_t) blockLen), br ((size_t) blockLen);
        for (auto& c : chunks)
        {
            mx.reset();
            dsp::LoudnessMeter pre, post; pre.prepare (rate); post.prepare (rate);
            const int total = (int) c.l.size();
            for (int pos = 0; pos + blockLen <= total; pos += blockLen)
            {
                std::copy_n (c.l.data() + pos, blockLen, bl.data());
                std::copy_n (c.r.data() + pos, blockLen, br.data());
                pre.process (bl.data(), br.data(), blockLen);
                mx.process (bl.data(), br.data(), blockLen);
                post.process (bl.data(), br.data(), blockLen);
                if (pos >= blockLen * 6)   // skip start-up (meter window + limiter latency)
                {
                    const float a = pre.getMomentary(), b = post.getMomentary();
                    if (a > -70.0f)
                    {
                        auto& e = deltaByLevel[(int) std::floor (a)];
                        e.first += b - a; ++e.second;
                        grs.push_back (-mx.getBlockGainReductionDb());
                        if (req.clipPct > 0) { clipSum += -mx.getBlockClipReductionDb(); ++clipN; }
                    }
                }
            }
        }
        Sim s {};
        // predict whole-session loudness: each session block shifted by the delta measured at its level
        std::vector<float> predicted; predicted.reserve (sessionBlocks.size());
        int lowestBin = 1000, highestBin = -1000;
        for (auto& [k, v] : deltaByLevel) { lowestBin = std::min (lowestBin, k); highestBin = std::max (highestBin, k); }
        for (auto b : sessionBlocks)
        {
            if (b <= -70.0f) { predicted.push_back (b); continue; }
            const int bin = (int) std::floor (b);
            float d;
            auto it = deltaByLevel.find (bin);
            if (it != deltaByLevel.end()) d = (float) (it->second.first / it->second.second);
            else if (bin < lowestBin) d = drive;                            // quieter than anything simulated: no limiting
            else
            {
                auto nearest = deltaByLevel.lower_bound (bin);
                if (nearest == deltaByLevel.end()) --nearest;
                d = (float) (nearest->second.first / nearest->second.second);
            }
            predicted.push_back (b + d);
        }
        s.predicted = dsp::LoudnessMeter::gatedIntegrated (predicted);
        if (! grs.empty())
        {
            double sum = 0; for (auto g : grs) sum += g;
            s.avgGr = (float) (sum / grs.size());
            std::sort (grs.begin(), grs.end());
            s.maxGr = grs.back();
            s.p95Gr = grs[(size_t) std::min ((int) grs.size() - 1, (int) std::round (0.95 * (grs.size() - 1)))];
        }
        s.clip = clipN > 0 ? (float) (clipSum / clipN) : 0.0f;
        return s;
    };

    // secant search for drive -> target
    float d0 = juce::jlimit (-6.0f, 24.0f, req.targetLufs - res.sourceLufs);
    Sim s0 = simulate (d0);
    float d1 = juce::jlimit (-6.0f, 24.0f, d0 + (req.targetLufs - s0.predicted) * 1.2f);
    Sim s1 = s0;
    if (std::abs (s0.predicted - req.targetLufs) > 0.1f)
    {
        s1 = simulate (d1);
        for (int it = 0; it < 6 && std::abs (s1.predicted - req.targetLufs) > 0.1f; ++it)
        {
            if (threadShouldExit()) break;
            const float slope = (s1.predicted - s0.predicted) / ((d1 - d0) == 0 ? 1e-3f : (d1 - d0));
            float d2 = std::abs (slope) < 0.05f ? d1 + (req.targetLufs - s1.predicted) * 2.0f
                                                : d1 + (req.targetLufs - s1.predicted) / slope;
            d2 = juce::jlimit (-6.0f, 24.0f, d2);
            if (std::abs (d2 - d1) < 0.02f) break;
            d0 = d1; s0 = s1; d1 = d2; s1 = simulate (d1);
        }
    }
    else d1 = d0;

    // Unreachable target: loudness saturates (the limiter only pumps harder). Find the knee —
    // the smallest drive that gets within 0.3 LU of the loudest achievable result.
    bool unreachable = false;
    if (d1 >= 23.9f && s1.predicted < req.targetLufs - 0.3f && ! threadShouldExit())
    {
        unreachable = true;
        const float maxLoud = s1.predicted;
        float lo = std::min (d0, 23.0f), hi = 24.0f; Sim best = s1;
        for (int it = 0; it < 7; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            const Sim sm = simulate (mid);
            if (sm.predicted >= maxLoud - 0.3f) { hi = mid; best = sm; } else lo = mid;
        }
        d1 = hi; s1 = best;
    }

    res.driveDb = std::round (d1 * 10.0f) / 10.0f;
    res.predictedLufs = s1.predicted;
    res.avgGrDb = s1.avgGr; res.maxGrDb = s1.maxGr; res.p95GrDb = s1.p95Gr; res.clipShareDb = s1.clip;
    res.risk = s1.avgGr > req.maxAvgGrDb * 1.5f ? 2 : (s1.avgGr > req.maxAvgGrDb ? 1 : 0);

    // safe target: largest drive with avg GR <= allowance (bisection on avg GR)
    res.safeTargetLufs = res.predictedLufs;
    if (res.risk > 0 && ! threadShouldExit())
    {
        float lo = -6.0f, hi = d1; Sim best = simulate (lo);
        for (int it = 0; it < 6; ++it)
        {
            const float mid = 0.5f * (lo + hi);
            const Sim sm = simulate (mid);
            if (sm.avgGr <= req.maxAvgGrDb) { lo = mid; best = sm; } else hi = mid;
        }
        res.safeTargetLufs = std::round (best.predicted * 2.0f) / 2.0f;
    }
    res.valid = std::abs (res.predictedLufs - req.targetLufs) < 1.0f || res.driveDb >= 24.0f || res.driveDb <= -6.0f;
    if (! res.valid) res.error = ru ("Не удалось точно попасть в цель — материал ведёт себя нелинейно.");
    if (unreachable)
    {
        res.valid = true;
        res.error = fmt ("Цель недостижима: громче ≈{} LUFS этот материал не станет — лимитер просто начнёт сильнее «качать». Помогут клиппер или работа над пиками в миксе.",
                         juce::String (std::round (res.predictedLufs * 10.0f) / 10.0f, 1));
    }
    return res;
}
} // namespace mooni
