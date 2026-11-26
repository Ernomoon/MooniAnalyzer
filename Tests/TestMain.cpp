// Mooni Analyzer — headless test runner for DSP and analysis.
#include "dsp/ParametricEQ.h"
#include "dsp/StereoImager.h"
#include "dsp/Maximizer.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/TruePeak.h"
#include "analysis/Accumulator.h"
#include "analysis/Diagnostics.h"
#include "analysis/LoudnessPlanner.h"
#include "analysis/AnalysisEngine.h"
#include "analysis/ReferenceTrack.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <random>
#include <cstdio>

using namespace mooni;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s  | ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
#define NEAR(a, b, tol, label) do { const double _a = (a), _b = (b); ++checks; if (std::abs (_a - _b) > (tol)) { ++failures; std::printf ("  FAIL %s:%d  %s: got %.4f expected %.4f (tol %.3f)\n", __FILE__, __LINE__, label, _a, _b, (double) (tol)); } else std::printf ("  ok   %-48s %.4f (expect %.4f)\n", label, _a, _b); } while (0)

static constexpr double PI = 3.14159265358979323846;
using Buf = std::vector<float>;
static std::mt19937 rng (1234);

static void sine (Buf& b, double sr, double f, double amp, double phase = 0)
{ for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (amp * std::sin (2 * PI * f * (double) i / sr + phase)); }

static void white (Buf& b, double amp)
{ std::normal_distribution<float> d (0.0f, 1.0f); for (auto& x : b) x = (float) amp * d (rng); }

static void pink (Buf& b, double amp)
{
    // Paul Kellet's refined pink filter
    std::normal_distribution<float> d (0.0f, 1.0f);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& x : b)
    {
        const double w = d (rng);
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        x = (float) (amp * 0.11 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362));
        b6 = w * 0.115926;
    }
}

/** Steep 8th-order low-pass (realistic band-limited programme material). */
static void bandLimit (Buf& b, double sr, double fc = 18000.0)
{
    const auto c = dsp::BiquadCoeffs::make (dsp::FilterType::HighCut, sr, std::min (fc, sr * 0.42), 0.7071, 0);
    dsp::BiquadState st[4];
    for (auto& x : b) { float y = x; for (auto& s : st) y = s.process (c, y); x = y; }
}

static double rmsDb (const Buf& b, size_t from = 0)
{ double s = 0; for (size_t i = from; i < b.size(); ++i) s += (double) b[i] * b[i]; return 10 * std::log10 (s / (double) (b.size() - from) + 1e-30); }

/** Independent reference true-peak: 16x windowed-sinc interpolation with 64-tap half length. */
static double referenceTruePeakDb (const Buf& x)
{
    double peak = 0;
    const int L = 16, H = 64;
    for (size_t n = (size_t) H; n + (size_t) H < x.size(); ++n)
        for (int p = 0; p < L; ++p)
        {
            const double t = (double) p / L;
            double acc = 0;
            for (int k = -H + 1; k <= H; ++k)
            {
                const double d = t - k;
                const double s = std::abs (d) < 1e-12 ? 1.0 : std::sin (PI * d) / (PI * d);
                const double w = 0.5 + 0.5 * std::cos (PI * d / (H + 1));
                acc += x[n + (size_t) k] * s * w;
            }
            peak = std::max (peak, std::abs (acc));
        }
    return 20 * std::log10 (peak + 1e-30);
}

/** Iterate a buffer in blocks, clamping the last block. */
template <typename F> static void blocks (size_t total, int bs, F&& f)
{ for (size_t p = 0; p < total; p += (size_t) bs) f (p, (int) std::min<size_t> ((size_t) bs, total - p)); }

static float measureLufs (const Buf& l, const Buf& r, double sr)
{
    dsp::LoudnessMeter m; m.prepare (sr);
    m.process (l.data(), r.data(), (int) l.size());
    return m.getIntegrated();
}

