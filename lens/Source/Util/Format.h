#pragma once

#include <juce_core/juce_core.h>

// Number formats shared by the GUI and the Coach (LESSONS.md: "250 Hz" or
// "2.5 kHz"; gains with one decimal and a sign, "−3.0 dB").
namespace lens::fmt
{
inline const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92"); // U+2212

inline juce::String number (double v, int decimals)
{
    auto s = juce::String (std::abs (v), decimals);
    return (v < 0.0 && s.getDoubleValue() != 0.0 ? minus : juce::String()) + s; // NOLINT
}

inline juce::String freq (double hz)
{
    if (hz < 1000.0)
        return juce::String ((int) std::lround (hz)) + " Hz";
    const double k = hz / 1000.0;
    const double r = std::round (k * 10.0) / 10.0;
    if (std::abs (r - std::round (r)) < 1.0e-9)
        return juce::String ((int) std::lround (r)) + " kHz";
    return juce::String (r, 1) + " kHz";
}

// "+3.0 dB", "−3.0 dB", "0.0 dB".
inline juce::String gain (double db)
{
    const double r = std::round (db * 10.0) / 10.0;
    if (std::abs (r) < 0.05)
        return "0.0 dB";
    return (r > 0 ? "+" : minus) + juce::String (std::abs (r), 1) + " dB";
}

// Unsigned, one decimal: "1.5".
inline juce::String plain (double v) { return juce::String (std::abs (v), 1); }

// Logic note naming: MIDI 60 = C3.
struct Note
{
    juce::String name;
    int cents = 0;
};

inline Note note (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (hz <= 0.0)
        return {};
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - nearest) * 100.0);
    const int octave = (int) std::floor (nearest / 12.0) - 2;
    const int idx = ((nearest % 12) + 12) % 12;
    return { juce::String (names[idx]) + juce::String (octave), cents };
}

inline juce::String noteWithCents (double hz)
{
    const auto n = note (hz);
    if (n.cents == 0)
        return n.name;
    return n.name + " " + (n.cents > 0 ? "+" : minus) + juce::String (std::abs (n.cents)) + " ct";
}
} // namespace lens::fmt
