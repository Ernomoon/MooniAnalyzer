#include "ReferenceTrack.h"
#include "../core/Text.h"

namespace mooni
{
static constexpr double kMaxSeconds = 8.0 * 60.0;

ReferenceTrack::ReferenceTrack() : juce::Thread ("Mooni reference")
{
    formats.registerBasicFormats();
}

ReferenceTrack::~ReferenceTrack() { stopThread (5000); }

void ReferenceTrack::load (const juce::File& f, double hostSampleRate)
{
    stopThread (3000);
    {
        std::lock_guard<std::mutex> g (infoLock);
        pendingFile = f;
        pendingRate = hostSampleRate > 0 ? hostSampleRate : 48000.0;
        error = {};
    }
    status = (int) Status::Loading;
    progress = 0.0f;
    ++changes;
    startThread (juce::Thread::Priority::background);
}

void ReferenceTrack::clear()
{
    stopThread (3000);
    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        playBuffer.setSize (2, 0);
        playRate = 0.0;
    }
    std::lock_guard<std::mutex> g (infoLock);
    file = juce::File(); stats.reset(); report.reset(); error = {};
    status = (int) Status::Empty;
    ++changes;
}

void ReferenceTrack::setHostRate (double sr)
{
    if (getStatus() != Status::Ready || sr <= 0) return;
    if (std::abs (sr - playRate) < 0.5) return;
    juce::File f;
    { std::lock_guard<std::mutex> g (infoLock); f = file; }
    if (f.existsAsFile()) load (f, sr);
}

juce::String ReferenceTrack::getError() const { std::lock_guard<std::mutex> g (infoLock); return error; }
juce::File ReferenceTrack::getFile() const { std::lock_guard<std::mutex> g (infoLock); return file; }
std::shared_ptr<const MixStats> ReferenceTrack::getStats() const { std::lock_guard<std::mutex> g (infoLock); return stats; }
ReportPtr ReferenceTrack::getReport() const { std::lock_guard<std::mutex> g (infoLock); return report; }

double ReferenceTrack::lengthSeconds() const
{
    std::lock_guard<std::mutex> g (infoLock);
    return stats ? stats->secondsTotal : 0.0;
}

bool ReferenceTrack::render (float* left, float* right, int n, float gain) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (playLock);
    if (! sl.isLocked() || playBuffer.getNumSamples() == 0) return false;
    const auto len = (juce::int64) playBuffer.getNumSamples();
    auto pos = playPos.load();
    const float* srcL = playBuffer.getReadPointer (0);
    const float* srcR = playBuffer.getReadPointer (playBuffer.getNumChannels() > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        if (pos >= len) pos = 0;
        left[i] = srcL[pos] * gain; right[i] = srcR[pos] * gain;
        ++pos;
    }
    playPos.store (pos);
    return true;
}

void ReferenceTrack::run()
{
    juce::File f; double hostRate;
    { std::lock_guard<std::mutex> g (infoLock); f = pendingFile; hostRate = pendingRate; }

    auto fail = [this] (const juce::String& msg)
    {
        std::lock_guard<std::mutex> g (infoLock);
        error = msg;
        status = (int) Status::Error;
        ++changes;
    };

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { fail (ru ("Не удалось открыть файл. Поддерживаются WAV, AIFF, FLAC, OGG и MP3.")); return; }
    if (reader->sampleRate < 8000) { fail (ru ("Странная частота дискретизации файла.")); return; }

    const double srcRate = reader->sampleRate;
    const auto total = (juce::int64) std::min ((double) reader->lengthInSamples, kMaxSeconds * srcRate);
    if (total < srcRate * 3) { fail (ru ("Файл слишком короткий (нужно хотя бы 3 секунды).")); return; }

    juce::AudioBuffer<float> src (2, (int) total);
    {
        const int chunk = 65536;
        juce::AudioBuffer<float> tmp ((int) std::max (2u, reader->numChannels), chunk);
        for (juce::int64 pos = 0; pos < total; pos += chunk)
        {
            if (threadShouldExit()) return;
            const int n = (int) std::min<juce::int64> (chunk, total - pos);
            reader->read (&tmp, 0, n, pos, true, true);
            src.copyFrom (0, (int) pos, tmp, 0, 0, n);
            src.copyFrom (1, (int) pos, tmp, reader->numChannels > 1 ? 1 : 0, 0, n);
            progress = 0.4f * (float) (pos + n) / (float) total;
        }
    }

    // analysis at the file's native rate
    MixAccumulator acc;
    acc.prepare (srcRate, { 120.0f, 1000.0f, 6000.0f });
    {
        const int chunk = 4096;
        for (int pos = 0; pos < (int) total; pos += chunk)
        {
            if (threadShouldExit()) return;
            const int n = std::min (chunk, (int) total - pos);
            acc.process (src.getReadPointer (0) + pos, src.getReadPointer (1) + pos, n);
            progress = 0.4f + 0.4f * (float) (pos + n) / (float) total;
        }
    }
    auto st = std::make_shared<MixStats> (acc.computeStats());
    if (! st->isValid()) { fail (ru ("В файле почти тишина — анализировать нечего.")); return; }
    DiagnosticsContext ctx;
    auto rep = std::make_shared<Report> (diagnostics::diagnose (*st, ctx));
    rep->fromReferenceFile = true;
    rep->label = f.getFileNameWithoutExtension();

    // host-rate playback copy
    juce::AudioBuffer<float> play;
    if (std::abs (srcRate - hostRate) < 0.5) play = std::move (src);
    else
    {
        const double ratio = srcRate / hostRate;
        const int outLen = (int) std::floor ((double) total / ratio) - 64;
        play.setSize (2, std::max (0, outLen));
        for (int c = 0; c < 2; ++c)
        {
            juce::WindowedSincInterpolator interp;
            interp.process (ratio, src.getReadPointer (c), play.getWritePointer (c), play.getNumSamples());
            progress = 0.8f + 0.1f * (float) (c + 1);
            if (threadShouldExit()) return;
        }
    }
    {
        const juce::SpinLock::ScopedLockType sl (playLock);
        playBuffer = std::move (play);
        playRate = hostRate;
        playPos = 0;
    }
    {
        std::lock_guard<std::mutex> g (infoLock);
        file = f; stats = st; report = rep;
    }
    progress = 1.0f;
    status = (int) Status::Ready;
    ++changes;
}
} // namespace mooni