// ---------------------------------------------------------------------------
static void testLoudness()
{
    std::printf ("\n[Loudness meter — EBU Tech 3341]\n");
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Buf l ((size_t) (sr * 20)), r;
        sine (l, sr, 1000, std::pow (10.0, -23.0 / 20.0)); r = l;
        NEAR (measureLufs (l, r, sr), -23.0, 0.1, (juce::String ("1 kHz -23 dBFS stereo @ ") + juce::String (sr, 0)).toRawUTF8());
    }
    {
        const double sr = 48000;
        Buf l ((size_t) (sr * 80));
        const double a36 = std::pow (10.0, -36.0 / 20.0), a23 = std::pow (10.0, -23.0 / 20.0);
        for (size_t i = 0; i < l.size(); ++i)
        {
            const double t = (double) i / sr;
            const double a = (t < 10 || t >= 70) ? a36 : a23;
            l[i] = (float) (a * std::sin (2 * PI * 1000 * t));
        }
        Buf r = l;
        NEAR (measureLufs (l, r, sr), -23.0, 0.1, "Tech 3341 case 3 (gating -36/-23/-36)");
        dsp::LoudnessMeter m; m.prepare (sr); m.process (l.data(), r.data(), (int) l.size());
        CHECK (m.getMaxMomentary() > -23.2f && m.getMaxMomentary() < -22.8f, "max momentary %.2f", m.getMaxMomentary());
    }
    {
        // LRA: Tech 3342 case 1: 20 s at -20 dBFS then 20 s at -30 dBFS -> LRA 10
        const double sr = 48000;
        Buf l ((size_t) (sr * 40));
        for (size_t i = 0; i < l.size(); ++i)
        {
            const double t = (double) i / sr;
            l[i] = (float) (std::pow (10.0, (t < 20 ? -20.0 : -30.0) / 20.0) * std::sin (2 * PI * 1000 * t));
        }
        Buf r = l;
        dsp::LoudnessMeter m; m.prepare (sr); m.process (l.data(), r.data(), (int) l.size());
        NEAR (m.getLoudnessRange(), 10.0, 0.3, "LRA Tech 3342 case 1");
    }
    {
        Buf z (48000 * 5, 0.0f);
        dsp::LoudnessMeter m; m.prepare (48000); m.process (z.data(), z.data(), (int) z.size());
        CHECK (m.getIntegrated() <= -99.0f, "silence integrated %.2f", m.getIntegrated());
    }
}

static void testTruePeak()
{
    std::printf ("\n[True peak]\n");
    const double sr = 48000;
    Buf x ((size_t) sr);
    sine (x, sr, sr / 4, 1.0, PI / 4);   // samples at +-0.707
    dsp::TruePeakDetector tp; float pk = 0, sp = 0;
    for (auto v : x) { pk = std::max (pk, tp.push (v)); sp = std::max (sp, std::abs (v)); }
    NEAR (20 * std::log10 (sp), -3.01, 0.05, "fs/4 sine 45deg: sample peak");
    NEAR (20 * std::log10 (pk), 0.0, 0.25, "fs/4 sine 45deg: true peak (4x)");
    NEAR (referenceTruePeakDb (x), 0.0, 0.05, "fs/4 sine 45deg: reference 16x TP");

    Buf y ((size_t) sr); sine (y, sr, 997, std::pow (10.0, -6.0 / 20.0));
    dsp::TruePeakDetector tp2; float pk2 = 0;
    for (auto v : y) pk2 = std::max (pk2, tp2.push (v));
    NEAR (20 * std::log10 (pk2), -6.0, 0.05, "997 Hz -6 dBFS: true peak");
}

static void testDetectorVsReference()
{
    std::printf ("\n[True-peak detector vs 16x reference, white noise band-limited to 20 kHz]\n");
    Buf x (48000); white (x, 0.3); bandLimit (x, 48000, 20000);
    dsp::TruePeakDetectorT<4> t4; dsp::TruePeakDetectorT<8> t8; float p4 = 0, p8 = 0;
    for (auto v : x) { p4 = std::max (p4, t4.push (v)); p8 = std::max (p8, t8.push (v)); }
    const double ref = referenceTruePeakDb (x);
    NEAR (20 * std::log10 (p4), ref, 0.5, "4x detector vs reference");
    NEAR (20 * std::log10 (p8), ref, 0.15, "8x detector vs reference");
    dsp::Maximizer mx; dsp::MaximizerParams pr; pr.enabled = true; pr.driveDb = 14; pr.ceilingDb = -1; pr.releaseMs = 80;
    mx.setParams (pr); mx.prepare (48000, 512);
    Buf l = x, r (x.size()); white (r, 0.3); bandLimit (r, 48000, 20000);
    blocks (l.size(), 512, [&] (size_t p, int n) { mx.process (l.data() + p, r.data() + p, n); });
    float sp = 0; int clamped = 0;
    for (size_t i = 2000; i < l.size(); ++i) { sp = std::max (sp, std::abs (l[i])); if (std::abs (l[i]) >= 0.89125f) ++clamped; }
    std::printf ("  info limited white noise: sample peak %.3f dB, samples at clamp %d, ref TP %.3f dBTP\n",
                 20 * std::log10 (sp), clamped, referenceTruePeakDb (l));
}

