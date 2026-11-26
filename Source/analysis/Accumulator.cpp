#include "Accumulator.h"
#include <numeric>

namespace mooni
{
static inline float toDb (double power) { return (float) (10.0 * std::log10 (std::max (power, 1.0e-20))); }
static inline float ampDb (double a)    { return (float) (20.0 * std::log10 (std::max (a, 1.0e-10))); }

static const float* sixthCentres()
{
    static const auto table = []
    {
        std::array<float, BandGrid::kSixthBands> c {};
        for (int i = 0; i < BandGrid::kSixthBands; ++i) c[(size_t) i] = BandGrid::sixthCentre (i);
        return c;
    }();
    return table.data();
}

MixAccumulator::MixAccumulator()
{
    fft = std::make_unique<juce::dsp::FFT> (kFftOrder);
    window.resize (kFftSize);
    double wsum = 0;
    for (int i = 0; i < kFftSize; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / kFftSize));
        wsum += window[(size_t) i];
    }
    windowNorm = (wsum * 0.5) * (wsum * 0.5);   // full-scale sine -> 0 dB peak bin
    ringM.assign (kFftSize, 0); ringS.assign (kFftSize, 0); fftBuf.assign (kFftSize * 2, 0);
    sumPower.assign (kBins, 0); sumSide.assign (kBins, 0); livePower.assign (kBins, 0);
    kWeight.assign (kBins, 1.0);
}

void MixAccumulator::prepare (double sampleRate, std::array<float, 3> crossovers)
{
    sr = sampleRate;
    xo = crossovers;
    dsp::StereoImager::sortCrossovers (xo[0], xo[1], xo[2]);
    loud.prepare (sr);
    for (int b = 0; b < kBins; ++b)
        kWeight[(size_t) b] = loud.kWeightPower (std::max (1.0, b * sr / kFftSize));
    stepLen = std::max (1, (int) std::round (sr * 0.1));
    winLen = std::max (8, (int) std::round (sr * 0.01));
    lMid.design (sr, xo[1]); lLow.design (sr, xo[0]); lHigh.design (sr, xo[2]);
    rMid.design (sr, xo[1]); rLow.design (sr, xo[0]); rHigh.design (sr, xo[2]);
    reset();
}

void MixAccumulator::reset()
{
    loud.reset(); tpL.reset(); tpR.reset();
    lMid.reset(); lLow.reset(); lHigh.reset(); rMid.reset(); rLow.reset(); rHigh.reset();
    bLL.fill (0); bRR.fill (0); bLR.fill (0);
    LL = RR = LR = 0; stepLL = stepRR = stepLR = 0;
    corrSteps = negCorrSteps = 0; worstCorr = 1; lastStepCorr = 1; lastStepWidth = 0;
    maxSample = maxTp = stepPeak = stepTp = 0; lastStepPeakDb = lastStepTpDb = -100; recentStepPeaks.clear();
    activeSumSq = 0; activeCount = 0; sumL = sumR = 0; count = 0;
    truePeakOvers = 0; microSum = 0; microCount = 0; activeSteps = 0;
    prevL = prevR = 0; runL = runR = 0; flatRunHist.fill (0); flatTopRuns = 0;
    winFill = 0; winPeak = 0; winEnergy = 0; slowRms2 = 0; avgEnergy = 0;
    punchSum = 0; punchCount = 0; onsets = onsetsSinceRead = 0; samplesSinceOnset = 0;
    std::fill (ringM.begin(), ringM.end(), 0.0f); std::fill (ringS.begin(), ringS.end(), 0.0f);
    ringPos = 0; hopFill = 0;
    std::fill (sumPower.begin(), sumPower.end(), 0.0); std::fill (sumSide.begin(), sumSide.end(), 0.0);
    std::fill (livePower.begin(), livePower.end(), 0.0f);
    frames = 0; snaps.clear(); stepsSinceSnap = 0; stepFill = 0;
}

void MixAccumulator::endRun (float v, int run)
{
    if (run >= 3)
    {
        const float db = ampDb (std::abs (v));
        const int idx = (int) std::round (-db * 10.0f);
        if (idx >= 0 && idx < (int) flatRunHist.size()) ++flatRunHist[(size_t) idx];
        if (run >= 4) ++flatTopRuns;
    }
}

