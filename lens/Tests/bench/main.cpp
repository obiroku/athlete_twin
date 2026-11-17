// Headless benchmark. Prints the CPU model and the real-time factor of
// the processor at 48 kHz, 128-sample blocks, on one core (SPEC §13).
//
// Usage: lens_bench [seconds=30] [sampleRate=48000] [blockSize=128]

#include <chrono>
#include <cstdio>

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "helpers/OfflineRender.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double seconds = argc > 1 ? juce::String (argv[1]).getDoubleValue() : 30.0;
    const double sampleRate = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 48000.0;
    const int blockSize = argc > 3 ? juce::String (argv[3]).getIntValue() : 128;

    if (seconds <= 0.0 || sampleRate <= 0.0 || blockSize <= 0)
    {
        std::fprintf (stderr, "usage: lens_bench [seconds] [sampleRate] [blockSize]\n");
        return 1;
    }

    std::printf ("LENS benchmark\n");
    std::printf ("CPU: %s (%d logical cores)\n",
                 juce::SystemStats::getCpuModel().toRawUTF8(),
                 juce::SystemStats::getNumCpus());
    std::printf ("OS:  %s\n", juce::SystemStats::getOperatingSystemName().toRawUTF8());
    std::printf ("%.0f Hz, %d-sample blocks, stereo, %.1f s of audio\n\n", sampleRate, blockSize, seconds);

    struct Scenario
    {
        const char* name;
        // Later phases configure bands here.
    };
    const Scenario scenarios[] = { { "Phase 1 pass-through" } };

    const int numSamples = (int) (seconds * sampleRate);
    const auto input = lens::test::makeNoise (2, numSamples, 7, 0.25f);

    for (const auto& sc : scenarios)
    {
        lens::LensProcessor proc;
        lens::test::RenderOptions opts;
        opts.sampleRate = sampleRate;
        opts.blockPattern = { blockSize };

        (void) lens::test::render (proc, input, opts); // warm-up

        const auto t0 = std::chrono::steady_clock::now();
        const auto out = lens::test::render (proc, input, opts);
        const auto t1 = std::chrono::steady_clock::now();

        const double elapsed = std::chrono::duration<double> (t1 - t0).count();
        const double audio = (double) out.getNumSamples() / sampleRate;
        std::printf ("%-40s %8.1fx real time   %6.3f%% of one core\n",
                     sc.name, audio / elapsed, 100.0 * elapsed / audio);
    }

    std::printf ("\nNote: timings include the test harness's buffer copies.\n");
    return 0;
}