static void testEq()
{
    std::printf ("\n[Parametric EQ]\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        dsp::ParametricEQ eq;
        dsp::EqBandParams b; b.enabled = true; b.type = (int) dsp::FilterType::Bell; b.freq = 1000; b.gainDb = 6; b.q = 1;
        eq.setBand (0, b); eq.setEnabled (true); eq.prepare (sr, 512);
        Buf l ((size_t) sr), r; sine (l, sr, 1000, 0.25); r = l;
        const double before = rmsDb (l, (size_t) (sr / 2));
        for (size_t p = 0; p < l.size(); p += 512) eq.process (l.data() + p, r.data() + p, (int) std::min<size_t> (512, l.size() - p));
        NEAR (rmsDb (l, (size_t) (sr / 2)) - before, 6.0, 0.05, (juce::String ("bell +6 dB @1k gain, sr ") + juce::String (sr, 0)).toRawUTF8());
        NEAR (dsp::ParametricEQ::bandResponseDb (b, 1000, sr), 6.0, 0.01, "bell analytic response @1k");
        NEAR (dsp::ParametricEQ::bandResponseDb (b, 100, sr), 0.0, 0.2, "bell analytic response @100 Hz");
    }
    {
        // fully disabled -> bit-exact
        dsp::ParametricEQ eq; eq.setEnabled (false); eq.prepare (48000, 512);
        Buf l (48000), r (48000); white (l, 0.3); white (r, 0.3);
        Buf l0 = l, r0 = r;
        for (size_t p = 0; p < l.size(); p += 512) eq.process (l.data() + p, r.data() + p, 512 > (int) (l.size() - p) ? (int) (l.size() - p) : 512);
        CHECK (l == l0 && r == r0, "EQ disabled must be bit-exact");
        std::printf ("  ok   EQ disabled is bit-exact\n");
    }
    {
        // side-only band on a mono signal must not change anything
        dsp::ParametricEQ eq;
        dsp::EqBandParams b; b.enabled = true; b.type = (int) dsp::FilterType::Bell; b.freq = 300; b.gainDb = -8; b.q = 0.7f; b.channel = 2;
        eq.setBand (0, b); eq.setEnabled (true); eq.prepare (48000, 512);
        Buf l (48000); white (l, 0.3); Buf r = l, l0 = l;
        blocks (l.size(), 512, [&] (size_t p, int n) { eq.process (l.data() + p, r.data() + p, n); });
        double maxDiff = 0; for (size_t i = 0; i < l.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (l[i] - l0[i]));
        CHECK (maxDiff < 1e-6, "side band changed mono signal by %g", maxDiff);
        std::printf ("  ok   Side-only band leaves mono signal untouched (max diff %.2g)\n", maxDiff);
    }
    {
        // parameter sweep stays finite (stability under automation)
        dsp::ParametricEQ eq; eq.setEnabled (true); eq.prepare (44100, 64);
        Buf l (44100 * 4), r; white (l, 0.5); r = l;
        bool finite = true;
        for (size_t p = 0, k = 0; p + 64 <= l.size(); p += 64, ++k)
        {
            for (int i = 0; i < 8; ++i)
            {
                dsp::EqBandParams b; b.enabled = true; b.type = (int) ((k / 50 + (size_t) i) % 6);
                b.freq = 20.0f * std::pow (1000.0f, (float) ((k * 7 + (size_t) i * 13) % 100) / 100.0f);
                b.gainDb = (float) ((int) (k % 37) - 18); b.q = 0.1f + (float) ((k + (size_t) i) % 50) * 0.3f; b.channel = (int) (k % 3);
                eq.setBand (i, b);
            }
            eq.process (l.data() + p, r.data() + p, 64);
            for (size_t i = p; i < p + 64; ++i) finite = finite && std::isfinite (l[i]) && std::abs (l[i]) < 100.0f;
        }
        CHECK (finite, "EQ became unstable during automation sweep");
        std::printf ("  ok   EQ stable under aggressive automation sweep\n");
    }
}