void MixAccumulator::process (const float* left, const float* right, int numSamples)
{
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = std::min (numSamples - offset, stepLen - stepFill);
        const float* L = left + offset; const float* R = right + offset;

        for (int i = 0; i < chunk; ++i)
        {
            const float l = L[i], r = R[i];
            const float al = std::abs (l), ar = std::abs (r);

            // peaks
            stepPeak = std::max (stepPeak, std::max (al, ar));
            stepTp = std::max (stepTp, std::max (tpL.push (l), tpR.push (r)));

            // DC & energy
            sumL += l; sumR += r; ++count;
            stepLL += (double) l * l; stepRR += (double) r * r; stepLR += (double) l * r;

            // flat-top runs (identical consecutive samples at significant level)
            if (l == prevL && al > 0.25f) ++runL; else { endRun (prevL, runL + 1); runL = 0; }
            if (r == prevR && ar > 0.25f) ++runR; else { endRun (prevR, runR + 1); runR = 0; }
            prevL = l; prevR = r;

            // per-band stereo statistics
            float l1, l2, l3, l4, r1, r2, r3, r4, lLo, lHi, rLo, rHi;
            lMid.split (l, lLo, lHi); lLow.split (lLo, l1, l2); lHigh.split (lHi, l3, l4);
            rMid.split (r, rLo, rHi); rLow.split (rLo, r1, r2); rHigh.split (rHi, r3, r4);
            const float lb[4] { l1, l2, l3, l4 }, rb[4] { r1, r2, r3, r4 };
            for (int b = 0; b < 4; ++b)
            {
                bLL[(size_t) b] += (double) lb[b] * lb[b];
                bRR[(size_t) b] += (double) rb[b] * rb[b];
                bLR[(size_t) b] += (double) lb[b] * rb[b];
            }

            // transients (10 ms windows vs 300 ms RMS)
            const double e = 0.5 * ((double) l * l + (double) r * r);
            winEnergy += e; winPeak = std::max (winPeak, std::max (al, ar));
            const double slowCoeff = 1.0 / (0.3 * sr);
            slowRms2 += (e - slowRms2) * slowCoeff;
            ++samplesSinceOnset;
            if (++winFill >= winLen)
            {
                const double wE = winEnergy / winLen;
                if (slowRms2 > 1.0e-5) // above ~ -50 dBFS
                {
                    punchSum += ampDb (winPeak) - toDb (slowRms2);
                    ++punchCount;
                }
                if (wE > 4.0 * avgEnergy && wE > 1.0e-4 && samplesSinceOnset > (int) (0.08 * sr))
                {
                    ++onsets; ++onsetsSinceRead; samplesSinceOnset = 0;
                }
                avgEnergy += (wE - avgEnergy) * 0.05;   // ~200 ms memory at 10 ms windows
                winEnergy = 0; winPeak = 0; winFill = 0;
            }

            // spectrum ring (mid / side)
            ringM[(size_t) ringPos] = 0.5f * (l + r);
            ringS[(size_t) ringPos] = 0.5f * (l - r);
            ringPos = (ringPos + 1) % kFftSize;
            if (++hopFill >= kHop) { hopFill = 0; fftFrame(); }
        }

        loud.process (L, R, chunk);
        stepFill += chunk;
        offset += chunk;
        if (stepFill >= stepLen) { stepFill = 0; stepCompleted(); }
    }

    lMid.sanitize(); lLow.sanitize(); lHigh.sanitize(); rMid.sanitize(); rLow.sanitize(); rHigh.sanitize();
}

