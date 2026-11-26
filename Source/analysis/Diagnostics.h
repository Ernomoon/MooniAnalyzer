#pragma once
#include "Report.h"

namespace mooni
{
struct DiagnosticsContext
{
    int genreChoice = 0;                          // 0 = auto
    const MixStats* reference = nullptr;          // optional reference track stats
    std::array<float, 4> currentWidthParams { 100, 100, 100, 100 };
    float currentBalanceParam = 0;
    bool imagerOn = false;
    juce::String fingerprint;
};

namespace diagnostics
{
    Report diagnose (const MixStats& stats, const DiagnosticsContext& ctx);

    /** Auto-detect a style profile index (>= 1) from measurable features. */
    int detectGenre (const MixStats& s, float& confidence);

    /** Target band levels (relative, mean of 500 Hz–2 kHz = 0 dB). */
    std::array<float, BandGrid::kSixthBands> targetCurve (const GenreProfile& g, const MixStats* reference);

    /** Band levels relative to their own 500 Hz–2 kHz mean. */
    std::array<float, BandGrid::kSixthBands> relativeBands (const std::array<float, BandGrid::kSixthBands>& absDb);

    float niceFrequency (float f);
}
} // namespace mooni