static void testImager()
{
    std::printf ("\n[Stereo imager]\n");
    const double sr = 48000;
    {
        // 100 % widths: magnitude-flat (impulse response spectrum)
        dsp::StereoImager im; im.setEnabled (true); im.setCrossovers (120, 1000, 6000); im.setWidths ({ 100, 100, 100, 100 });
        im.prepare (sr, 4096);
        Buf l (8192, 0.0f), r (8192, 0.0f); l[0] = 1.0f; r[0] = 0.3f;
        im.process (l.data(), r.data(), 8192);
        juce::dsp::FFT fft (13);
        std::vector<float> buf (16384, 0.0f); std::copy (l.begin(), l.end(), buf.begin());
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double mn = 1e9, mx = -1e9;
        for (int b = 3; b < 4000; ++b) { const double db = 20 * std::log10 (buf[(size_t) b] + 1e-12); mn = std::min (mn, db); mx = std::max (mx, db); }
        NEAR (mx - mn, 0.0, 0.05, "imager @100%: magnitude ripple (dB)");
    }
    {
        // width 0 on all bands -> L == R
        dsp::StereoImager im; im.setEnabled (true); im.setWidths ({ 0, 0, 0, 0 }); im.prepare (sr, 512);
        Buf l ((size_t) sr), r ((size_t) sr); white (l, 0.3); white (r, 0.3);
        blocks (l.size(), 512, [&] (size_t p, int n) { im.process (l.data() + p, r.data() + p, n); });
        double maxDiff = 0; for (size_t i = (size_t) (sr / 2); i < l.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (l[i] - r[i]));
        CHECK (maxDiff < 1e-4, "width 0 should give mono, diff %g", maxDiff);
        std::printf ("  ok   width 0%% on all bands collapses to mono (max L-R %.2g)\n", maxDiff);
    }
    {
        // low band to mono, measure with the analyser
        // pink noise (music-like spectrum; white noise leaks too much 120-300 Hz energy through the LR4 skirt)
        dsp::StereoImager im; im.setEnabled (true); im.setWidths ({ 0, 100, 100, 100 }); im.prepare (sr, 512);
        Buf l ((size_t) sr * 6), r ((size_t) sr * 6); pink (l, 0.2); pink (r, 0.2);
        blocks (l.size(), 512, [&] (size_t p, int n) { im.process (l.data() + p, r.data() + p, n); });
        MixAccumulator acc; acc.prepare (sr, { 120, 1000, 6000 }); acc.process (l.data(), r.data(), (int) l.size());
        const auto st = acc.computeStats();
        CHECK (st.bandWidthPct[0] < 15.0f, "low band width after narrowing %.1f", st.bandWidthPct[0]);
        CHECK (st.bandWidthPct[3] > 85.0f, "top band width untouched %.1f", st.bandWidthPct[3]);
        std::printf ("  ok   low band narrowed: width low %.1f%%, top %.1f%%\n", st.bandWidthPct[0], st.bandWidthPct[3]);
    }
    {
        dsp::StereoImager im; im.setEnabled (false); im.prepare (sr, 512);
        Buf l (48000), r (48000); white (l, 0.3); white (r, 0.3); Buf l0 = l, r0 = r;
        blocks (l.size(), 512, [&] (size_t p, int n) { im.process (l.data() + p, r.data() + p, n); });
        CHECK (l == l0 && r == r0, "imager off must be bit-exact");
        std::printf ("  ok   imager disabled is bit-exact\n");
    }
}

static void testMaximizer()
{
    std::printf ("\n[Maximizer]\n");
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        dsp::Maximizer mx;
        dsp::MaximizerParams p; p.enabled = true; p.driveDb = 14; p.ceilingDb = -1.0f; p.releaseMs = 80;
        mx.setParams (p); mx.prepare (sr, 512);
        Buf l ((size_t) (sr * 4)), r ((size_t) (sr * 4)); pink (l, 0.3); pink (r, 0.3);
        // add sharp transients
        for (size_t i = 0; i < l.size(); i += (size_t) (sr / 2)) for (int k = 0; k < 200 && i + (size_t) k < l.size(); ++k) { l[i + (size_t) k] += 0.8f * std::exp (-k / 40.0f); r[i + (size_t) k] -= 0.7f * std::exp (-k / 40.0f); }
        bandLimit (l, sr); bandLimit (r, sr);   // programme material is band-limited (~18 kHz)
        for (size_t pos = 0; pos < l.size(); pos += 512) mx.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (512, l.size() - pos));
        const double tpL = referenceTruePeakDb (l), tpR = referenceTruePeakDb (r);
        CHECK (std::max (tpL, tpR) <= -1.0 + 0.1, "TP after limiting %.3f / %.3f", tpL, tpR);
        dsp::TruePeakDetector m4; float pk = 0; for (size_t i = 0; i < l.size(); ++i) pk = std::max (pk, std::max (m4.push (l[i]), 0.0f));
        dsp::TruePeakDetector m4r; for (size_t i = 0; i < r.size(); ++i) pk = std::max (pk, m4r.push (r[i]));
        CHECK (20 * std::log10 (pk) <= -1.0 + 0.02, "ITU-style 4x meter reads %.3f", 20 * std::log10 (pk));
        std::printf ("  ok   +14 dB drive, ceiling -1 dBTP @ %.0f: reference TP L %.2f R %.2f dBTP\n", sr, tpL, tpR);
    }
    {
        // off -> pure delay of exactly getLatencySamples()
        dsp::Maximizer mx; dsp::MaximizerParams p; p.enabled = false; mx.setParams (p); mx.prepare (48000, 512);
        const int lat = mx.getLatencySamples();
        Buf l (48000), r (48000); white (l, 0.3); white (r, 0.3); Buf l0 = l, r0 = r;
        for (size_t pos = 0; pos < l.size(); pos += 512) mx.process (l.data() + pos, r.data() + pos, 512 > (int) (l.size() - pos) ? (int) (l.size() - pos) : 512);
        bool exact = true; for (size_t i = (size_t) lat; i < l.size(); ++i) exact = exact && l[i] == l0[i - (size_t) lat] && r[i] == r0[i - (size_t) lat];
        CHECK (exact, "maximizer off must be an exact delay of %d samples", lat);
        std::printf ("  ok   maximizer disabled = exact %d-sample delay (%.2f ms)\n", lat, lat / 48.0);
    }
    {
        // on, drive 0, quiet signal -> transparent delay (no limiting)
        dsp::Maximizer mx; dsp::MaximizerParams p; p.enabled = true; p.driveDb = 0; p.ceilingDb = -1; mx.setParams (p); mx.prepare (48000, 512);
        const int lat = mx.getLatencySamples();
        Buf l (48000), r (48000); sine (l, 48000, 440, 0.1); r = l; Buf l0 = l;
        for (size_t pos = 0; pos < l.size(); pos += 512) mx.process (l.data() + pos, r.data() + pos, 512 > (int) (l.size() - pos) ? (int) (l.size() - pos) : 512);
        double maxDiff = 0; for (size_t i = (size_t) lat + 2000; i < l.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (l[i] - l0[i - (size_t) lat]));
        CHECK (maxDiff < 1e-5, "unity limiter changed quiet signal by %g", maxDiff);
        std::printf ("  ok   maximizer on, no limiting needed: transparent (max diff %.2g)\n", maxDiff);
    }
    {
        // clipper path keeps latency constant and lowers limiter work
        dsp::Maximizer a, b; dsp::MaximizerParams p; p.enabled = true; p.driveDb = 10; p.ceilingDb = -1;
        a.setParams (p); p.clipPct = 60; b.setParams (p);
        a.prepare (48000, 512); b.prepare (48000, 512);
        CHECK (a.getLatencySamples() == b.getLatencySamples(), "latency must not depend on clipper");
        Buf l (48000 * 3), r; pink (l, 0.3); r = l; Buf l2 = l, r2 = r;
        float grA = 0, grB = 0;
        for (size_t pos = 0; pos + 512 <= l.size(); pos += 512)
        {
            a.process (l.data() + pos, r.data() + pos, 512); grA = std::min (grA, a.getBlockGainReductionDb());
            b.process (l2.data() + pos, r2.data() + pos, 512); grB = std::min (grB, b.getBlockGainReductionDb());
        }
        CHECK (grB > grA, "clipper should reduce limiter GR (%.2f vs %.2f)", grB, grA);
        std::printf ("  ok   clipper 60%%: max limiter GR %.2f dB (vs %.2f dB without)\n", grB, grA);
    }
}

