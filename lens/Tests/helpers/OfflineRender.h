#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

// Offline rendering for tests and the benchmark.
namespace lens::test
{
struct RenderOptions
{
    double sampleRate = 48000.0;

    // Block sizes, cycled until the input is used up. { 64 } is a fixed
    // size; { 1, 517, 64, 4096, 3 } is a variable pattern.
    std::vector<int> blockPattern { 512 };

    // Optional external side-chain (mono or stereo), same length as the
    // input. Null disables the side-chain bus.
    const juce::AudioBuffer<float>* sidechain = nullptr;

    // Called before each block with the absolute sample position, for
    // tests that move parameters mid-render.
    std::function<void (juce::int64 position)> beforeBlock;
};

// Configures the processor's buses for the input's channel count (1 or 2),
// prepares it, renders, and returns the main output. The output is not
// latency-compensated; see processor.getLatencySamples().
juce::AudioBuffer<float> render (juce::AudioProcessor& processor,
                                 const juce::AudioBuffer<float>& input,
                                 const RenderOptions& options = {});

// A pattern that mixes tiny, odd, and maximum-size blocks.
std::vector<int> variableBlockPattern();

juce::AudioBuffer<float> makeNoise (int channels, int numSamples, juce::int64 seed, float peak = 0.5f);
juce::AudioBuffer<float> makeSine (int channels, int numSamples, double sampleRate, double freq, float peak = 0.5f);

bool readWav (const juce::File&, juce::AudioBuffer<float>& dest, double& sampleRate);
bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, int bits = 24);

// True when both buffers have the same shape and every sample has the
// same bit pattern.
bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b);

// Peak of (a − b) in dBFS (−inf → −300).
double peakDifferenceDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b);
} // namespace lens::test
