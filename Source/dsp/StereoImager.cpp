#include "StereoImager.h"

namespace mooni::dsp
{
void StereoImager::sortCrossovers (float& f1, float& f2, float& f3)
{
    f1 = std::clamp (f1, 40.0f, 800.0f);
    f2 = std::clamp (f2, f1 * 1.5f, 8000.0f);
    f3 = std::clamp (f3, f2 * 1.5f, 16000.0f);
}

void StereoImager::prepare (double sampleRate, int)
{
    sr = sampleRate;
    for (size_t i = 0; i < width.size(); ++i)
    {
        width[i].setTime (0.04, sr);
        width[i].reset (widthTarget[i] * 0.01);
    }
    balance.setTime (0.04, sr);
    balance.reset (balanceTarget);
    mix.setLength (0.03, sr);
    mix.reset (enabledTarget ? 1.0f : 0.0f);
    design();
    reset();
}

void StereoImager::reset()
{
    splitMid.reset(); splitLow.reset(); splitHigh.reset();
    apLowBranch.reset(); apHighBranch.reset(); apM1.reset(); apM2.reset(); apM3.reset();
}

void StereoImager::setCrossovers (float f1, float f2, float f3)
{
    sortCrossovers (f1, f2, f3);
    xo[0] = f1; xo[1] = f2; xo[2] = f3;
}

void StereoImager::setWidths (const std::array<float, kNumBands>& w)
{
    for (size_t i = 0; i < w.size(); ++i)
        widthTarget[i] = std::clamp (w[i], 0.0f, 200.0f);
}

void StereoImager::design()
{
    const float f1 = std::min (xo[0], (float) sr * 0.45f);
    const float f2 = std::min (xo[1], (float) sr * 0.45f);
    const float f3 = std::min (xo[2], (float) sr * 0.45f);
    splitMid.design (sr, f2);
    splitLow.design (sr, f1);
    splitHigh.design (sr, f3);
    apLowBranch.design (sr, f3);
    apHighBranch.design (sr, f1);
    apM1.design (sr, f1); apM2.design (sr, f2); apM3.design (sr, f3);
    designed[0] = xo[0]; designed[1] = xo[1]; designed[2] = xo[2];
}

void StereoImager::process (float* left, float* right, int numSamples) noexcept
{
    mix.target = enabledTarget ? 1.0f : 0.0f;
    if (! enabledTarget && mix.value <= 0.0f)
    {
        reset();
        return;
    }

    // Crossover moves are glided (max ~6 % per block) to avoid zipper noise from large jumps.
    if (designed[0] != xo[0] || designed[1] != xo[1] || designed[2] != xo[2])
    {
        float next[3];
        for (int i = 0; i < 3; ++i)
        {
            const float ratio = std::clamp (xo[i] / std::max (1.0f, designed[i]), 1.0f / 1.06f, 1.06f);
            next[i] = std::abs (ratio - 1.0f) < 1.0e-4f ? xo[i] : designed[i] * ratio;
            if ((ratio >= 1.0f && next[i] > xo[i]) || (ratio < 1.0f && next[i] < xo[i])) next[i] = xo[i];
        }
        const float t0 = xo[0], t1 = xo[1], t2 = xo[2];
        xo[0] = next[0]; xo[1] = next[1]; xo[2] = next[2];
        design();                      // designs from xo and stores into designed[]
        xo[0] = t0; xo[1] = t1; xo[2] = t2;
    }

    for (size_t i = 0; i < width.size(); ++i) width[i].target = widthTarget[i] * 0.01;
    balance.target = balanceTarget;

    for (int n = 0; n < numSamples; ++n)
    {
        const float l = left[n], r = right[n];
        const float m = 0.5f * (l + r), s = 0.5f * (l - r);

        float sLow, sHigh, b1, b2, b3, b4;
        splitMid.split (s, sLow, sHigh);
        splitLow.split (sLow, b1, b2);
        splitHigh.split (sHigh, b3, b4);
        // phase alignment: low-branch bands need AP(f3), high-branch bands need AP(f1)
        // (applied to the branch sums; AP is linear so this equals per-band compensation)
        const float w1 = (float) width[0].next(), w2 = (float) width[1].next();
        const float w3 = (float) width[2].next(), w4 = (float) width[3].next();
        const float lowBranch  = w1 * b1 + w2 * b2;
        const float highBranch = w3 * b3 + w4 * b4;

        const float sOut = apLowBranch.process (lowBranch) + apHighBranch.process (highBranch);
        const float mOut = apM3.process (apM2.process (apM1.process (m)));

        if (balance.isSmoothing (1.0e-4) || n == 0)
        {
            const double bal = balance.next();           // dB, + = louder right
            gainL = (float) std::pow (10.0, -bal / 40.0);
            gainR = (float) std::pow (10.0,  bal / 40.0);
        }
        const float gL = gainL, gR = gainR;

        const float wetL = (mOut + sOut) * gL;
        const float wetR = (mOut - sOut) * gR;
        const float g = mix.next();
        left[n]  = l + g * (wetL - l);
        right[n] = r + g * (wetR - r);
    }

    splitMid.sanitize(); splitLow.sanitize(); splitHigh.sanitize();
}
} // namespace mooni::dsp
