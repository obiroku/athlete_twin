#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "helpers/OfflineRender.h"

using namespace lens::test;

TEST_CASE ("Pass-through is bit-identical at every block size", "[passthrough]")
{
    const std::vector<std::vector<int>> patterns { { 1 }, { 64 }, { 517 }, { 4096 }, variableBlockPattern() };

    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            const int n = 30000;
            const auto input = makeNoise (channels, n, 1234 + channels);
            const auto sidechain = makeNoise (2, n, 99);

            for (const auto& pattern : patterns)
            {
                INFO ("sr " << sr << ", channels " << channels << ", first block " << pattern.front()
                            << ", pattern length " << pattern.size());
                lens::LensProcessor proc;

                RenderOptions opts;
                opts.sampleRate = sr;
                opts.blockPattern = pattern;
                REQUIRE (bitIdentical (render (proc, input, opts), input));
                REQUIRE (proc.getLatencySamples() == 0);

                // Side-chain connected: the main path is still untouched.
                opts.sidechain = &sidechain;
                REQUIRE (bitIdentical (render (proc, input, opts), input));
            }
        }
}

TEST_CASE ("Pass-through ignores parameter values in Phase 1", "[passthrough]")
{
    lens::LensProcessor proc;
    juce::Random rng (5);
    for (auto* p : proc.getParameters())
        p->setValueNotifyingHost (rng.nextFloat());

    const auto input = makeSine (2, 20000, 48000.0, 1000.0);
    RenderOptions opts;
    opts.blockPattern = variableBlockPattern();
    REQUIRE (bitIdentical (render (proc, input, opts), input));
}

TEST_CASE ("Test helper: WAV write and read round-trip", "[helpers]")
{
    const auto file = juce::File::createTempFile (".wav");
    const auto input = makeSine (2, 4800, 48000.0, 440.0);
    REQUIRE (writeWav (file, input, 48000.0, 32));

    juce::AudioBuffer<float> loaded;
    double sr = 0.0;
    REQUIRE (readWav (file, loaded, sr));
    file.deleteFile();

    REQUIRE (sr == 48000.0);
    REQUIRE (loaded.getNumChannels() == 2);
    REQUIRE (loaded.getNumSamples() == 4800);
    REQUIRE (peakDifferenceDb (loaded, input) < -160.0);
}
