// Phase 3: analyzer accuracy, peak finder, loudness, FIFO behaviour.
#include <catch2/catch_test_macros.hpp>

#include "DSP/EqBand.h"
#include "GUI/Analyzer.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"
#include "Util/Format.h"

#include <chrono>
#include <thread>

using namespace lens::test;
using lens::gui::SpectrumAnalyzer;

namespace
{
void feedAll (SpectrumAnalyzer& a, const juce::AudioBuffer<float>& b, int chunk = 4096,
              const std::function<void (double seconds)>& after = {})
{
    for (int pos = 0; pos < b.getNumSamples(); pos += chunk)
    {
        const int n = juce::jmin (chunk, b.getNumSamples() - pos);
        a.feed (SpectrumAnalyzer::Pre, b.getReadPointer (0, pos), n);
        if (after)
            after ((double) (pos + n) / 48000.0);
    }
}
} // namespace

TEST_CASE ("Analyzer: sines at 100 Hz, 1 kHz, 10 kHz within 1/24 octave and 1 dB", "[analyzer]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            SpectrumAnalyzer a;
            a.prepare (fs);
            a.setSmoothing (0);
            a.setTilt (0.0);
            const float amp = 0.25f; // -12.04 dBFS
            const auto sine = makeSine (1, (int) (fs * 1.5), fs, f, amp);
            for (int pos = 0; pos < sine.getNumSamples(); pos += 4096)
                a.feed (SpectrumAnalyzer::Pre, sine.getReadPointer (0, pos), juce::jmin (4096, sine.getNumSamples() - pos));

            double peakHz = 0.0;
            const double level = a.findPeak (SpectrumAnalyzer::Pre, f / 2, f * 2, peakHz);
            const double trueDb = 20.0 * std::log10 (amp);
            INFO ("fs " << fs << " f " << f << ": peak " << peakHz << " Hz at " << level << " dB (true " << trueDb << ")");
            REQUIRE (std::abs (std::log2 (peakHz / f)) <= 1.0 / 24.0);
            REQUIRE (std::abs (level - trueDb) <= 1.0);

            // The drawn trace (raw smoothing) also peaks within 1 dB.
            SpectrumAnalyzer::Snapshot snap;
            a.getSnapshot (snap);
            double best = -300.0, bestF = 0.0;
            for (size_t i = 0; i < snap.freqs.size(); ++i)
                if (snap.freqs[i] > f / 1.1 && snap.freqs[i] < f * 1.1 && snap.db[0][i] > best)
                {
                    best = snap.db[0][i];
                    bestF = snap.freqs[i];
                }
            REQUIRE (std::abs (best - trueDb) <= 1.0);
            REQUIRE (std::abs (std::log2 (bestF / f)) <= 1.0 / 24.0);
        }
}

TEST_CASE ("Peak finder: a 220 Hz resonance is marked within 3 s; plain pink noise gets no markers", "[analyzer][peaks]")
{
    const double fs = 48000.0;
    auto pink = makePink (1, (int) (fs * 10), 77, -20.0);

    SECTION ("No markers on plain pink noise")
    {
        SpectrumAnalyzer a;
        a.prepare (fs);
        int maxMarkers = 0;
        feedAll (a, pink, 4096, [&] (double) { maxMarkers = juce::jmax (maxMarkers, (int) a.peaks().size()); });
        REQUIRE (maxMarkers == 0);
    }

    SECTION ("Resonance at 220 Hz (+10 dB, Q 8)")
    {
        lens::dsp::EqBand res;
        res.prepare (fs, 1);
        lens::dsp::BandParams p;
        p.on = true;
        p.freq = 220.0f;
        p.gain = 10.0f;
        p.q = 8.0f;
        res.setParams (p, true);
        std::vector<double> x ((size_t) pink.getNumSamples()), dummy (x.size());
        for (int i = 0; i < pink.getNumSamples(); ++i)
            x[(size_t) i] = pink.getSample (0, i);
        res.process (x.data(), dummy.data(), (int) x.size(), nullptr);
        for (int i = 0; i < pink.getNumSamples(); ++i)
            pink.setSample (0, i, (float) x[(size_t) i]);

        SpectrumAnalyzer a;
        a.prepare (fs);
        double foundAt = -1.0, foundHz = 0.0;
        feedAll (a, pink, 2048, [&] (double t)
        {
            if (foundAt >= 0.0)
                return;
            for (const auto& m : a.peaks())
                if (std::abs (std::log2 (m.freq / 220.0)) <= 1.0 / 6.0)
                {
                    foundAt = t;
                    foundHz = m.freq;
                }
        });
        INFO ("found at " << foundAt << " s, " << foundHz << " Hz");
        REQUIRE (foundAt > 0.0);
        REQUIRE (foundAt <= 3.0);
        WARN ("Peak finder: marker at " << foundHz << " Hz after " << foundAt << " s");
    }
}

