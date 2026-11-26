#pragma once
#include <juce_core/juce_core.h>

namespace mooni
{
/** All user-facing text in sources is UTF-8. Always go through ru() so JUCE never
    interprets Cyrillic bytes as ASCII/Latin-1. */
inline juce::String ru (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/** ru() + sequential "{}" substitution: fmt ("В районе {}–{} Гц", lo, hi). */
inline juce::String fmtImpl (juce::String s) { return s; }
template <typename T, typename... Rest>
juce::String fmtImpl (juce::String s, const T& first, const Rest&... rest)
{
    const int idx = s.indexOf ("{}");
    if (idx < 0) return s;
    return fmtImpl (s.substring (0, idx) + juce::String (first) + s.substring (idx + 2), rest...);
}
template <typename... Args>
juce::String fmt (const char* utf8, const Args&... args) { return fmtImpl (ru (utf8), args...); }

/** Number formatting helpers (Russian decimal comma is avoided on purpose: audio software
    conventionally uses a dot, and users read numbers from other plugins the same way). */
/** NB: juce::String (float, 0) means "full precision", so 0 decimals is handled explicitly. */
inline juce::String num (float v, int decimals = 1)
{
    if (decimals <= 0) return juce::String (juce::roundToInt (v));
    const float r = std::round (v * std::pow (10.0f, (float) decimals)) / std::pow (10.0f, (float) decimals);
    return juce::String (r == 0.0f ? 0.0f : r, decimals);   // never print "-0.0"
}
inline juce::String signedNum (float v, int decimals = 1) { const auto s = num (v, decimals); return (s.getDoubleValue() > 0 ? "+" : "") + s; }

/** Russian plural: plural (5, "запись", "записи", "записей") -> "записей". */
inline juce::String plural (int n, const char* one, const char* few, const char* many)
{
    const int a = std::abs (n) % 100, b = a % 10;
    const char* w = (a >= 11 && a <= 19) ? many : (b == 1 ? one : (b >= 2 && b <= 4 ? few : many));
    return juce::String (n) + " " + ru (w);
}
inline juce::String hz (float f)
{
    if (f >= 1000.0f) return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : (std::abs (f - std::round (f / 100) * 100) < 1 ? 1 : 2))
                             .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + ru (" кГц");
    return juce::String (juce::roundToInt (f)) + ru (" Гц");
}
inline juce::String hzRange (float lo, float hi)
{
    if (lo >= 1000.0f && hi >= 1000.0f)
        return juce::String (lo / 1000.0f, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + ru ("–")
             + juce::String (hi / 1000.0f, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + ru (" кГц");
    if (hi < 1000.0f)
        return juce::String (juce::roundToInt (lo)) + ru ("–") + juce::String (juce::roundToInt (hi)) + ru (" Гц");
    return hz (lo) + ru (" – ") + hz (hi);
}
} // namespace mooni
