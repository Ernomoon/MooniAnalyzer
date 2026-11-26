#include "LoudnessMeter.h"
#include <algorithm>
#include <numeric>

namespace mooni::dsp
{
void LoudnessMeter::prepare (double sampleRate)
{
    sr = sampleRate;
    // K-weighting for arbitrary sample rates (pre-filter shelf + RLB high-pass), per libebur128.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        pre.b0 = (Vh + Vb * K / Q + K * K) / a0;
        pre.b1 = 2.0 * (K * K - Vh) / a0;
        pre.b2 = (Vh - Vb * K / Q + K * K) / a0;
        pre.a1 = 2.0 * (K * K - 1.0) / a0;
        pre.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
        rlb.a1 = 2.0 * (K * K - 1.0) / a0;
        rlb.a2 = (1.0 - K / Q + K * K) / a0;
    }
    stepLen = std::max (1, (int) std::round (sr * 0.1));
    reset();
}

void LoudnessMeter::reset()
{
    preL.reset(); preR.reset(); rlbL.reset(); rlbR.reset();
    stepFill = 0; stepAcc = 0.0; steps.clear();
    momentary = shortTerm = maxMomentary = maxShortTerm = kSilence;
    blockLoudness.clear(); shortTermHistory.clear();
    gatedBlocks = 0; totalSamples = 0;
}

int LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    int completed = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = rlbL.process (rlb, preL.process (pre, left[i]));
        const float r = rlbR.process (rlb, preR.process (pre, right[i]));
        stepAcc += (double) l * l + (double) r * r;

        if (++stepFill >= stepLen)
        {
            steps.push_back (stepAcc / stepLen);
            if (steps.size() > 30) steps.pop_front();
            stepAcc = 0.0; stepFill = 0;
            ++completed;

            const size_t n = steps.size();
            if (n >= 4)
            {
                double e = 0; for (size_t k = n - 4; k < n; ++k) e += steps[k];
                momentary = energyToLufs (e / 4.0);
                blockLoudness.push_back (momentary);
                maxMomentary = std::max (maxMomentary, momentary);
                if (momentary > -70.0f) ++gatedBlocks;
            }
            if (n >= 30)
            {
                double e = 0; for (auto v : steps) e += v;
                shortTerm = energyToLufs (e / 30.0);
                shortTermHistory.push_back (shortTerm);
                maxShortTerm = std::max (maxShortTerm, shortTerm);
            }
            else if (n >= 4)
            {
                double e = 0; for (auto v : steps) e += v;
                shortTerm = energyToLufs (e / (double) n); // provisional until 3 s are available
            }
        }
    }
    totalSamples += numSamples;
    preL.sanitize(); preR.sanitize(); rlbL.sanitize(); rlbR.sanitize();
    return completed;
}

float LoudnessMeter::gatedIntegrated (const std::vector<float>& blocks)
{
    double sum = 0; int count = 0;
    for (auto b : blocks) if (b > -70.0f) { sum += std::pow (10.0, (b + 0.691) / 10.0); ++count; }
    if (count == 0) return kSilence;
    const double relGate = -0.691 + 10.0 * std::log10 (sum / count) - 10.0;
    sum = 0; count = 0;
    for (auto b : blocks) if (b > -70.0f && b > relGate) { sum += std::pow (10.0, (b + 0.691) / 10.0); ++count; }
    if (count == 0) return kSilence;
    return (float) (-0.691 + 10.0 * std::log10 (sum / count));
}

float LoudnessMeter::getIntegrated() const { return gatedIntegrated (blockLoudness); }

float LoudnessMeter::getLoudnessRange() const
{
    std::vector<float> st;
    st.reserve (shortTermHistory.size());
    double sum = 0; int count = 0;
    for (auto v : shortTermHistory) if (v > -70.0f) { sum += std::pow (10.0, (v + 0.691) / 10.0); ++count; }
    if (count < 2) return 0.0f;
    const double gate = -0.691 + 10.0 * std::log10 (sum / count) - 20.0;
    for (auto v : shortTermHistory) if (v > -70.0f && v > gate) st.push_back (v);
    if (st.size() < 2) return 0.0f;
    std::sort (st.begin(), st.end());
    auto pct = [&] (double p) { return st[(size_t) std::clamp ((int) std::round (p * (double) (st.size() - 1)), 0, (int) st.size() - 1)]; };
    return pct (0.95) - pct (0.10);
}
} // namespace mooni::dsp
