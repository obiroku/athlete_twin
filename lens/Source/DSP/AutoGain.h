#pragma once

#include <array>

#include "FilterDesign.h"
#include "Types.h"

// Auto-gain from the EQ curve (SPEC §3 Output): the loudness change the
// curve would cause on pink noise, K-weighted like BS.1770.
namespace lens::dsp
{
class AutoGainEstimator
{
public:
    static constexpr int numPoints = 64;

    void prepare (double sampleRate);

    struct BandCurve
    {
        const BandDesign* design = nullptr;
        StereoMode mode = StereoMode::Stereo;
    };

    // Loudness change of the combined curve in dB (positive = louder).
    double loudnessChangeDb (const BandCurve* bands, int numBands, int numChannels) const noexcept;

private:
    std::array<double, numPoints> w {};      // tan(π·f/fs)
    std::array<double, numPoints> weight {}; // K-weighting power
    double weightSum = 1.0;
};
} // namespace lens::dsp
