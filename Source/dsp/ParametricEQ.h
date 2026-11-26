#pragma once
#include "Biquad.h"
#include <array>
#include <vector>

namespace mooni::dsp
{
enum class BandChannel : int { Stereo = 0, Mid, Side };

struct EqBandParams
{
    bool  enabled = false;
    int   type    = (int) FilterType::Bell;
    float freq    = 1000.0f;
    float gainDb  = 0.0f;
    float q       = 0.707f;
    int   channel = (int) BandChannel::Stereo;

    bool operator== (const EqBandParams& o) const
    {
        return enabled == o.enabled && type == o.type && freq == o.freq && gainDb == o.gainDb && q == o.q && channel == o.channel;
    }
};

/** Eight-band minimum-phase parametric EQ processed in the M/S domain.
    Stereo bands filter M and S identically (== filtering L and R), Mid/Side bands only one of them.
    All parameter changes are smoothed; type/channel changes cross-fade through the dry signal. */
class ParametricEQ
{
public:
    static constexpr int kNumBands = 8;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setEnabled (bool shouldBeOn) { enabledTarget = shouldBeOn; }
    void setBand (int index, const EqBandParams& p) { if (index >= 0 && index < kNumBands) pending[(size_t) index] = p; }

    void process (float* left, float* right, int numSamples) noexcept;

    /** Combined magnitude (dB) of a band set for the Mid (or Side) path at frequency f. */
    static double pathResponseDb (const std::array<EqBandParams, kNumBands>& bands, double f, double sampleRate, bool sidePath);
    static double bandResponseDb (const EqBandParams& band, double f, double sampleRate);

    bool isFullyBypassed() const noexcept { return ! enabledTarget && globalMix.value <= 0.0f; }

private:
    struct Band
    {
        EqBandParams active;               // type/channel currently running
        OnePole logFreq, gain, q;
        Ramp mix;                          // 0 = dry, 1 = filtered
        BiquadCoeffs coeffs;
        BiquadState stM, stS;
        bool needsSwitch = false;
    };

    void updateCoeffs (Band& b);

    double sr = 44100.0;
    std::array<EqBandParams, kNumBands> pending {};
    std::array<Band, kNumBands> bands {};
    bool enabledTarget = false;
    Ramp globalMix;
    int coeffCounter = 0;
};
} // namespace mooni::dsp
