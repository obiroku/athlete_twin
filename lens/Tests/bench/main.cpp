// Headless benchmark (SPEC §13). Prints the CPU model and, for each
// scenario, the real-time factor at 48 kHz with 128-sample blocks on one
// core. The analyzer runs on its own thread and is reported separately.
//
// Usage: lens_bench [seconds=20] [sampleRate=48000] [blockSize=128]

#include <chrono>
#include <cstdio>
#include <functional>

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

#if __has_include("GUI/Analyzer.h")
 #include "GUI/Analyzer.h"
 #define LENS_BENCH_ANALYZER 1
#endif

using namespace lens::test;

namespace
{
void fourDynamic (lens::LensProcessor& p)
{
    setEightMixedBands (p, false);
    // Bands 2, 3, 4 and 6 become dynamic (a shelf and three bells).
    setDyn (p, 2, { 0, -4.0f, -30.0f, 3.0f, 20.0f, 200.0f });
    setDyn (p, 3, { 0, -6.0f, -35.0f, 3.0f, 10.0f, 120.0f });
    setDyn (p, 4, { 0, 4.0f, -40.0f, 2.0f, 3.0f, 80.0f });
    setDyn (p, 6, { 1, -3.0f, -45.0f, 4.0f, 1.0f, 50.0f, false, true, 3000.0f, 9000.0f });
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double seconds = argc > 1 ? juce::String (argv[1]).getDoubleValue() : 20.0;
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
        std::function<void (lens::LensProcessor&)> setup;
        double budgetPercent;
    };
    const Scenario scenarios[] = {
        { "All bands off", [] (lens::LensProcessor&) {}, 0.0 },
        { "Zero Latency, 8 static bands", [] (lens::LensProcessor& p) { setEightMixedBands (p); }, 1.5 },
        { "Zero Latency, 8 bands incl. 4 dynamic", fourDynamic, 3.0 },
        { "Linear Phase Medium, 8 bands incl. 4 dynamic",
          [] (lens::LensProcessor& p)
          {
              fourDynamic (p);
              setParam (p, "phase_mode", 1.0f);
              setParam (p, "lp_resolution", 1.0f);
          },
          15.0 },
    };

    const int numSamples = (int) (seconds * sampleRate);
    const auto input = makePink (2, numSamples, 7, -16.0);
    bool allPass = true;

    for (const auto& sc : scenarios)
    {
        lens::LensProcessor proc;
        sc.setup (proc);
        proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);
        juce::AudioBuffer<float> block (4, blockSize);
        juce::MidiBuffer midi;
        auto runAll = [&]
        {
            for (int pos = 0; pos + blockSize <= numSamples; pos += blockSize)
            {
                block.clear();
                block.copyFrom (0, 0, input, 0, pos, blockSize);
                block.copyFrom (1, 0, input, 1, pos, blockSize);
                proc.processBlock (block, midi);
            }
        };
        runAll(); // warm-up (and let Linear Phase kernels settle)
        juce::Thread::sleep (200);

        const auto t0 = std::chrono::steady_clock::now();
        runAll();
        const auto t1 = std::chrono::steady_clock::now();
        const int processed = (numSamples / blockSize) * blockSize;
        const double elapsed = std::chrono::duration<double> (t1 - t0).count();
        const double audio = (double) processed / sampleRate;
        const double pct = 100.0 * elapsed / audio;
        const bool ok = sc.budgetPercent <= 0.0 || pct < sc.budgetPercent;
        allPass = allPass && ok;
        std::printf ("%-46s %8.1fx real time  %7.3f%% of one core", sc.name, audio / elapsed, pct);
        if (sc.budgetPercent > 0.0)
            std::printf ("   budget < %.1f%%  %s", sc.budgetPercent, ok ? "PASS" : "OVER");
        std::printf ("\n");
    }

#ifdef LENS_BENCH_ANALYZER
    {
        lens::gui::SpectrumAnalyzer analyzer;
        analyzer.prepare (sampleRate);
        std::vector<float> chunk (4096);
        const auto t0 = std::chrono::steady_clock::now();
        int pos = 0;
        while (pos + 4096 <= numSamples)
        {
            for (int i = 0; i < 4096; ++i)
                chunk[(size_t) i] = input.getSample (0, pos + i);
            analyzer.feed (lens::gui::SpectrumAnalyzer::Pre, chunk.data(), 4096);
            analyzer.feed (lens::gui::SpectrumAnalyzer::Post, chunk.data(), 4096);
            analyzer.feed (lens::gui::SpectrumAnalyzer::Sidechain, chunk.data(), 4096);
            pos += 4096;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("%-46s %8.1fx real time  %7.3f%% of one core (analyzer thread)\n", "Analyzer, 3 traces", seconds / elapsed,
                     100.0 * elapsed / seconds);
    }
#endif

    std::printf ("\nTimings cover processBlock only (plus a buffer copy per block). %s\n", allPass ? "All budgets met." : "Some budgets missed.");
    return 0;
}
