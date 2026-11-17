#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Types.h"

namespace lens::params
{
// Reads APVTS atomics into a dsp::EngineParams snapshot. Lock-free and
// allocation-free: safe on the audio thread.
class ParamReader
{
public:
    explicit ParamReader (juce::AudioProcessorValueTreeState&);
    void read (dsp::EngineParams&) const noexcept;
    void readBand (int bandIndex, dsp::BandParams&) const noexcept;

private:
    struct BandPtrs
    {
        std::atomic<float>*on, *shape, *freq, *gain, *q, *slope, *stereo;
        std::atomic<float>*dynOn, *dynMode, *dynRange, *dynThresh, *dynRatio, *dynAttack, *dynRelease;
        std::atomic<float>*detSource, *detCustom, *detHp, *detLp;
        std::atomic<float>*specOn, *specThresh, *specAmount;
    };
    std::array<BandPtrs, dsp::kNumBands> bands {};
    std::atomic<float>*phaseMode, *lpResolution, *character, *outputGain, *autoGain, *delta, *bypass, *abSlot, *lookahead;
};
} // namespace lens::params
