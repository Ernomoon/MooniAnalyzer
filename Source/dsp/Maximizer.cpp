#include "Maximizer.h"

namespace mooni::dsp
{
static inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

void Maximizer::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlockSize = std::max (1, maxBlockSize);

    look = std::max (16, (int) std::round (0.0015 * sr));            // 1.5 ms look-ahead
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxBlockSize);
    osLatency = (int) std::round (oversampler->getLatencyInSamples());

    // the detector reports peaks around input sample (n - kCenter); see the gain-computer notes
    const int detectorDelay = TruePeakDetectorT<8>::kCenter;
    latency = osLatency + look - 1 + detectorDelay;

    clipDryDelay.prepare (osLatency);
    mainDelay.prepare (look - 1 + detectorDelay);
    bypassDelay.prepare (latency);

    holdLen = look + 2;
    wedgeVal.assign ((size_t) holdLen + 2, 1.0f);
    wedgeIdx.assign ((size_t) holdLen + 2, 0);
    boxBuf.assign ((size_t) look, 1.0);

    clipL.assign ((size_t) maxBlockSize, 0.0f);
    clipR.assign ((size_t) maxBlockSize, 0.0f);
    dryL.assign ((size_t) maxBlockSize, 0.0f);
    dryR.assign ((size_t) maxBlockSize, 0.0f);

    drive.setTime (0.05, sr);     drive.reset (dbToGain (params.driveDb));
    ceiling.setTime (0.05, sr);   ceiling.reset (dbToGain (params.ceilingDb));
    onMix.setLength (0.03, sr);   onMix.reset (params.enabled ? 1.0f : 0.0f);
    clipMix.setLength (0.03, sr); clipMix.reset (params.clipPct > 0.0f ? 1.0f : 0.0f);
    reset();
}

void Maximizer::reset()
{
    if (oversampler) oversampler->reset();
    clipDryDelay.reset(); mainDelay.reset(); bypassDelay.reset();
    tpL.reset(); tpR.reset();
    wedgeHead = 0; wedgeSize = 0; sampleIndex = 0;
    std::fill (boxBuf.begin(), boxBuf.end(), 1.0); boxPos = 0; boxSum = (double) look;
    env = 1.0;
    lastBlockGrDb = lastBlockClipDb = 0.0f;
}

void Maximizer::processClipper (float* l, float* r, int n) noexcept
{
    // dry (delay-compensated) copy
    for (int i = 0; i < n; ++i) { clipL[(size_t) i] = l[i]; clipR[(size_t) i] = r[i]; clipDryDelay.process (clipL[(size_t) i], clipR[(size_t) i]); }

    clipMix.target = params.clipPct > 0.0f ? 1.0f : 0.0f;
    if (! clipMix.isActive() && clipMix.value <= 0.0f)
    {
        std::copy (clipL.begin(), clipL.begin() + n, l);
        std::copy (clipR.begin(), clipR.begin() + n, r);
        lastBlockClipDb = 0.0f;
        return;
    }

    if (clipMix.value <= 0.0f)
        oversampler->reset();      // waking up from idle: start from a clean filter state

    float* chans[2] = { l, r };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    auto up = oversampler->processSamplesUp (block);
    const float ceil = (float) ceiling.value;
    const float thr = ceil * (float) dbToGain ((1.0 - params.clipPct / 100.0) * 6.0);
    float peakIn = 0, peakOut = 0;
    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        auto* d = up.getChannelPointer (ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            peakIn = std::max (peakIn, std::abs (d[i]));
            d[i] = softClip (d[i], thr);
            peakOut = std::max (peakOut, std::abs (d[i]));
        }
    }
    oversampler->processSamplesDown (block);
    lastBlockClipDb = (peakIn > 1e-9f && peakOut > 1e-9f) ? (float) (20.0 * std::log10 (peakOut / peakIn)) : 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float g = clipMix.next();
        l[i] = clipL[(size_t) i] + g * (l[i] - clipL[(size_t) i]);
        r[i] = clipR[(size_t) i] + g * (r[i] - clipR[(size_t) i]);
    }
}

void Maximizer::process (float* left, float* right, int numSamples) noexcept
{
    onMix.target = params.enabled ? 1.0f : 0.0f;
    const double releaseCoeff = 1.0 - std::exp (-1.0 / (std::max (1.0f, params.releaseMs) * 0.001 * sr));
    drive.target = dbToGain (params.driveDb);
    ceiling.target = dbToGain (std::min (0.0f, params.ceilingDb));

    // dry path, delayed by the full latency (used for bypass and cross-fades)
    for (int i = 0; i < numSamples; ++i)
    {
        dryL[(size_t) i] = left[i]; dryR[(size_t) i] = right[i];
        bypassDelay.process (dryL[(size_t) i], dryR[(size_t) i]);
    }

    if (! params.enabled && ! onMix.isActive() && onMix.value <= 0.0f)
    {
        std::copy (dryL.begin(), dryL.begin() + numSamples, left);
        std::copy (dryR.begin(), dryR.begin() + numSamples, right);
        lastBlockGrDb = lastBlockClipDb = 0.0f;
        return;
    }

    // 1) drive
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = (float) drive.next();
        left[i] *= g; right[i] *= g;
    }
    // ceiling smoothing runs once per block (slow parameter)
    for (int i = 0; i < numSamples; ++i) ceiling.next();

    // 2) optional soft clipper (constant latency either way)
    processClipper (left, right, numSamples);

    // 3) true-peak look-ahead limiter
    const double ceil = ceiling.value * 0.9977; // 0.02 dB safety for interpolation error
    double minEnv = 1.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float pk = std::max (tpL.push (left[i]), tpR.push (right[i]));
        const float target = pk > ceil ? (float) (ceil / pk) : 1.0f;

        const float held = pushHold (target);

        boxSum += (double) held - boxBuf[(size_t) boxPos];
        boxBuf[(size_t) boxPos] = held;
        boxPos = (boxPos + 1) % look;
        const double box = std::min (1.0, boxSum / look);

        if (box < env) env = box;
        else           env += (box - env) * releaseCoeff;
        minEnv = std::min (minEnv, env);

        float a = left[i], b = right[i];
        mainDelay.process (a, b);
        a *= (float) env; b *= (float) env;
        // final safety: sample-domain clamp at the ceiling (only catches pathological residue)
        const float c = (float) ceiling.value;
        a = std::clamp (a, -c, c); b = std::clamp (b, -c, c);

        const float m = onMix.next();
        left[i]  = dryL[(size_t) i] + m * (a - dryL[(size_t) i]);
        right[i] = dryR[(size_t) i] + m * (b - dryR[(size_t) i]);
    }
    // keep box sum numerically honest
    double s = 0; for (auto v : boxBuf) s += v; boxSum = s;
    lastBlockGrDb = (float) (20.0 * std::log10 (std::max (1.0e-6, minEnv)));
}
} // namespace mooni::dsp
