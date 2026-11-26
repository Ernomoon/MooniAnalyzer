#include "ParametricEQ.h"

namespace mooni::dsp
{
static constexpr int kCoeffInterval = 16;

void ParametricEQ::prepare (double sampleRate, int)
{
    sr = sampleRate;
    globalMix.setLength (0.025, sr);
    globalMix.reset (enabledTarget ? 1.0f : 0.0f);

    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& b = bands[i];
        b.active = pending[i];
        b.logFreq.setTime (0.03, sr / kCoeffInterval);
        b.gain.setTime (0.03, sr / kCoeffInterval);
        b.q.setTime (0.03, sr / kCoeffInterval);
        b.logFreq.reset (std::log (std::max (10.0f, pending[i].freq)));
        b.gain.reset (pending[i].gainDb);
        b.q.reset (pending[i].q);
        b.mix.setLength (0.02, sr);
        b.mix.reset (pending[i].enabled ? 1.0f : 0.0f);
        b.needsSwitch = false;
        updateCoeffs (b);
        b.stM.reset(); b.stS.reset();
    }
}

void ParametricEQ::reset()
{
    for (auto& b : bands) { b.stM.reset(); b.stS.reset(); }
}

void ParametricEQ::updateCoeffs (Band& b)
{
    b.coeffs = BiquadCoeffs::make ((FilterType) b.active.type, sr, std::exp (b.logFreq.value), b.q.value, b.gain.value);
}

void ParametricEQ::process (float* left, float* right, int numSamples) noexcept
{
    globalMix.target = enabledTarget ? 1.0f : 0.0f;

    if (! enabledTarget && ! globalMix.isActive() && globalMix.value <= 0.0f)
    {
        for (auto& b : bands) { b.stM.reset(); b.stS.reset(); }
        return; // fully bypassed: bit-transparent
    }

    // Block-rate bookkeeping: parameter targets and type/channel switch requests
    for (size_t i = 0; i < bands.size(); ++i)
    {
        auto& b = bands[i];
        const auto& p = pending[i];
        b.logFreq.target = std::log (std::max (10.0f, p.freq));
        b.gain.target = p.gainDb;
        b.q.target = p.q;

        const bool structural = (p.type != b.active.type) || (p.channel != b.active.channel);
        if (structural && b.mix.value > 0.0f) { b.needsSwitch = true; b.mix.target = 0.0f; }
        else if (structural)
        {
            b.active.type = p.type; b.active.channel = p.channel;
            b.stM.reset(); b.stS.reset();
            // jump smoothers on a silent switch so the new filter starts at its real setting
            b.logFreq.reset (b.logFreq.target); b.gain.reset (b.gain.target); b.q.reset (b.q.target);
            updateCoeffs (b);
            b.needsSwitch = false;
            b.mix.target = p.enabled ? 1.0f : 0.0f;
        }
        else if (! b.needsSwitch)
            b.mix.target = p.enabled ? 1.0f : 0.0f;
        b.active.enabled = p.enabled;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        if (++coeffCounter >= kCoeffInterval)
        {
            coeffCounter = 0;
            for (auto& b : bands)
            {
                if (b.logFreq.isSmoothing() || b.gain.isSmoothing (1e-4) || b.q.isSmoothing())
                {
                    b.logFreq.next(); b.gain.next(); b.q.next();
                    updateCoeffs (b);
                }
            }
        }

        const float l = left[n], r = right[n];
        float m = 0.5f * (l + r), s = 0.5f * (l - r);
        const float dm = m, ds = s;

        for (auto& b : bands)
        {
            const float mix = b.mix.next();
            if (b.needsSwitch && mix <= 0.0f)
            {
                const auto idx = (size_t) (&b - bands.data());
                b.active.type = pending[idx].type; b.active.channel = pending[idx].channel;
                b.stM.reset(); b.stS.reset();
                b.logFreq.reset (b.logFreq.target); b.gain.reset (b.gain.target); b.q.reset (b.q.target);
                updateCoeffs (b);
                b.needsSwitch = false;
                b.mix.target = pending[idx].enabled ? 1.0f : 0.0f;
                continue;
            }
            if (mix <= 0.0f)
                continue;

            const auto ch = (BandChannel) b.active.channel;
            if (ch != BandChannel::Side)
            {
                const float y = b.stM.process (b.coeffs, m);
                m += mix * (y - m);
            }
            if (ch != BandChannel::Mid)
            {
                const float y = b.stS.process (b.coeffs, s);
                s += mix * (y - s);
            }
        }

        const float g = globalMix.next();
        m = dm + g * (m - dm);
        s = ds + g * (s - ds);
        left[n]  = m + s;
        right[n] = m - s;
    }

    for (auto& b : bands) { b.stM.sanitize(); b.stS.sanitize(); }
}

double ParametricEQ::bandResponseDb (const EqBandParams& band, double f, double sampleRate)
{
    if (! band.enabled) return 0.0;
    return BiquadCoeffs::make ((FilterType) band.type, sampleRate, band.freq, band.q, band.gainDb).magnitudeDb (f, sampleRate);
}

double ParametricEQ::pathResponseDb (const std::array<EqBandParams, kNumBands>& bs, double f, double sampleRate, bool sidePath)
{
    double sum = 0.0;
    for (const auto& b : bs)
    {
        if (! b.enabled) continue;
        const auto ch = (BandChannel) b.channel;
        if (sidePath ? ch == BandChannel::Mid : ch == BandChannel::Side) continue;
        sum += bandResponseDb (b, f, sampleRate);
    }
    return sum;
}
} // namespace mooni::dsp