void MixAccumulator::stepCompleted()
{
    const float momentary = loud.getMomentary();
    const bool active = momentary > -50.0f;

    maxSample = std::max (maxSample, stepPeak);
    maxTp = std::max (maxTp, stepTp);
    if (stepTp > 1.0f) ++truePeakOvers;
    lastStepPeakDb = ampDb (stepPeak);
    lastStepTpDb = ampDb (stepTp);

    recentStepPeaks.push_back (stepPeak);
    if (recentStepPeaks.size() > 4) recentStepPeaks.pop_front();

    if (active)
    {
        ++activeSteps;
        activeSumSq += stepLL + stepRR; activeCount += 2 * stepLen;
        float blockPeak = 0; for (auto p : recentStepPeaks) blockPeak = std::max (blockPeak, p);
        microSum += ampDb (blockPeak) - momentary; ++microCount;

        LL += stepLL; RR += stepRR; LR += stepLR;
        const double denom = std::sqrt (stepLL * stepRR);
        const float corr = denom > 1e-12 ? (float) (stepLR / denom) : 1.0f;
        lastStepCorr = corr;
        const double m2 = stepLL + stepRR + 2 * stepLR, s2 = stepLL + stepRR - 2 * stepLR;
        lastStepWidth = m2 > 1e-12 ? (float) std::min (300.0, 100.0 * std::sqrt (std::max (0.0, s2) / m2)) : 0.0f;
        ++corrSteps;
        if (corr < 0.0f) ++negCorrSteps;
        worstCorr = std::min (worstCorr, corr);

        if (++stepsSinceSnap >= 10)
        {
            stepsSinceSnap = 0;
            Snapshot s;
            s.integrated = loud.getIntegrated();
            const double M2 = LL + RR + 2 * LR, S2 = LL + RR - 2 * LR;
            s.width = M2 > 1e-12 ? (float) std::min (300.0, 100.0 * std::sqrt (std::max (0.0, S2) / M2)) : 0.0f;
            bandPowers (sumPower, std::max (1.0, frames), sixthCentres(),
                        BandGrid::kSixthBands, 1.0 / 12.0, s.bands.data());
            snaps.push_back (s);
            if (snaps.size() > 12) snaps.pop_front();
        }
    }
    else
    {
        lastStepCorr = 1.0f; lastStepWidth = 0.0f;
    }

    stepLL = stepRR = stepLR = 0; stepPeak = 0; stepTp = 0;
}

