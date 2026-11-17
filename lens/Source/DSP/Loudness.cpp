#include "Loudness.h"

#include <complex>

namespace lens::dsp
{
double Biquad::powerAt (double f, double fs) const noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * f / fs;
    const std::complex<double> z1c = std::polar (1.0, -w);
    const std::complex<double> z2c = z1c * z1c;
    const auto num = b0 + b1 * z1c + b2 * z2c;
    const auto den = 1.0 + a1 * z1c + a2 * z2c;
    return std::norm (num / den);
}

void KWeighting::prepare (double fs) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    {
        // Stage 1: high shelf (head effects).
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        // Stage 2: RLB high-pass.
        const double f0 = 38.13547087602444;
        const double Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (K * K - 1.0) / a0;
        highPass.a2 = (1.0 - K / Q + K * K) / a0;
    }
    reset();
}

void ShortTermLoudness::prepare (double sampleRate, int numChannels)
{
    channels = numChannels > 1 ? 2 : 1;
    blockLen = (int) std::lround (sampleRate * 0.1);
    for (auto& k : kw)
        k.prepare (sampleRate);
    reset();
}

void ShortTermLoudness::reset()
{
    for (auto& k : kw)
        k.reset();
    acc = 0.0;
    accCount = 0;
    blocks.fill (0.0);
    writeIndex = 0;
    filled = 0;
    currentLufs = -200.0;
}

void ShortTermLoudness::closeBlock() noexcept
{
    blocks[(size_t) writeIndex] = acc;
    writeIndex = (writeIndex + 1) % numBlocks;
    if (filled < numBlocks)
        ++filled;
    acc = 0.0;
    accCount = 0;

    double sum = 0.0;
    for (int i = 0; i < filled; ++i)
        sum += blocks[(size_t) i];
    currentLufs = msToLufs (sum / ((double) filled * blockLen));
}
} // namespace lens::dsp
