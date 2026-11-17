#include "Warm.h"

namespace lens::dsp
{
float WarmStage::shape (float x) noexcept
{
    // Soft tanh curve (unity slope at 0) plus a touch of even harmonics.
    constexpr float drive = 1.5f;
    const float t = std::tanh (drive * x) / drive;
    return t + 0.04f * t * t;
}

void WarmStage::prepare (double sampleRate, int, int numChannels)
{
    channels = numChannels > 1 ? 2 : 1;
    os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, 1,
                                                           juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                           true, true);
    os->initProcessing ((size_t) maxChunk);
    latencySamples = juce::roundToInt (os->getLatencyInSamples());
    buf.setSize (channels, maxChunk);
    // DC blocker (the even harmonics add a little DC) at ~5 Hz, 2× rate.
    dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / (2.0 * sampleRate));
    levelCoeff = 1.0 - std::exp (-1.0 / (0.5 * sampleRate));
    reset();
}

void WarmStage::reset() noexcept
{
    if (os != nullptr)
        os->reset();
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    eIn = eOut = 1.0e-6;
    makeup = 1.0;
}

void WarmStage::process (double* L, double* R, int n) noexcept
{
    if (os == nullptr || n <= 0)
        return;
    for (int i = 0; i < n; ++i)
    {
        buf.setSample (0, i, (float) L[i]);
        if (channels > 1)
            buf.setSample (1, i, (float) R[i]);
    }
    juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
    auto up = os->processSamplesUp (block);
    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        auto* d = up.getChannelPointer (c);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float y = shape (d[i]);
            // y − x[n−1] + a·y[n−1]: one-pole DC blocker.
            const float out = y - dcX[c] + dcCoeff * dcY[c];
            dcX[c] = y;
            dcY[c] = out;
            d[i] = out;
        }
    }
    os->processSamplesDown (block);
    for (int i = 0; i < n; ++i)
    {
        const double inL = L[i], inR = R[i];
        const double oL = buf.getSample (0, i), oR = channels > 1 ? buf.getSample (1, i) : oL;
        eIn += levelCoeff * (inL * inL + inR * inR - eIn);
        eOut += levelCoeff * (oL * oL + oR * oR - eOut);
        const double target = eOut > 1.0e-12 ? juce::jlimit (0.891, 1.122, std::sqrt (eIn / eOut)) : 1.0;
        makeup += levelCoeff * (target - makeup);
        L[i] = oL * makeup;
        R[i] = oR * makeup;
    }
}
} // namespace lens::dsp
