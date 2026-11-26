#pragma once
#include "Accumulator.h"
#include "Diagnostics.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <mutex>

namespace mooni
{
/** Optional reference track: decoded and analysed on its own thread, never on the audio thread.
    A host-rate copy is kept for loudness-matched monitoring ("listen to the reference"). */
class ReferenceTrack : private juce::Thread
{
public:
    enum class Status : int { Empty = 0, Loading, Ready, Error };

    ReferenceTrack();
    ~ReferenceTrack() override;

    void load (const juce::File& file, double hostSampleRate);
    void clear();
    void setHostRate (double sr);          // reloads the playback copy if the rate changed

    Status getStatus() const noexcept { return (Status) status.load(); }
    float getProgress() const noexcept { return progress.load(); }
    juce::String getError() const;
    juce::File getFile() const;
    std::shared_ptr<const MixStats> getStats() const;
    ReportPtr getReport() const;
    int changeCounter() const noexcept { return changes.load(); }

    /** Audio thread: adds nothing if busy (try-lock), never blocks. Returns false if nothing rendered. */
    bool render (float* left, float* right, int numSamples, float gain) noexcept;
    void restartPlayback() noexcept { playPos.store (0); }
    double lengthSeconds() const;

private:
    void run() override;

    juce::AudioFormatManager formats;
    mutable std::mutex infoLock;
    juce::File file, pendingFile;
    double pendingRate = 48000.0;
    juce::String error;
    std::shared_ptr<const MixStats> stats;
    ReportPtr report;

    juce::SpinLock playLock;
    juce::AudioBuffer<float> playBuffer;   // at host rate
    double playRate = 0.0;
    std::atomic<juce::int64> playPos { 0 };

    std::atomic<int> status { (int) Status::Empty };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> changes { 0 };
};
} // namespace mooni
