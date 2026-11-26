#pragma once
#include "Biquad.h"
#include <array>

namespace mooni::dsp
{
/** 4th-order Linkwitz-Riley split (two cascaded Butterworth biquads per side). */
struct LR4
{
    BiquadCoeffs lp, hp;
    BiquadState lp1, lp2, hp1, hp2;

    void design (double sr, double f)
    {
        lp = BiquadCoeffs::make (FilterType::HighCut, sr, f, 0.70710678118654752, 0);
        hp = BiquadCoeffs::make (FilterType::LowCut,  sr, f, 0.70710678118654752, 0);
    }
    inline void split (float x, float& low, float& high) noexcept
    {
        low  = lp2.process (lp, lp1.process (lp, x));
        high = hp2.process (hp, hp1.process (hp, x));
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void sanitize() { lp1.sanitize(); lp2.sanitize(); hp1.sanitize(); hp2.sanitize(); }
};

struct AllPass2
{
    BiquadCoeffs c; BiquadState s;
    void design (double sr, double f) { c = BiquadCoeffs::allpass (sr, f); }
    inline float process (float x) noexcept { return s.process (c, x); }
    void reset() { s.reset(); }
};

/** Four-band M/S width processor.
    Only the Side signal is band-split (LR4 tree with all-pass compensation); Mid is passed
    through the identical all-pass cascade, so at 100 % width the output is L/R phase-coherent
    and magnitude-flat. Width 0 % = mono band, 100 % = unchanged, up to 200 %. */
class StereoImager
{
public:
    static constexpr int kNumBands = 4;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setEnabled (bool on) { enabledTarget = on; }
    void setCrossovers (float f1, float f2, float f3);
    void setWidths (const std::array<float, kNumBands>& widthPercent);
    void setBalanceDb (float db) { balanceTarget = db; }

    void process (float* left, float* right, int numSamples) noexcept;

    /** Split one stereo sample into per-band side signals (used by the analyser too). */
    static void sortCrossovers (float& f1, float& f2, float& f3);

private:
    void design();

    double sr = 44100.0;
    float xo[3] { 120.0f, 1000.0f, 6000.0f };
    float designed[3] { 0, 0, 0 };
    std::array<float, kNumBands> widthTarget { 100, 100, 100, 100 };
    std::array<OnePole, kNumBands> width;
    OnePole balance;
    float balanceTarget = 0.0f;

    LR4 splitMid, splitLow, splitHigh;  // on Side: at f2, then f1 (low branch), f3 (high branch)
    AllPass2 apLowBranch, apHighBranch; // compensation: low branch gets AP(f3), high branch AP(f1)
    AllPass2 apM1, apM2, apM3;          // Mid path: AP(f1)·AP(f2)·AP(f3)

    bool enabledTarget = false;
    Ramp mix;
    float gainL = 1.0f, gainR = 1.0f;
};
} // namespace mooni::dsp
