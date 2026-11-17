#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "Types.h"

// Spectral dynamics (SPEC §5). Inside a Bell band's −3 dB span,
// individual frequencies that stick out above the band's smoothed level
// are pulled down. STFT N = 2048 (scaled with sample rate), hop N/4,
// Hann analysis and synthesis, overlap-add. Only the change (G − 1)·X is
// resynthesised, so bins outside every span pass untouched; latency N.
namespace lens::dsp
{
class SpectralProcessor
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels);
    void reset() noexcept;
    int latency() const noexcept { return N; }
    int frameSize() const noexcept { return N; }

    // In place, any n (audio thread).
    void process (double* L, double* R, int n, const EngineParams&) noexcept;

    // Total cut per bin (dB, ≥ 0) of the last frame, for the display.
    const std::vector<float>& lastCutDb() const noexcept { return cutDb; }
    double binHz() const noexcept { return fs / N; }
    // Any thread: the cut at a frequency in the last frame (display).
    float publishedCutDb (double freq) const noexcept;

private:
    void frame (const EngineParams&) noexcept;

    double fs = 48000.0;
    int N = 2048, hop = 512, channels = 2;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    std::array<std::vector<float>, 2> inRing, olaRing;
    int pos = 0, sinceHop = 0;
    std::array<std::vector<float>, 2> spec;
    std::vector<float> work, power, gainDb, cutDb;
    std::array<std::vector<float>, kNumBands> smoothCut; // per band, per bin
    std::unique_ptr<std::atomic<float>[]> published;
    int numPublished = 0;
};
} // namespace lens::dsp