static void testAccumulator()
{
    std::printf ("\n[Analyser measurements]\n");
    const double sr = 48000;
    auto stats = [&] (const Buf& l, const Buf& r) { MixAccumulator a; a.prepare (sr, { 120, 1000, 6000 }); a.process (l.data(), r.data(), (int) l.size()); return a.computeStats(); };
    {
        Buf z ((size_t) sr * 5, 0.0f);
        auto s = stats (z, z);
        CHECK (! s.isValid(), "silence must be invalid");
        auto rep = diagnostics::diagnose (s, {});
        CHECK (! rep.valid && rep.problems.empty(), "silence report");
        std::printf ("  ok   silence -> no report, no problems, no NaN (I=%.1f)\n", s.integrated);
    }
    {
        Buf l ((size_t) sr * 10), r ((size_t) sr * 10); white (l, 0.1); white (r, 0.1);
        auto s = stats (l, r);
        NEAR (s.correlation, 0.0, 0.03, "uncorrelated noise: correlation");
        NEAR (s.widthPct, 100.0, 4.0, "uncorrelated noise: width %");
        NEAR (s.monoLossDb, -3.01, 0.15, "uncorrelated noise: mono loss dB");
    }
    {
        Buf l ((size_t) sr * 10); pink (l, 0.2); Buf r = l;
        auto s = stats (l, r);
        NEAR (s.correlation, 1.0, 0.001, "mono: correlation");
        NEAR (s.widthPct, 0.0, 0.5, "mono: width");
        auto rel = diagnostics::relativeBands (s.sixth);
        float mn = 100, mx = -100;
        for (int i = 0; i < BandGrid::kSixthBands; ++i)
        {
            const float f = BandGrid::sixthCentre (i);
            if (f >= 40 && f <= 16000) { mn = std::min (mn, rel[(size_t) i]); mx = std::max (mx, rel[(size_t) i]); }
        }
        NEAR (mx - mn, 0.0, 3.0, "pink noise flat in 1/6-oct bands (spread dB)");
    }
    {
        Buf l ((size_t) sr * 10); pink (l, 0.2); Buf r = l; for (auto& v : r) v = -v;
        auto s = stats (l, r);
        NEAR (s.correlation, -1.0, 0.001, "antiphase: correlation");
        auto rep = diagnostics::diagnose (s, {});
        bool phase = false; for (auto& p : rep.problems) phase = phase || p.id == "phase";
        CHECK (phase, "antiphase must report a phase problem");
        std::printf ("  ok   antiphase -> phase problem reported (score %d)\n", rep.score);
    }
    {
        // L 3 dB louder
        Buf l ((size_t) sr * 8), r ((size_t) sr * 8); white (l, 0.1); white (r, 0.1);
        for (auto& v : l) v *= 1.4125f;
        auto s = stats (l, r);
        NEAR (s.balanceDb, -3.0, 0.15, "balance: L +3 dB -> -3 dB");
    }
    {
        Buf l ((size_t) sr * 10); sine (l, sr, 220, 2.0);
        for (auto& v : l) v = std::clamp (v, -1.0f, 1.0f);
        Buf r = l;
        auto s = stats (l, r);
        CHECK (s.clipEvents > 100, "hard-clipped sine: clip events %d", s.clipEvents);
        std::printf ("  ok   hard-clipped sine: %d clip events (%.0f/min)\n", s.clipEvents, s.clipEventsPerMinute);
        Buf c ((size_t) sr * 10); sine (c, sr, 220, 0.9); Buf c2 = c;
        auto s2 = stats (c, c2);
        CHECK (s2.clipEvents == 0, "clean sine: clip events %d", s2.clipEvents);
        std::printf ("  ok   clean sine at -0.9 dBFS: %d clip events\n", s2.clipEvents);
    }
    {
        // transient-heavy vs compressed: punch / micro-dynamics ordering
        Buf a ((size_t) sr * 10, 0.0f), b ((size_t) sr * 10);
        for (size_t i = 0; i < a.size(); i += (size_t) (sr / 4)) for (int k = 0; k < 4000 && i + (size_t) k < a.size(); ++k) a[i + (size_t) k] = 0.9f * std::exp (-k / 600.0f) * std::sin (2 * (float) PI * 60 * k / (float) sr);
        Buf noise ((size_t) sr * 10); pink (noise, 0.02); for (size_t i = 0; i < a.size(); ++i) a[i] += noise[i];
        pink (b, 0.25); for (auto& v : b) v = std::tanh (v * 3.0f) * 0.5f;
        auto sa = stats (a, a), sb = stats (b, b);
        CHECK (sa.microDynamicsDb > sb.microDynamicsDb + 3, "micro-dynamics transient %.1f vs squashed %.1f", sa.microDynamicsDb, sb.microDynamicsDb);
        CHECK (sa.onsets > 30, "onsets detected %d", sa.onsets);
        std::printf ("  ok   transients: micro-dyn %.1f dB vs squashed %.1f dB, onsets %d\n", sa.microDynamicsDb, sb.microDynamicsDb, sa.onsets);
    }
    {
        // bass-heavy material -> bass problem and a low cut / shelf suggestion with negative gain
        Buf l ((size_t) sr * 12), r ((size_t) sr * 12); pink (l, 0.1); pink (r, 0.1);
        dsp::ParametricEQ eq; dsp::EqBandParams b; b.enabled = true; b.type = (int) dsp::FilterType::LowShelf; b.freq = 150; b.gainDb = 12; b.q = 0.7f;
        eq.setBand (0, b); eq.setEnabled (true); eq.prepare (sr, 512);
        for (size_t p = 0; p < l.size(); p += 512) eq.process (l.data() + p, r.data() + p, 512 > (int) (l.size() - p) ? (int) (l.size() - p) : 512);
        auto s = stats (l, r);
        DiagnosticsContext ctx; ctx.genreChoice = genreIndexForKey ("pop");
        auto rep = diagnostics::diagnose (s, ctx);
        bool bass = false; for (auto& p : rep.problems) bass = bass || p.id.startsWith ("bass_much") || p.id.startsWith ("sub_much");
        bool cut = false; for (auto& e : rep.eq) cut = cut || (e.gainDb < 0 && e.freq < 250);
        CHECK (bass && cut, "bass boost not detected (problems %d, eq %d)", (int) rep.problems.size(), (int) rep.eq.size());
        std::printf ("  ok   +12 dB low shelf detected: score %d, %d problems, first EQ %s %.0f Hz %.1f dB\n", rep.score, (int) rep.problems.size(),
                     rep.eq.empty() ? "-" : "band", rep.eq.empty() ? 0.0 : rep.eq.front().freq, rep.eq.empty() ? 0.0 : rep.eq.front().gainDb);

        // no stacked / opposing duplicates in the recommendation set
        for (size_t a = 0; a < rep.eq.size(); ++a)
            for (size_t b2 = a + 1; b2 < rep.eq.size(); ++b2)
                CHECK (! (rep.eq[a].type == rep.eq[b2].type && rep.eq[a].channel == rep.eq[b2].channel && std::abs (std::log2 (rep.eq[a].freq / rep.eq[b2].freq)) < 0.5f),
                       "duplicate EQ suggestions %.0f / %.0f Hz", rep.eq[a].freq, rep.eq[b2].freq);
        for (auto& e : rep.eq) CHECK (e.gainDb <= 4.0f && e.gainDb >= -6.0f, "suggestion gain within gentle limits (%.1f)", e.gainDb);

        // applying the suggestion should reduce the deviation (closed loop)
        dsp::ParametricEQ fix; int i = 0;
        for (auto& e : rep.eq) { dsp::EqBandParams p; p.enabled = true; p.type = e.type; p.freq = e.freq; p.gainDb = e.gainDb; p.q = e.q; p.channel = e.channel; fix.setBand (i++, p); }
        fix.setEnabled (true); fix.prepare (sr, 512);
        for (size_t p = 0; p < l.size(); p += 512) fix.process (l.data() + p, r.data() + p, 512 > (int) (l.size() - p) ? (int) (l.size() - p) : 512);
        auto s2 = stats (l, r);
        auto rep2 = diagnostics::diagnose (s2, ctx);
        CHECK (rep2.subscores[0].value > rep.subscores[0].value, "tonal score should improve %d -> %d", rep.subscores[0].value, rep2.subscores[0].value);
        std::printf ("  ok   applying recommendations improves tonal score %d -> %d (total %d -> %d)\n",
                     rep.subscores[0].value, rep2.subscores[0].value, rep.score, rep2.score);
    }
    {
        // sample-rate independence of the measurements
        float lufs[3]; int k = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            Buf l ((size_t) (rate * 6)); sine (l, rate, 1000, 0.1); Buf r = l;
            MixAccumulator a; a.prepare (rate, { 120, 1000, 6000 }); a.process (l.data(), r.data(), (int) l.size());
            lufs[k++] = a.computeStats().integrated;
        }
        CHECK (std::abs (lufs[0] - lufs[2]) < 0.05f && std::abs (lufs[1] - lufs[2]) < 0.05f, "rate dependence %.3f %.3f %.3f", lufs[0], lufs[1], lufs[2]);
        std::printf ("  ok   same LUFS at 44.1/48/96 kHz: %.3f %.3f %.3f\n", lufs[0], lufs[1], lufs[2]);
    }
}

