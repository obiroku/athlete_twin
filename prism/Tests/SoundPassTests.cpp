// Sound pass: dual warp, unison modes/stack/range, new filters, brightness.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "DSP/Filters.h"
#include "DSP/FFT.h"
#include <cstdio>

using namespace prism;
using namespace prism::test;

static double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

static void sawPatch (PrismProcessor& p)
{
    TableSource t; t.factory = "Saw Sweep"; p.setTableSource (0, t); p.setTableSource (1, t); waitForTables (p);
    setParam (p, "oscA_wt_pos", 1.0f); setParam (p, "oscA_rand_phase", 0.0f); setParam (p, "drift", 0.0f);
    setParam (p, "f1_enable", 0.0f); setParam (p, "env1_vel_amt", 0.0f);
}

static double bandDb (const juce::AudioBuffer<float>& b, double lo, double hi)
{
    const int n = 32768;
    auto db = spectrumDb (b.getReadPointer (0) + 9600, n);
    double e = 0;
    for (size_t k = 0; k < db.size(); ++k)
    {
        const double f = k * 48000.0 / n;
        if (f >= lo && f < hi) e += std::pow (10.0, db[k] / 10.0);
    }
    return 10.0 * std::log10 (e + 1e-30);
}

TEST_CASE ("Oscillators keep their top end: an A4 saw reaches beyond 19 kHz", "[sound]")
{
    auto p = makeProcessor();
    sawPatch (*p);
    auto b = render (*p, { { 0.0, 1.5, 69, 1.0f } }, 1.0);
    auto db = spectrumDb (b.getReadPointer (0) + 9600, 32768);
    // strongest partial between 19 and 21 kHz relative to the fundamental
    double top = -300, fund = -300;
    for (size_t k = 0; k < db.size(); ++k)
    {
        const double f = k * 48000.0 / 32768;
        if (std::abs (f - 440.0) < 5) fund = std::max (fund, db[k]);
        if (f > 19000 && f < 21000) top = std::max (top, db[k]);
    }
    std::printf ("  A4 saw: strongest partial 19-21 kHz at %.1f dB re fundamental\n", top - fund);
    REQUIRE (top - fund > -40.0); // a saw's 45th harmonic is -33 dB
}

TEST_CASE ("Dual warp: both warps act and stay clean at High quality", "[sound]")
{
    auto measure = [] (int w1, float a1, int w2, float a2, int q) {
        auto p = makeProcessor();
        sawPatch (*p);
        setParam (*p, "quality", (float) q);
        setParam (*p, "oscA_warp_mode", (float) w1); setParam (*p, "oscA_warp_amt", a1);
        setParam (*p, "oscA_warp2_mode", (float) w2); setParam (*p, "oscA_warp2_amt", a2);
        return render (*p, { { 0.0, 1.5, 72, 1.0f } }, 1.0);
    };
    const auto plain = measure (0, 0, 0, 0, 1);
    const auto one = measure ((int) WarpMode::BendPlus, 0.5f, 0, 0, 1);
    const auto two = measure ((int) WarpMode::BendPlus, 0.5f, (int) WarpMode::Drive, 0.6f, 1);
    REQUIRE (maxAbsDiff (plain, one) > 0.01);
    REQUIRE (maxAbsDiff (one, two) > 0.01);
    for (int w : { (int) WarpMode::Flip, (int) WarpMode::Drive, (int) WarpMode::Fold })
    {
        const auto b = measure ((int) WarpMode::Mirror, 0.4f, w, 0.5f, 1);
        bool finite = true;
        for (int s = 0; s < b.getNumSamples(); ++s) finite &= std::isfinite (b.getSample (0, s));
        REQUIRE (finite);
        REQUIRE (peak (b) < 0.9);
    }
}

TEST_CASE ("Unison: stack adds an octave, range widens detune, modes differ", "[sound]")
{
    auto run = [] (int stack, float range, int mode) {
        auto p = makeProcessor();
        sawPatch (*p);
        setParam (*p, "oscA_unison", 4); setParam (*p, "oscA_detune", 0.3f);
        setParam (*p, "oscA_uni_stack", (float) stack); setParam (*p, "oscA_uni_range", range); setParam (*p, "oscA_uni_mode", (float) mode);
        TableSource t; t.factory = "Basic Shapes"; p->setTableSource (0, t); waitForTables (*p);
        setParam (*p, "oscA_wt_pos", 0.0f); // sine: energy only near the played pitch (and its stacked octave)
        return render (*p, { { 0.0, 1.5, 57, 1.0f } }, 1.0);
    };
    const auto off = run (0, 1.0f, 0), oct = run (1, 1.0f, 0);
    const double octOff = bandDb (off, 400, 480), octOn = bandDb (oct, 400, 480);
    std::printf ("  stack +12: energy around 440 Hz %.1f dB -> %.1f dB\n", octOff, octOn);
    REQUIRE (octOn - octOff > 20.0);
    // range 6 st at detune 0.3: outer voices move from ~223 Hz to ~238 Hz
    const auto wide = run (0, 6.0f, 0);
    REQUIRE (bandDb (wide, 232, 245) - bandDb (off, 232, 245) > 20.0);
    REQUIRE (maxAbsDiff (run (0, 1.0f, 1), run (0, 1.0f, 2)) > 1e-3);
}

TEST_CASE ("New filters: LP6 slope, DJ centre is flat, Diffuser is all-pass", "[sound]")
{
    const double sr = 48000.0;
    auto response = [sr] (FilterType t, float fc, float reso, double f) {
        dsp::FilterCore core;
        core.prepare (sr);
        core.setType (t);
        double e = 0;
        for (int i = 0; i < 48000; ++i)
        {
            float l = (float) std::sin (2 * dsp::kPi * f * i / sr), r = l;
            core.process (l, r, fc, reso, 0.0f);
            if (i > 24000) e += l * l;
        }
        return 10.0 * std::log10 (e / 24000.0 / 0.5);
    };
    // LP6: one octave above cutoff is about -7 dB, three octaves about -18 dB
    REQUIRE_THAT (response (FilterType::LP6, 1000.0f, 0.0f, 8000.0), Catch::Matchers::WithinAbs (-18.0, 1.5));
    // DJ at the knob centre (~632 Hz) passes everything
    for (double f : { 60.0, 1000.0, 10000.0 }) REQUIRE (std::abs (response (FilterType::DJ, 632.5f, 0.0f, f)) < 0.3);
    REQUIRE (response (FilterType::DJ, 60.0f, 0.0f, 5000.0) < -20.0);   // turned left: low-pass
    REQUIRE (response (FilterType::DJ, 15000.0f, 0.0f, 100.0) < -20.0); // turned right: high-pass
    // Diffuser without feedback changes phase, not level
    for (double f : { 100.0, 1000.0, 8000.0 }) REQUIRE (std::abs (response (FilterType::Diffuser, 1000.0f, 0.0f, f)) < 0.3);
}
