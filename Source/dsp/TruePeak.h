#pragma once
#include <array>
#include <cmath>
#include <algorithm>

namespace mooni::dsp
{
/** Polyphase windowed-sinc interpolator used for true-peak estimation (ITU-R BS.1770 style:
    4x for metering; the limiter uses 8x for a tighter worst-case bound).
    Phase 0 is an exact delayed copy of the input; phases 1..3 are the inter-sample points.
    Group delay: kCenter input samples. */
template <int OversampleFactor>
class TruePeakDetectorT
{
public:
    static constexpr int kOversample = OversampleFactor;
    static constexpr int kTapsPerPhase = 24;
    static constexpr int kCenter = 11;

    TruePeakDetectorT() { buildTable(); reset(); }

    void reset() { hist.fill (0.0f); pos = 0; }

    /** Push one sample; returns the maximum absolute value among the 4 interpolated points
        located between input samples (n - kCenter - 1) and (n - kCenter). */
    inline float push (float x) noexcept
    {
        hist[(size_t) pos] = x;
        hist[(size_t) (pos + kTapsPerPhase)] = x; // mirrored copy -> contiguous window
        pos = (pos + 1) % kTapsPerPhase;
        const float* w = hist.data() + pos;       // oldest .. newest (kTapsPerPhase samples)

        float peak = std::abs (w[kTapsPerPhase - 1 - kCenter]);   // == x[n - kCenter]
        for (int p = 1; p < kOversample; ++p)
        {
            const float* h = table[static_cast<size_t> (p)].data();
            float acc = 0.0f;
            for (int k = 0; k < kTapsPerPhase; ++k)
                acc += h[k] * w[k];
            peak = std::max (peak, std::abs (acc));
        }
        return peak;
    }

private:
    void buildTable()
    {
        // Window w[k] = x[n-12+k]. Phase p evaluates the band-limited signal at time
        // (n - kCenter - 1) + p/4, i.e. tap(k) = h(kTapsPerPhase - 1 - kCenter - 1 + p/4 - k),
        // with h(t) = sinc(t) * Kaiser window over |t| <= kTapsPerPhase/2 + 0.5 input samples.
        const double beta = 7.0, half = kTapsPerPhase * 0.5 + 0.5;
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        const double denom = bessel0 (beta);
        for (int p = 0; p < kOversample; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k)
            {
                const double t = (kTapsPerPhase - 2 - kCenter) + (double) p / kOversample - k;
                const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (3.14159265358979323846 * t) / (3.14159265358979323846 * t);
                const double r = t / half;
                const double win = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / denom;
                table[(size_t) p][(size_t) k] = (float) (sinc * win);
                sum += sinc * win;
            }
            for (auto& c : table[(size_t) p]) c = (float) (c / sum); // unity DC gain per phase
        }
    }

    std::array<std::array<float, (size_t) kTapsPerPhase>, (size_t) kOversample> table {};
    std::array<float, (size_t) kTapsPerPhase * 2> hist {};
    int pos = 0;
};
/** ITU-style 4x meter. */
using TruePeakDetector = TruePeakDetectorT<4>;
} // namespace mooni::dsp