static void testPlannerAndScore()
{
    std::printf ("\n[Loudness planner]\n");
    const double sr = 48000;
    // music-like: pink bed + kick-like transients, 40 s with a quieter middle section
    Buf l ((size_t) sr * 40), r ((size_t) sr * 40), nl ((size_t) sr * 40), nr ((size_t) sr * 40);
    pink (nl, 0.12); pink (nr, 0.12);
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double t = (double) i / sr;
        const double sect = (t > 15 && t < 25) ? 0.5 : 1.0;
        const double beat = std::fmod (t, 0.5);
        const double kick = 0.3 * std::exp (-beat * 25) * std::sin (2 * PI * (50 + 80 * std::exp (-beat * 40)) * beat);
        l[i] = (float) (sect * (nl[i] + kick)); r[i] = (float) (sect * (nr[i] + kick));
    }
    for (auto [target, clip] : std::initializer_list<std::pair<float, float>> { { -9.0f, 0.0f }, { -7.0f, 0.0f }, { -5.0f, 50.0f } })
    {
        LoudnessPlanner pl; pl.prepare (sr);
        for (size_t p = 0; p < l.size(); p += 1024) pl.addAudio (l.data() + p, r.data() + p, (int) std::min<size_t> (1024, l.size() - p));
        LoudnessPlanner::Request req; req.targetLufs = target; req.ceilingDb = -1; req.releaseMs = 100; req.clipPct = clip;
        CHECK (pl.requestPlan (req), "plan request accepted");
        while (pl.isBusy()) juce::Thread::sleep (20);
        auto res = pl.getResult();
        // verify against the real processor on the whole signal
        dsp::Maximizer mx; dsp::MaximizerParams p; p.enabled = true; p.driveDb = res.driveDb; p.ceilingDb = -1; p.releaseMs = 100; p.clipPct = clip;
        mx.setParams (p); mx.prepare (sr, 512);
        Buf ol = l, orr = r;
        for (size_t pos = 0; pos < ol.size(); pos += 512) mx.process (ol.data() + pos, orr.data() + pos, 512 > (int) (ol.size() - pos) ? (int) (ol.size() - pos) : 512);
        const float achieved = measureLufs (ol, orr, sr);
        NEAR (achieved, res.predictedLufs, 0.5, (juce::String ("planner prediction vs real, target ") + juce::String (target, 0)).toRawUTF8());
        if (res.error.isEmpty())
            NEAR (achieved, target, 0.6, (juce::String ("planner target ") + juce::String (target, 0) + " LUFS -> achieved").toRawUTF8());
        else
            std::printf ("       note: %s\n", res.error.toRawUTF8());
        std::printf ("       clip %.0f%%: source %.2f LUFS, drive %+.1f dB, predicted %.2f, avg GR %.1f dB, risk %d, safe target %.1f, TP %.2f\n",
                     clip, res.sourceLufs, res.driveDb, res.predictedLufs, res.avgGrDb, res.risk, res.safeTargetLufs, std::max (referenceTruePeakDb (ol), referenceTruePeakDb (orr)));
    }
}

