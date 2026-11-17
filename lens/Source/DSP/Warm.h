#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>

// Warm character (SPEC §3): gentle saturation on the output stage, 2×
// oversampled with polyphase IIR half-bands. Unity gain for small
// signals, so it never just sounds louder.
namespace lens::dsp
{
class WarmStage
{
public:
    static constexpr int maxChunk = 8; // the engine calls it per 8-sample chunk

    void prepare (double sampleRate, int maxBlock, int numChannels);
    void reset() noexcept;
    int latency() const noexcept { return latencySamples; }
    void process (double* L, double* R, int n) noexcept;

    // The waveshaper (oversampled domain), exposed for tests.
    static float shape (float x) noexcept;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> buf;
    int channels = 2;
    int latencySamples = 0;
    float dcX[2] {}, dcY[2] {};
    float dcCoeff = 0.9995f;
    // Level match: slow RMS of input and output keeps Warm at the same
    // level as Clean (within ±1 dB of makeup).
    double eIn = 1.0e-6, eOut = 1.0e-6, makeup = 1.0, levelCoeff = 0.0001;
};
} // namespace lens::dsp
