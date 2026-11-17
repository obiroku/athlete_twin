#include "OfflineRender.h"

#include <cmath>
#include <cstring>

namespace lens::test
{
juce::AudioBuffer<float> render (juce::AudioProcessor& processor,
                                 const juce::AudioBuffer<float>& input,
                                 const RenderOptions& options)
{
    const int mainCh = input.getNumChannels();
    const int scCh = options.sidechain != nullptr ? options.sidechain->getNumChannels() : 0;
    const int numSamples = input.getNumSamples();
    jassert (mainCh == 1 || mainCh == 2);
    jassert (scCh >= 0 && scCh <= 2);
    jassert (scCh == 0 || options.sidechain->getNumSamples() == numSamples);
    jassert (! options.blockPattern.empty());

    const auto mainSet = juce::AudioChannelSet::canonicalChannelSet (mainCh);
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (mainSet);
    layout.outputBuses.add (mainSet);
    if (processor.getBusCount (true) > 1)
        layout.inputBuses.add (scCh > 0 ? juce::AudioChannelSet::canonicalChannelSet (scCh)
                                        : juce::AudioChannelSet::disabled());

    [[maybe_unused]] const bool layoutOk = processor.setBusesLayout (layout);
    jassert (layoutOk);

    int maxBlock = 1;
    for (int b : options.blockPattern)
        maxBlock = juce::jmax (maxBlock, b);

    processor.setNonRealtime (true);
    processor.setRateAndBufferSizeDetails (options.sampleRate, maxBlock);
    processor.prepareToPlay (options.sampleRate, maxBlock);

    const int totalCh = juce::jmax (processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
    juce::AudioBuffer<float> scratch (totalCh, maxBlock);
    juce::AudioBuffer<float> output (mainCh, numSamples);
    juce::MidiBuffer midi;

    int pos = 0;
    size_t patternIndex = 0;
    while (pos < numSamples)
    {
        const int n = juce::jmin (options.blockPattern[patternIndex], numSamples - pos);
        patternIndex = (patternIndex + 1) % options.blockPattern.size();
        if (n <= 0)
            continue;

        if (options.beforeBlock)
            options.beforeBlock (pos);

        juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(), totalCh, n);
        block.clear();
        for (int ch = 0; ch < mainCh; ++ch)
            block.copyFrom (ch, 0, input, ch, pos, n);
        for (int ch = 0; ch < scCh && mainCh + ch < totalCh; ++ch)
            block.copyFrom (mainCh + ch, 0, *options.sidechain, ch, pos, n);

        midi.clear();
        processor.processBlock (block, midi);

        for (int ch = 0; ch < mainCh; ++ch)
            output.copyFrom (ch, pos, block, ch, 0, n);

        pos += n;
    }

    processor.releaseResources();
    return output;
}

std::vector<int> variableBlockPattern()
{
    return { 1, 517, 64, 4096, 3, 128, 1024, 7, 2048, 31, 256, 1, 1, 4095, 512 };
}

juce::AudioBuffer<float> makeNoise (int channels, int numSamples, juce::int64 seed, float peak)
{
    juce::AudioBuffer<float> buf (channels, numSamples);
    juce::Random rng (seed);
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = buf.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = (rng.nextFloat() * 2.0f - 1.0f) * peak;
    }
    return buf;
}

juce::AudioBuffer<float> makeSine (int channels, int numSamples, double sampleRate, double freq, float peak)
{
    juce::AudioBuffer<float> buf (channels, numSamples);
    const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* d = buf.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = peak * (float) std::sin (w * (double) i);
    }
    return buf;
}

bool readWav (const juce::File& file, juce::AudioBuffer<float>& dest, double& sampleRate)
{
    juce::WavAudioFormat format;
    auto stream = file.createInputStream();
    if (stream == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (stream.release(), true));
    if (reader == nullptr)
        return false;

    sampleRate = reader->sampleRate;
    dest.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    return reader->read (&dest, 0, (int) reader->lengthInSamples, 0, true, true);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate, int bits)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    auto writer = format.createWriterFor (stream,
                                          juce::AudioFormatWriterOptions()
                                              .withSampleRate (sampleRate)
                                              .withNumChannels (buffer.getNumChannels())
                                              .withBitsPerSample (bits));
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

double peakDifferenceDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    jassert (a.getNumChannels() == b.getNumChannels() && a.getNumSamples() == b.getNumSamples());
    double peak = 0.0;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
    {
        auto* x = a.getReadPointer (ch);
        auto* y = b.getReadPointer (ch);
        for (int i = 0; i < a.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs ((double) x[i] - (double) y[i]));
    }
    return peak > 0.0 ? 20.0 * std::log10 (peak) : -300.0;
}
} // namespace lens::test