static void testSerialization()
{
    std::printf ("\n[Report serialisation]\n");
    Buf l (48000 * 8), r (48000 * 8); pink (l, 0.2); pink (r, 0.2);
    MixAccumulator a; a.prepare (48000, { 120, 1000, 6000 }); a.process (l.data(), r.data(), (int) l.size());
    auto rep = diagnostics::diagnose (a.computeStats(), {});
    auto back = Report::fromValueTree (rep.toValueTree());
    CHECK (back.valid && back.score == rep.score && back.problems.size() == rep.problems.size() && back.eq.size() == rep.eq.size(), "roundtrip");
    NEAR (back.stats.integrated, rep.stats.integrated, 0.01, "roundtrip integrated");
    CHECK (back.verdict == rep.verdict, "verdict text (UTF-8) survives roundtrip");
    std::printf ("  ok   report roundtrip: score %d, %d problems, verdict \"%s\"\n", back.score, (int) back.problems.size(), back.verdict.toRawUTF8());
}

static void testEngine()
{
    std::printf ("\n[Analysis engine end-to-end]\n");
    AnalysisEngine eng;
    std::atomic<int> got { 0 }; ReportPtr lastRep;
    std::mutex m;
    eng.setReportCallback ([&] (ReportPtr r, bool final) { std::lock_guard<std::mutex> g (m); lastRep = r; if (final) ++got; });
    eng.setMode (AnalysisMode::Smart);
    const double sr = 48000; eng.prepare (sr, 512);
    juce::Thread::sleep (50);
    Buf l (512), r (512);
    const int blocks = (int) (sr * 25 / 512);
    for (int b = 0; b < blocks; ++b)
    {
        pink (l, 0.15); pink (r, 0.15);
        eng.pushAudio (l.data(), r.data(), l.data(), r.data(), l.data(), r.data(), 512, true, true);
        juce::Thread::sleep (1);   // ~10x faster than real time; lets the FIFO drain
    }
    LiveData live; eng.getLive (live);
    CHECK (eng.readyCounter() >= 1 && eng.reportCounter() >= 1, "smart mode should be ready (with a report) during playback (state %d)", (int) live.state);
    std::printf ("  ok   smart mode ready during playback: %d ready event(s), %d report(s), %.1f s heard, FIFO drops %d\n",
                 eng.readyCounter(), eng.reportCounter(), live.activeSeconds, live.fifoDrops);
    for (int b = 0; b < 40; ++b) { std::fill (l.begin(), l.end(), 0.0f); eng.pushAudio (l.data(), l.data(), l.data(), l.data(), l.data(), l.data(), 512, false, true); juce::Thread::sleep (10); }
    juce::Thread::sleep (300);
    CHECK (got.load() >= 1 && lastRep != nullptr && lastRep->valid, "final report after stop");
    if (lastRep) std::printf ("  ok   final report on transport stop: score %d, %.1f s analysed, genre guess %s\n", lastRep->score, lastRep->stats.secondsAnalysed, lastRep->genreGuess.toRawUTF8());
}