TEST_CASE ("Loudness: EBU Tech 3341 stereo 1 kHz at -23 dBFS reads -23.0 LUFS +/- 0.1", "[loudness]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        lens::LensProcessor proc;
        const auto tone = makeSine (2, (int) (fs * 4), fs, 1000.0, (float) std::pow (10.0, -23.0 / 20.0));
        RenderOptions o;
        o.sampleRate = fs;
        render (proc, tone, o);
        const float in = proc.stats().inputLufs.load();
        const float out = proc.stats().outputLufs.load();
        INFO ("fs " << fs << ": in " << in << " out " << out);
        REQUIRE (std::abs (in + 23.0f) <= 0.1f);
        REQUIRE (std::abs (out + 23.0f) <= 0.1f);
    }
}

TEST_CASE ("Audio thread never waits when the analyzer FIFOs are flooded", "[analyzer][realtime]")
{
    lens::LensProcessor proc;
    setEightMixedBands (proc);
    proc.setPlayConfigDetails (2, 2, 48000.0, 128);
    proc.prepareToPlay (48000.0, 128);

    auto& eng = proc.getEngine();
    // A slow consumer: the analyzer thread is running, but we also flood
    // the FIFOs by never letting it catch up for the first second.
    lens::gui::AnalyzerThread analyzer (eng.preFifo, eng.postFifo, eng.sidechainFifo);

    const auto noise = makeNoise (2, 128, 1);
    juce::AudioBuffer<float> block (4, 128);
    juce::MidiBuffer midi;
    double worstMs = 0.0;
    const long long droppedBefore = eng.preFifo.dropped();

    for (int i = 0; i < 4000; ++i) // ~10.7 s of audio
    {
        if (i == 1500)
            analyzer.start (48000.0);
        block.clear();
        block.copyFrom (0, 0, noise, 0, 0, 128);
        block.copyFrom (1, 0, noise, 1, 0, 128);
        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock (block, midi);
        const auto t1 = std::chrono::steady_clock::now();
        worstMs = std::max (worstMs, std::chrono::duration<double, std::milli> (t1 - t0).count());
    }
    analyzer.stop();
    const long long dropped = eng.preFifo.dropped() - droppedBefore;
    INFO ("worst block " << worstMs << " ms, dropped " << dropped << " samples");
    REQUIRE (dropped > 0);         // the FIFO really was full
    REQUIRE (worstMs < 2.0);       // a 128-sample block lasts 2.67 ms
    WARN ("FIFO flood: worst block " << worstMs << " ms with " << dropped << " samples dropped");
}

TEST_CASE ("Formatting: frequencies, gains and Logic note names", "[format]")
{
    REQUIRE (lens::fmt::freq (250.0) == "250 Hz");
    REQUIRE (lens::fmt::freq (2500.0) == "2.5 kHz");
    REQUIRE (lens::fmt::freq (10000.0) == "10 kHz");
    REQUIRE (lens::fmt::gain (-3.0) == lens::fmt::minus + "3.0 dB");
    REQUIRE (lens::fmt::gain (2.0) == "+2.0 dB");
    REQUIRE (lens::fmt::note (261.6256).name == "C3"); // middle C is C3 in Logic
    REQUIRE (lens::fmt::note (440.0).name == "A3");
    REQUIRE (lens::fmt::note (41.2).name == "E0");
    REQUIRE (lens::fmt::note (449.0).cents == 35);
}
