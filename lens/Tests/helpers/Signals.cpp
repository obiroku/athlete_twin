#include "Signals.h"

#include "DSP/Loudness.h"

#include <cmath>

namespace lens::test
{
juce::AudioBuffer<float> makePink (int channels, int numSamples, juce::int64 seed, double targetRmsDb, bool identicalChannels)
{
    juce::AudioBuffer<float> buf (channels, numSamples);
    juce::Random rng (seed);
    for (int ch = 0; ch < channels; ++ch)
    {
        if (identicalChannels && ch > 0)
        {
            buf.copyFrom (ch, 0, buf, 0, 0, numSamples);
            continue;
        }
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto* d = buf.getWritePointer (ch);
        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double white = rng.nextDouble() * 2.0 - 1.0;
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;
            d[i] = (float) pink;
            sumSq += pink * pink;
        }
        const double rms = std::sqrt (sumSq / numSamples);
        const double gain = std::pow (10.0, targetRmsDb / 20.0) / rms;
        for (int i = 0; i < numSamples; ++i)
            d[i] = (float) (d[i] * gain);
    }
    return buf;
}

std::complex<double> sineComponent (const float* x, int start, int length, double fs, double freq)
{
    // Least squares fit of a·cos + b·sin (+ handles non-integer periods).
    double cc = 0, ss = 0, cs = 0, xc = 0, xs = 0;
    const double w = juce::MathConstants<double>::twoPi * freq / fs;
    for (int i = start; i < start + length; ++i)
    {
        const double c = std::cos (w * i), s = std::sin (w * i);
        cc += c * c;
        ss += s * s;
        cs += c * s;
        xc += x[i] * c;
        xs += x[i] * s;
    }
    const double det = cc * ss - cs * cs;
    const double a = (xc * ss - xs * cs) / det;
    const double b = (xs * cc - xc * cs) / det;
    return { a, -b };
}

double sineAmplitude (const float* x, int start, int length, double fs, double freq)
{
    return std::abs (sineComponent (x, start, length, fs, freq));
}

double lufsOf (const juce::AudioBuffer<float>& buf, double fs, int start, int end)
{
    if (end < 0)
        end = buf.getNumSamples();
    double total = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        lens::dsp::KWeighting kw;
        kw.prepare (fs);
        const auto* d = buf.getReadPointer (ch);
        double sum = 0.0;
        for (int i = 0; i < end; ++i)
        {
            const double y = kw.tick (d[i]);
            if (i >= start)
                sum += y * y;
        }
        total += sum / (end - start);
    }
    return lens::dsp::ShortTermLoudness::msToLufs (total);
}

double rmsDb (const juce::AudioBuffer<float>& buf, int ch, int start, int end)
{
    if (end < 0)
        end = buf.getNumSamples();
    double sum = 0.0;
    const auto* d = buf.getReadPointer (ch);
    for (int i = start; i < end; ++i)
        sum += (double) d[i] * d[i];
    const double ms = sum / juce::jmax (1, end - start);
    return ms > 0 ? 10.0 * std::log10 (ms) : -300.0;
}

double maxStep (const juce::AudioBuffer<float>& buf, int ch, int start, int end)
{
    const auto* d = buf.getReadPointer (ch);
    double m = 0.0;
    for (int i = juce::jmax (1, start); i < end; ++i)
        m = juce::jmax (m, std::abs ((double) d[i] - d[i - 1]));
    return m;
}

std::complex<double> dftAt (const std::vector<double>& h, double fs, double freq)
{
    const double w = juce::MathConstants<double>::twoPi * freq / fs;
    // Rotating phasor in double; renormalised regularly.
    std::complex<double> acc (0.0, 0.0);
    const std::complex<double> step = std::polar (1.0, -w);
    std::complex<double> ph (1.0, 0.0);
    for (size_t n = 0; n < h.size(); ++n)
    {
        acc += h[n] * ph;
        ph *= step;
        if ((n & 1023) == 1023)
            ph = std::polar (1.0, -w * (double) (n + 1));
    }
    return acc;
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& in, int samples)
{
    juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
    out.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        if (samples < in.getNumSamples())
            out.copyFrom (ch, samples, in, ch, 0, in.getNumSamples() - samples);
    return out;
}
} // namespace lens::test