static void testReference()
{
    std::printf ("\n[Reference track]\n");
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mooni_ref_test.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        auto os = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.release(), 44100, 2, 24, {}, 0));
        juce::AudioBuffer<float> b (2, 44100 * 10);
        Buf l ((size_t) 44100 * 10); pink (l, 0.2);
        b.copyFrom (0, 0, l.data(), (int) l.size()); b.copyFrom (1, 0, l.data(), (int) l.size());
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    ReferenceTrack ref; ref.load (file, 48000);
    for (int i = 0; i < 400 && ref.getStatus() == ReferenceTrack::Status::Loading; ++i) juce::Thread::sleep (20);
    CHECK (ref.getStatus() == ReferenceTrack::Status::Ready, "reference ready (%d) %s", (int) ref.getStatus(), ref.getError().toRawUTF8());
    if (auto st = ref.getStats())
    {
        std::printf ("  ok   reference 44.1 kHz WAV analysed: %.2f LUFS, width %.1f%%, %.1f s\n", st->integrated, st->widthPct, st->secondsTotal);
        Buf l (512), r (512);
        CHECK (ref.render (l.data(), r.data(), 512, 1.0f), "render reference");
    }
    file.deleteFile();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;  // message manager for juce::Thread utilities
    std::printf ("Mooni Analyzer test runner\n");
    testLoudness();
    testTruePeak();
    testDetectorVsReference();
    testEq();
    testImager();
    testMaximizer();
    testAccumulator();
    testPlannerAndScore();
    testSerialization();
    testEngine();
    testReference();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
