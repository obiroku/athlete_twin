#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <vector>

// Reference and EQ Match (SPEC §9). Headless.
namespace lens::match
{
// Long-term average spectrum on 1/6-octave points, 20 Hz to 20 kHz.
struct Spectrum
{
    juce::String name;
    std::vector<double> freqs, db;
    double seconds = 0.0;
    bool isValid() const noexcept { return ! db.empty() && seconds > 0.0; }

    juce::var toVar() const;
    static Spectrum fromVar (const juce::var&);
};

std::vector<double> sixthOctavePoints();

class LtasAccumulator
{
public:
    void prepare (double sampleRate);
    void clear();
    void add (const float* mono, int n);
    Spectrum result (const juce::String& name = {}) const;
    double seconds() const noexcept { return fs > 0 ? (double) fed / fs : 0.0; }

private:
    void frame();
    double fs = 0.0;
    int size = 8192, hop = 4096, fill = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work;
    std::vector<double> sum;
    long long frames = 0, fed = 0;
    int writePos = 0;
};

// Reads a WAV, AIFF or MP3 file (background thread) into a spectrum.
Spectrum analyseFile (const juce::File&, const std::function<bool()>& shouldStop = {});

// Level-matched difference (reference − mine) in dB, smoothed to 1/3
// octave, on the 1/6-octave points from 30 Hz to 16 kHz.
struct Difference
{
    std::vector<double> freqs, db;
};
Difference difference (const Spectrum& reference, const Spectrum& mine);

struct Suggestion
{
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
};
// At most 6 broad bells (Q ≤ 2, gain within ±6 dB) that close the gap.
// Pure: never touches the EQ.
std::vector<Suggestion> suggest (const Difference&);

// Remembered references: ~/Music/LENS/References.
juce::File referencesFolder();
bool saveReference (const Spectrum&);
std::vector<Spectrum> loadReferences();
} // namespace lens::match
