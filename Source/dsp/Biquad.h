#pragma once
#include <cmath>
#include <complex>
#include <algorithm>

namespace mooni::dsp
{
constexpr double kPi = 3.14159265358979323846;

enum class FilterType : int
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,   // high-pass, 12 dB/oct
    HighCut,  // low-pass, 12 dB/oct
    Notch,
    NumTypes
};

/** Normalised biquad coefficients (a0 == 1). RBJ Audio-EQ-Cookbook designs. */
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs identity() { return {}; }

    static BiquadCoeffs make (FilterType type, double sampleRate, double freq, double q, double gainDb)
    {
        freq = std::clamp (freq, 5.0, sampleRate * 0.49);
        q    = std::clamp (q, 0.05, 40.0);
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * kPi * freq / sampleRate;
        const double cw    = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;

        switch (type)
        {
            case FilterType::Bell:
                b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
                break;
            case FilterType::LowShelf:
            {
                // q is interpreted as shelf "S" slope-like quality (0.707 = Butterworth-ish)
                const double sq = 2.0 * std::sqrt (A) * alpha;
                b0 =      A * ((A + 1) - (A - 1) * cw + sq);
                b1 =  2 * A * ((A - 1) - (A + 1) * cw);
                b2 =      A * ((A + 1) - (A - 1) * cw - sq);
                a0 =           (A + 1) + (A - 1) * cw + sq;
                a1 =     -2 * ((A - 1) + (A + 1) * cw);
                a2 =           (A + 1) + (A - 1) * cw - sq;
                break;
            }
            case FilterType::HighShelf:
            {
                const double sq = 2.0 * std::sqrt (A) * alpha;
                b0 =      A * ((A + 1) + (A - 1) * cw + sq);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 =      A * ((A + 1) + (A - 1) * cw - sq);
                a0 =           (A + 1) - (A - 1) * cw + sq;
                a1 =      2 * ((A - 1) - (A + 1) * cw);
                a2 =           (A + 1) - (A - 1) * cw - sq;
                break;
            }
            case FilterType::LowCut:
                b0 = (1 + cw) / 2;  b1 = -(1 + cw);  b2 = (1 + cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case FilterType::HighCut:
                b0 = (1 - cw) / 2;  b1 = 1 - cw;     b2 = (1 - cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case FilterType::Notch:
                b0 = 1;             b1 = -2 * cw;    b2 = 1;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case FilterType::NumTypes:
            default:
                return identity();
        }

        BiquadCoeffs c;
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    /** Second-order all-pass with Butterworth poles: equals LR4 LP + HP sum. */
    static BiquadCoeffs allpass (double sampleRate, double freq)
    {
        const double w0 = 2.0 * kPi * std::clamp (freq, 5.0, sampleRate * 0.49) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752);
        const double cw = std::cos (w0);
        const double a0 = 1 + alpha;
        BiquadCoeffs c;
        c.b0 = (1 - alpha) / a0; c.b1 = -2 * cw / a0; c.b2 = 1.0;
        c.a1 = -2 * cw / a0;     c.a2 = (1 - alpha) / a0;
        return c;
    }

    std::complex<double> response (double freq, double sampleRate) const
    {
        const double w = 2.0 * kPi * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    double magnitudeDb (double freq, double sampleRate) const
    {
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (freq, sampleRate))));
    }
};

/** Transposed direct form II biquad, one channel, double state for low-frequency precision. */
struct BiquadState
{
    double z1 = 0, z2 = 0;

    inline float process (const BiquadCoeffs& c, float xin) noexcept
    {
        const double x = xin;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }

    void reset() noexcept { z1 = z2 = 0; }

    /** Flush denormals / NaNs (defensive; called once per block). */
    void sanitize() noexcept
    {
        if (! std::isfinite (z1) || ! std::isfinite (z2)) { z1 = z2 = 0; return; }
        if (std::abs (z1) < 1.0e-25) z1 = 0;
        if (std::abs (z2) < 1.0e-25) z2 = 0;
    }
};

/** One-pole smoother helper for per-sample parameter interpolation. */
struct OnePole
{
    double value = 0, target = 0, coeff = 0;
    void setTime (double seconds, double sampleRate)
    {
        coeff = seconds <= 0 ? 0.0 : std::exp (-1.0 / (seconds * sampleRate));
    }
    void reset (double v) { value = target = v; }
    inline double next() noexcept { value = target + (value - target) * coeff; return value; }
    bool isSmoothing (double eps = 1.0e-5) const noexcept { return std::abs (value - target) > eps; }
};

/** Linear crossfade ramp 0..1 used for clickless switching. */
struct Ramp
{
    float value = 0, target = 0, step = 0.001f;
    void setLength (double seconds, double sampleRate) { step = (float) (1.0 / std::max (1.0, seconds * sampleRate)); }
    void reset (float v) { value = target = v; }
    inline float next() noexcept
    {
        if (value < target)      value = std::min (target, value + step);
        else if (value > target) value = std::max (target, value - step);
        return value;
    }
    bool isActive() const noexcept { return value != target; }
};
} // namespace mooni::dsp
