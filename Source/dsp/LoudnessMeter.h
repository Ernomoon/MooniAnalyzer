#pragma once
#include "Biquad.h"
#include <vector>
#include <deque>

namespace mooni::dsp
{
/** ITU-R BS.1770-4 / EBU R128 loudness meter (stereo, L/R weight 1.0).
    Momentary (400 ms), short-term (3 s), gated integrated loudness and LRA (EBU Tech 3342).
    Not real-time safe (allocates while accumulating history) -> used on the analysis thread. */
class LoudnessMeter
{
public:
    static constexpr float kSilence = -100.0f;

    void prepare (double sampleRate);
    void reset();

    /** Feed stereo samples. Returns number of completed 100 ms steps in this call. */
    int process (const float* left, const float* right, int numSamples);

    float getMomentary() const noexcept   { return momentary; }
    float getShortTerm() const noexcept   { return shortTerm; }
    float getIntegrated() const;
    float getLoudnessRange() const;
    float getMaxMomentary() const noexcept { return maxMomentary; }
    float getMaxShortTerm() const noexcept { return maxShortTerm; }

    /** Seconds of audio whose 400 ms block passed the absolute gate (-70 LUFS). */
    double getGatedSeconds() const noexcept { return gatedBlocks * 0.1; }
    double getSecondsProcessed() const noexcept { return totalSamples / sr; }

    /** History of momentary-block energies (one per 100 ms step) — used for planning. */
    const std::vector<float>& getBlockLoudness() const noexcept { return blockLoudness; }
    const std::vector<float>& getShortTermHistory() const noexcept { return shortTermHistory; }

    /** Integrated loudness over a custom list of block loudness values. */
    static float gatedIntegrated (const std::vector<float>& blocks);

    /** Power gain (linear) of the K-weighting curve at frequency f. */
    double kWeightPower (double f) const
    {
        const double m = std::abs (pre.response (f, sr) * rlb.response (f, sr));
        return m * m;
    }

    static float energyToLufs (double meanSquareSum) noexcept
    {
        return meanSquareSum <= 1.0e-20 ? kSilence : (float) (-0.691 + 10.0 * std::log10 (meanSquareSum));
    }

private:
    double sr = 48000.0;
    BiquadCoeffs pre, rlb;
    BiquadState preL, preR, rlbL, rlbR;

    int stepLen = 4800, stepFill = 0;
    double stepAcc = 0.0;
    std::deque<double> steps;           // last 30 step energies (mean square, summed channels)
    float momentary = kSilence, shortTerm = kSilence;
    float maxMomentary = kSilence, maxShortTerm = kSilence;
    std::vector<float> blockLoudness;   // momentary block loudness every 100 ms
    std::vector<float> shortTermHistory;
    int gatedBlocks = 0;
    double totalSamples = 0;
};
} // namespace mooni::dsp
