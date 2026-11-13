#pragma once
// WAV / AIFF wavetable import and export (SPEC §3).
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace prism
{
enum class ImportMode { Auto, Fixed, PitchDetect, SingleCycle };

struct ImportResult
{
    bool ok = false;
    juce::String error;
    juce::String name;
    int clmFrameSize = 0;                     // from a "clm " chunk, 0 if none
    std::vector<std::vector<float>> frames;   // 2048 samples each, after table hygiene
};

// Reads the "clm " chunk frame size (e.g. "<!>2048 ...") from a RIFF/WAVE file, 0 if absent.
int readClmFrameSize (const juce::File& f);

ImportResult importWavetable (const juce::File& f, ImportMode mode, int fixedFrames = 256);
ImportResult importFromSamples (const std::vector<float>& mono, ImportMode mode, int fixedFrames, int clmFrameSize);

// Autocorrelation cycle length in samples (0 if none found)
double detectCycleLength (const std::vector<float>& x);

// 32-bit float WAV, frames concatenated, with a "clm " chunk naming the frame size.
bool exportWavetable (const juce::File& f, const std::vector<std::vector<float>>& frames, double sampleRate = 44100.0);
} // namespace prism
