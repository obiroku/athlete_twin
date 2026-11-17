#pragma once

#include <juce_core/juce_core.h>

#include "DSP/Types.h"

namespace lens
{
class LensProcessor;
}

namespace lens::coach
{
// Everything the Coach looks at, sampled on the message thread 4 times a
// second from parameters and audio-thread stats. Plain data: tests build
// these by hand.
struct CoachState
{
    double time = 0.0; // seconds
    dsp::EngineParams params;
    juce::String trackType { "Bus" };
    int numChannels = 2;

    std::array<float, dsp::kNumBands> activity {};
    float inputLufs = -200.0f, outputLufs = -200.0f;
    float outputPeak = 0.0f;       // linear, since the previous sample
    float inputLevelDb = -200.0f;  // 100 ms RMS
    float midLevelDb = -200.0f, sideLevelDb = -200.0f;
    float sidechainLevelDb = -200.0f;
    int soloBand = -1, listenBand = -1;
    float latencyMs = 0.0f;

    bool playing() const noexcept { return inputLevelDb > -50.0f; }
    bool sidechainPresent() const noexcept { return sidechainLevelDb > -60.0f; }
    int bandsOn() const noexcept;
    int dynamicBands() const noexcept;
};

// Samples a processor (message thread). Resets the output peak hold.
CoachState sampleProcessor (LensProcessor&, double timeSeconds);
} // namespace lens::coach