void MixAccumulator::fftFrame()
{
    // gate: skip near-silent frames so silence does not skew the long-term spectrum
    double eM = 0, eS = 0;
    for (int i = 0; i < kFftSize; ++i) { eM += (double) ringM[(size_t) i] * ringM[(size_t) i]; eS += (double) ringS[(size_t) i] * ringS[(size_t) i]; }
    const bool active = (eM + eS) / kFftSize > 1.0e-7;   // ~ -70 dBFS

    auto transform = [this] (const std::vector<float>& ring, std::vector<float>& out)
    {
        for (int i = 0; i < kFftSize; ++i)
            fftBuf[(size_t) i] = ring[(size_t) ((ringPos + i) % kFftSize)] * window[(size_t) i];
        std::fill (fftBuf.begin() + kFftSize, fftBuf.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
        out.assign (fftBuf.begin(), fftBuf.begin() + kBins);
    };

    static thread_local std::vector<float> magM, magS;
    transform (ringM, magM);
    transform (ringS, magS);

    for (int b = 0; b < kBins; ++b)
    {
        const double pm = (double) magM[(size_t) b] * magM[(size_t) b] / windowNorm;
        const double ps = (double) magS[(size_t) b] * magS[(size_t) b] / windowNorm;
        const double p = pm + ps;
        if (active) { sumPower[(size_t) b] += p; sumSide[(size_t) b] += ps; }
        livePower[(size_t) b] = (float) (0.55 * livePower[(size_t) b] + 0.45 * p);
    }
    if (active) frames += 1.0;
}

void MixAccumulator::bandPowers (const std::vector<double>& binPower, double nFrames, const float* centres, int numBands,
                                 double halfWidthOct, float* out) const
{
    const double binHz = sr / kFftSize;
    for (int k = 0; k < numBands; ++k)
    {
        const double lo = centres[k] * std::pow (2.0, -halfWidthOct), hi = centres[k] * std::pow (2.0, halfWidthOct);
        double p = 0;
        if (hi >= sr * 0.5) { out[k] = -120.0f; continue; }
        // integrate bins with fractional overlap (bin b covers [b-0.5, b+0.5] * binHz)
        const int b0 = std::max (0, (int) std::floor (lo / binHz + 0.5)), b1 = std::min (kBins - 1, (int) std::floor (hi / binHz + 0.5));
        for (int b = b0; b <= b1; ++b)
        {
            const double bl = (b - 0.5) * binHz, bh = (b + 0.5) * binHz;
            const double ov = std::max (0.0, std::min (hi, bh) - std::max (lo, bl)) / binHz;
            p += ov * binPower[(size_t) b];
        }
        out[k] = toDb (p / std::max (1.0, nFrames));
    }
}

MixStats MixAccumulator::computeStats() const
{
    MixStats m;
    m.sampleRate = sr;
    m.secondsAnalysed = activeSteps * 0.1;
    m.secondsTotal = loud.getSecondsProcessed();
    m.integrated = loud.getIntegrated();
    m.lra = loud.getLoudnessRange();
    m.maxMomentary = loud.getMaxMomentary();
    m.maxShortTerm = loud.getMaxShortTerm();
    m.truePeakDb = ampDb (maxTp);
    m.samplePeakDb = ampDb (maxSample);
    m.rmsDb = activeCount > 0 ? toDb (activeSumSq / (double) activeCount) : -100.0f;
    m.crestDb = m.samplePeakDb - m.rmsDb;
    m.plr = m.integrated > -99 ? m.truePeakDb - m.integrated : 0.0f;
    m.microDynamicsDb = microCount > 0 ? (float) (microSum / microCount) : 0.0f;
    m.punchDb = punchCount > 0 ? (float) (punchSum / punchCount) : 0.0f;
    m.truePeakOvers = truePeakOvers;
    m.onsets = onsets;
    m.shortTermHistory = loud.getShortTermHistory();

    // clipping: flat runs within 0.3 dB of the overall sample peak (and peak above -3 dBFS)
    int clips = 0;
    if (m.samplePeakDb > -3.0f)
    {
        const int peakIdx = (int) std::round (-m.samplePeakDb * 10.0f);
        for (int i = std::max (0, peakIdx - 1); i <= std::min ((int) flatRunHist.size() - 1, peakIdx + 3); ++i)
            clips += flatRunHist[(size_t) i];
    }
    m.clipEvents = clips;
    m.flatTopRuns = flatTopRuns;
    m.clipEventsPerMinute = m.secondsAnalysed > 1 ? (float) (clips / (m.secondsAnalysed / 60.0)) : 0.0f;

    const double dc = count > 0 ? std::max (std::abs (sumL), std::abs (sumR)) / (double) count : 0.0;
    m.dcOffsetDb = ampDb (dc);

    // spectrum bands
    std::array<float, BandGrid::kSixthBands> c6 {}; std::array<float, BandGrid::kFineBands> c24 {};
    for (int i = 0; i < BandGrid::kSixthBands; ++i) c6[(size_t) i] = BandGrid::sixthCentre (i);
    for (int i = 0; i < BandGrid::kFineBands; ++i) c24[(size_t) i] = BandGrid::fineCentre (i);
    const double nf = std::max (1.0, frames);
    bandPowers (sumPower, nf, c6.data(), BandGrid::kSixthBands, 1.0 / 12.0, m.sixth.data());
    bandPowers (sumPower, nf, c24.data(), BandGrid::kFineBands, 1.0 / 48.0, m.fine.data());
    bandPowers (sumSide, nf, c6.data(), BandGrid::kSixthBands, 1.0 / 12.0, m.sideSixth.data());

    // infrasonic / low-end share / phone estimate from long-term spectrum
    const double binHz = sr / kFftSize;
    double infra = 0, lowRef = 0, kTotal = 0, kLow = 0, kPhone = 0;
    for (int b = 1; b < kBins; ++b)
    {
        const double f = b * binHz, p = sumPower[(size_t) b] / nf;
        if (f < 25) infra += p; else if (f < 200) lowRef += p;
        const double kp = p * kWeight[(size_t) b];
        kTotal += kp;
        if (f < 150) kLow += kp;
        // small-speaker model: 2nd-order high-pass at 250 Hz
        const double x = f / 250.0, hp = (x * x * x * x) / (1.0 + x * x * x * x);
        kPhone += kp * hp;
    }
    m.infrasonicRatioDb = lowRef > 0 ? toDb (infra / lowRef) : -100.0f;
    m.lowEndShare = kTotal > 0 ? (float) (kLow / kTotal) : 0.0f;
    m.phoneLossDb = kTotal > 0 ? toDb (kPhone / kTotal) : 0.0f;

    // stereo
    m.xovers = xo;
    const double denom = std::sqrt (LL * RR);
    m.correlation = denom > 1e-12 ? (float) (LR / denom) : 1.0f;
    const double M2 = LL + RR + 2 * LR, S2 = LL + RR - 2 * LR;
    m.widthPct = M2 > 1e-12 ? (float) std::min (300.0, 100.0 * std::sqrt (std::max (0.0, S2) / M2)) : (S2 > 1e-12 ? 300.0f : 0.0f);
    m.balanceDb = (LL > 1e-12 && RR > 1e-12) ? toDb (RR / LL) : 0.0f;   // + = right louder
    m.monoLossDb = (LL + RR) > 1e-12 ? toDb (std::max (1e-12, M2) / (2.0 * (LL + RR))) : 0.0f;
    m.negativeCorrFraction = corrSteps > 0 ? (float) negCorrSteps / (float) corrSteps : 0.0f;
    m.worstCorrelation = worstCorr;
    for (size_t b = 0; b < 4; ++b)
    {
        const double d = std::sqrt (bLL[b] * bRR[b]);
        m.bandCorr[b] = d > 1e-12 ? (float) (bLR[b] / d) : 1.0f;
        const double bm = bLL[b] + bRR[b] + 2 * bLR[b], bs = bLL[b] + bRR[b] - 2 * bLR[b];
        m.bandWidthPct[b] = bm > 1e-12 ? (float) std::min (300.0, 100.0 * std::sqrt (std::max (0.0, bs) / bm)) : 0.0f;
        m.bandMonoLossDb[b] = (bLL[b] + bRR[b]) > 1e-12 ? toDb (std::max (1e-12, bm) / (2.0 * (bLL[b] + bRR[b]))) : 0.0f;
    }
    return m;
}

MixAccumulator::Readiness MixAccumulator::readiness() const
{
    Readiness r;
    r.activeSeconds = activeSteps * 0.1;
    if (snaps.size() < 5) { r.confidence = (float) std::min (0.3, r.activeSeconds / 30.0); return r; }

    const auto& now = snaps.back();
    const auto& past = snaps[snaps.size() - 5];   // ~4 s of active audio earlier

    const float dI = std::abs (now.integrated - past.integrated);
    float specDiff = 0; int n = 0;
    for (int i = 3; i < BandGrid::kSixthBands - 3; ++i)
        if (now.bands[(size_t) i] > -100 && past.bands[(size_t) i] > -100)
        {
            // compare shapes: remove the level difference first
            specDiff += std::abs ((now.bands[(size_t) i] - past.bands[(size_t) i]) - (now.integrated - past.integrated));
            ++n;
        }
    specDiff = n > 0 ? specDiff / (float) n : 10.0f;
    const float dW = std::abs (now.width - past.width);

    r.loudnessStability = juce::jlimit (0.0f, 1.0f, 1.0f - dI / 1.2f);
    r.spectrumStability = juce::jlimit (0.0f, 1.0f, 1.0f - specDiff / 2.0f);
    r.stereoStability   = juce::jlimit (0.0f, 1.0f, 1.0f - dW / 15.0f);
    const float timeFactor = (float) juce::jlimit (0.0, 1.0, r.activeSeconds / 12.0);
    r.confidence = 0.30f * timeFactor + 0.25f * r.loudnessStability + 0.30f * r.spectrumStability + 0.15f * r.stereoStability;
    r.ready = (r.activeSeconds >= 8.0 && r.confidence >= 0.85f) || r.activeSeconds >= 45.0;
    return r;
}
} // namespace mooni
