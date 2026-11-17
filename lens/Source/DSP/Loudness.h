#pragma once

#include <array>
#include <cmath>
#include <vector>

// ITU-R BS.1770 K-weighting and a short-term (3 s) loudness meter.
namespace lens::dsp
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    double tick (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }

    // |H(e^jw)|² at frequency f.
    double powerAt (double f, double fs) const noexcept;
};

struct KWeighting
{
    Biquad shelf, highPass;

    void prepare (double sampleRate) noexcept;
    double tick (double x) noexcept { return highPass.tick (shelf.tick (x)); }
    void reset() noexcept
    {
        shelf.reset();
        highPass.reset();
    }
    double powerAt (double f, double fs) const noexcept { return shelf.powerAt (f, fs) * highPass.powerAt (f, fs); }
};

// Short-term loudness: mean square over the last 3 s, updated every
// 100 ms. Deterministic: the 100 ms grid runs on its own sample counter.
class ShortTermLoudness
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    // One sample per channel (channel 1 ignored when mono).
    void push (double left, double right) noexcept
    {
        const double kl = kw[0].tick (left);
        double sum = kl * kl;
        if (channels > 1)
        {
            const double kr = kw[1].tick (right);
            sum += kr * kr;
        }
        acc += sum;
        if (++accCount >= blockLen)
            closeBlock();
    }

    // LUFS of the last 3 s (or less, right after reset). −inf → −200.
    double lufs() const noexcept { return currentLufs; }
    bool hasFullWindow() const noexcept { return filled >= numBlocks; }

    static double msToLufs (double ms) noexcept { return ms > 1.0e-20 ? -0.691 + 10.0 * std::log10 (ms) : -200.0; }

private:
    void closeBlock() noexcept;

    static constexpr int numBlocks = 30;
    KWeighting kw[2];
    int channels = 2;
    int blockLen = 4800;
    double acc = 0.0;
    int accCount = 0;
    std::array<double, numBlocks> blocks {};
    int writeIndex = 0;
    int filled = 0;
    double currentLufs = -200.0;
};
} // namespace lens::dsp
