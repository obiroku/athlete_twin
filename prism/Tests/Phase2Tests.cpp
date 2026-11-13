// Phase 2: core voice - wavetable bandlimiting, glide, clicks, ladder, determinism.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "DSP/Wavetable.h"
#include "DSP/FactoryTables.h"
#include "DSP/WavetableOsc.h"
#include "DSP/FFT.h"
#include <cstdio>
#include <functional>

using namespace prism;
using namespace prism::test;

static double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

static void cleanPatch (PrismProcessor& p, const std::string& table, float pos)
{
    TableSource t; t.factory = table;
    p.setTableSource (0, t);
    setParam (p, "oscA_wt_pos", pos);
    setParam (p, "oscA_level", 0.75f);
    setParam (p, "oscA_rand_phase", 0.0f);
    setParam (p, "drift", 0.0f);
    setParam (p, "f1_enable", 0.0f);
    setParam (p, "env1_vel_amt", 0.0f);
    waitForTables (p);
}

// Worst non-harmonic level relative to the fundamental (dB)
static double worstNonHarmonic (const float* x, int n, double sr, double f0, double* fundDb = nullptr)
{
    auto db = spectrumDb (x, n);
    const double binHz = sr / n;
    const int k0 = (int) std::lround (f0 / binHz);
    double fund = -300;
    for (int k = std::max (1, k0 - 3); k <= k0 + 3; ++k) fund = std::max (fund, db[(size_t) k]);
    std::vector<bool> mask (db.size(), false);
    for (int k = 0; k < 8; ++k) mask[(size_t) k] = true; // DC
    for (int h = 1; h * f0 < sr / 2; ++h)
    {
        const int c = (int) std::lround (h * f0 / binHz);
        for (int k = c - 6; k <= c + 6; ++k) if (k >= 0 && k < (int) mask.size()) mask[(size_t) k] = true;
    }
    double worst = -300;
    for (size_t k = 0; k < db.size(); ++k) if (! mask[k]) worst = std::max (worst, db[k]);
    if (fundDb) *fundDb = fund;
    return worst - fund;
}

static double aliasingForNote (int note, const std::string& table = "Saw Sweep", float pos = 1.0f, int fftN = 32768,
                               const std::function<void (PrismProcessor&)>& setup = {})
{
    auto p = makeProcessor();
    cleanPatch (*p, table, pos);
    if (setup) setup (*p);
    const double sr = 48000.0;
    const double secs = 0.25 + (double) fftN / sr;
    auto b = render (*p, { { 0.0, secs + 1.0, note, 1.0f } }, secs);
    return worstNonHarmonic (b.getReadPointer (0) + (int) (0.2 * sr), fftN, sr, midiHz (note));
}

TEST_CASE ("Table hygiene: DC, 512 harmonics, single rotation, -1 dBFS", "[phase2]")
{
    for (int t = 0; t < (int) dsp::factoryTableNames().size(); ++t)
    {
        auto raw = dsp::generateFactoryFrames (t, 16);
        auto frames = raw;
        dsp::applyTableHygiene (frames);
        double peakAll = 0;
        for (auto& f : frames)
        {
            double dc = 0;
            for (float v : f) { dc += v; peakAll = std::max (peakAll, (double) std::abs (v)); }
            REQUIRE (std::abs (dc / f.size()) < 1e-4);
            auto h = dsp::realToHarmonics (f.data(), (int) f.size());
            double above = 0;
            for (size_t k = 513; k < h.size(); ++k) above = std::max (above, std::abs (h[k]));
            REQUIRE (above < 1e-5);
        }
        INFO (dsp::factoryTableNames()[(size_t) t]);
        REQUIRE_THAT (peakAll, Catch::Matchers::WithinAbs (0.891251, 1e-3));
        // frame 0 starts at an upward zero crossing
        REQUIRE (std::abs (frames[0][0]) < 0.02);
        REQUIRE (frames[0][3] > frames[0][2045]);
    }
}

TEST_CASE ("Mip level choice never lets the bright level alias", "[phase2]")
{
    for (double inc = 1e-5; inc < 0.5; inc *= 1.01)
    {
        const auto m = dsp::chooseMip ((float) inc);
        if (m.silent) continue;
        const int harm = dsp::Wavetable::harmonicsInLevel (m.level);
        // the brighter level's top harmonic is below Nyquist (allow the fastLog2 error margin)
        REQUIRE (harm * inc <= 0.5 * 1.01);
        if (m.wDark > 0.0f && m.level + 1 < dsp::Wavetable::NumLevels)
            REQUIRE (dsp::Wavetable::harmonicsInLevel (m.level + 1) * inc <= 0.5);
    }
}

TEST_CASE ("Aliasing: saw at MIDI 96 and 108 below -80 dB", "[phase2][alias]")
{
    for (int note : { 96, 108 })
    {
        const double a = aliasingForNote (note);
        std::printf ("  aliasing MIDI %d: %.1f dB\n", note, a);
        INFO ("MIDI " << note << " worst non-harmonic " << a << " dB");
        REQUIRE (a < -80.0);
    }
}

TEST_CASE ("Aliasing sweep: every note MIDI 24-108 below -70 dB", "[phase2][alias][slow]")
{
    double worst = -300; int worstNote = 0;
    for (int note = 24; note <= 108; ++note)
    {
        const double a = aliasingForNote (note, "Saw Sweep", 1.0f, note < 48 ? 65536 : 32768);
        if (a > worst) { worst = a; worstNote = note; }
        INFO ("MIDI " << note << ": " << a << " dB");
        REQUIRE (a < -70.0);
    }
    std::printf ("  aliasing sweep worst: %.1f dB at MIDI %d\n", worst, worstNote);
}

TEST_CASE ("Glide sweep 36 -> 120: no level jump > 0.5 dB between 10 ms windows", "[phase2][glide]")
{
    auto p = makeProcessor();
    cleanPatch (*p, "Saw Sweep", 1.0f);
    setParam (*p, "voice_mode", (float) VoiceMode::Legato);
    setParam (*p, "glide_time", 4.0f);
    setParam (*p, "glide_mode", 0.0f);
    const double sr = 48000.0, t0 = 0.3, T = 4.0;
    auto b = render (*p, { { 0.0, 5.0, 36, 1.0f }, { t0, 5.0, 120, 1.0f } }, t0 + T + 0.2);
    const float* x = b.getReadPointer (0);
    double prev = 0; bool have = false; double worst = 0; double worstAt = 0;
    for (double t = t0 + 0.05; t < t0 + T - 0.02; t += 0.01)
    {
        const double note = 36.0 + 84.0 * (t - t0) / T;
        const double f = midiHz (note);
        const int periods = (int) std::ceil (0.01 * f);
        const int len = (int) std::lround (periods * sr / f);
        const double db = 20.0 * std::log10 (rms (b, 0, (int) (t * sr), len) + 1e-12);
        if (have)
        {
            const double d = std::abs (db - prev);
            if (d > worst) { worst = d; worstAt = note; }
        }
        prev = db; have = true;
    }
    (void) x;
    std::printf ("  glide: worst 10 ms level step %.3f dB near MIDI %.1f\n", worst, worstAt);
    REQUIRE (worst <= 0.5);
}

static double clickRatio (float randPhase, const std::vector<Note>& notes, double secs, int poly = 16)
{
    auto p = makeProcessor();
    cleanPatch (*p, "Basic Shapes", 0.0f); // sine
    setParam (*p, "oscA_rand_phase", randPhase);
    setParam (*p, "poly", (float) poly);
    auto b = render (*p, notes, secs);
    // steady-state: a sine at A4 with the patch's level
    const double ss = maxDelta (b, 0, (int) (0.15 * 48000), 4800);
    return maxDelta (b, 0) / ss;
}

TEST_CASE ("Click test: note-on/off, steal, retrigger stay within 1.5x steady-state delta", "[phase2][click]")
{
    for (float rp : { 0.0f, 1.0f })
    {
        // note-on and note-off
        double r = clickRatio (rp, { { 0.0, 0.5, 69, 1.0f } }, 0.8);
        std::printf ("  click on/off rand %.0f%%: %.3f\n", rp * 100, r);
        REQUIRE (r <= 1.5);
        // retrigger the same key while it sounds (and while it releases)
        r = clickRatio (rp, { { 0.0, 0.3, 69, 1.0f }, { 0.3, 0.2, 69, 1.0f }, { 0.52, 0.2, 69, 1.0f } }, 0.9);
        std::printf ("  click retrigger rand %.0f%%: %.3f\n", rp * 100, r);
        REQUIRE (r <= 1.5);
        // steal: poly 1, a second key (other channel) takes the voice
        r = clickRatio (rp, { { 0.0, 0.6, 69, 1.0f, 1 }, { 0.3, 0.3, 69, 1.0f, 2 } }, 0.8, 1);
        std::printf ("  click steal rand %.0f%%: %.3f\n", rp * 100, r);
        REQUIRE (r <= 1.5);
    }
}

TEST_CASE ("Ladder self-oscillation tracks the keyboard within 5 cents (MIDI 48-96)", "[phase2][ladder]")
{
    double worst = 0; int worstNote = 0;
    for (int note = 48; note <= 96; note += 4)
    {
        auto p = makeProcessor();
        cleanPatch (*p, "Basic Shapes", 0.0f);
        setParam (*p, "oscA_level", 0.0f);
        setParam (*p, "noise_level", 0.002f);
        setParam (*p, "f1_enable", 1.0f);
        setParam (*p, "f1_type", (float) FilterType::Ladder);
        setParam (*p, "f1_cutoff", 261.63f);
        setParam (*p, "f1_keytrack", 1.0f);
        setParam (*p, "f1_reso", 1.0f);
        auto b = render (*p, { { 0.0, 2.0, note, 1.0f } }, 1.5);
        const double f = estimateFrequency (b.getReadPointer (0) + 48000, 24000, 48000.0);
        const double cents = 1200.0 * std::log2 (f / midiHz (note));
        INFO ("MIDI " << note << " -> " << f << " Hz (" << cents << " ct), peak " << peak (b));
        REQUIRE (peak (b, 48000, 24000) > 0.01);
        REQUIRE (std::abs (cents) <= 5.0);
        if (std::abs (cents) > worst) { worst = std::abs (cents); worstNote = note; }
    }
    std::printf ("  ladder self-oscillation worst %.2f cents at MIDI %d\n", worst, worstNote);
}

TEST_CASE ("Determinism: drift 100% and random phase 100% render bit-identically after reset", "[phase2]")
{
    auto p = makeProcessor();
    setParam (*p, "drift", 1.0f);
    setParam (*p, "oscA_rand_phase", 1.0f);
    setParam (*p, "oscA_unison", 5);
    setParam (*p, "noise_level", 0.1f);
    std::vector<Note> notes { { 0.0, 0.8, 60, 0.8f }, { 0.2, 0.6, 64, 0.6f }, { 0.4, 0.5, 67, 0.9f } };
    auto a = render (*p, notes, 1.2);
    p->prepareToPlay (48000.0, 512);
    auto b = render (*p, notes, 1.2);
    REQUIRE (maxAbsDiff (a, b) == 0.0);
}

TEST_CASE ("Block-size test on a unison + drift patch", "[phase2][blocksize]")
{
    auto run = [] (std::vector<int> blocks) {
        auto p = makeProcessor();
        TableSource t; t.factory = "Saw Sweep"; p->setTableSource (0, t); waitForTables (*p);
        setParam (*p, "oscA_unison", 7);
        setParam (*p, "oscA_wt_pos", 0.7f);
        setParam (*p, "oscA_wt_spread", 0.3f);
        setParam (*p, "drift", 1.0f);
        setParam (*p, "sub_level", 0.4f);
        setParam (*p, "f1_type", (float) FilterType::Ladder);
        setParam (*p, "f1_cutoff", 1200.0f);
        RenderOptions o; o.blocks = std::move (blocks);
        return render (*p, { { 0.0, 0.7, 48, 0.9f }, { 0.11, 0.5, 55, 0.6f }, { 0.37, 0.4, 62, 0.8f } }, 1.0, o);
    };
    const auto ref = run ({ 64 });
    for (auto pat : std::vector<std::vector<int>> { { 1 }, { 517 }, { 4096 }, { 3, 300, 1, 64, 1000, 7 } })
        REQUIRE (maxAbsDiff (ref, run (pat)) <= 1e-6);
}

TEST_CASE ("Voice modes: mono legato glides, poly stealing respects polyphony", "[phase2]")
{
    auto p = makeProcessor();
    setParam (*p, "poly", 4);
    std::vector<Note> notes;
    for (int i = 0; i < 8; ++i) notes.push_back ({ 0.01 * i, 1.0, 60 + i, 0.8f });
    render (*p, notes, 0.5);
    REQUIRE (p->engine.voiceCount.load() == 4);

    auto m = makeProcessor();
    cleanPatch (*m, "Basic Shapes", 0.0f);
    setParam (*m, "voice_mode", (float) VoiceMode::Mono);
    auto b = render (*m, { { 0.0, 1.0, 57, 1.0f }, { 0.3, 0.7, 69, 1.0f } }, 0.9);
    REQUIRE (m->engine.voiceCount.load() == 1);
    const double f = estimateFrequency (b.getReadPointer (0) + (int) (0.5 * 48000), 12000, 48000.0);
    REQUIRE_THAT (f, Catch::Matchers::WithinRel (440.0, 0.002));
}

TEST_CASE ("Pitch bend and voice_pitch shift the oscillator", "[phase2]")
{
    auto p = makeProcessor();
    cleanPatch (*p, "Basic Shapes", 0.0f);
    setParam (*p, "voice_pitch", 12.0f);
    auto b = render (*p, { { 0.0, 1.0, 57, 1.0f } }, 0.8);
    REQUIRE_THAT (estimateFrequency (b.getReadPointer (0) + 24000, 12000, 48000.0), Catch::Matchers::WithinRel (440.0, 0.002));

    auto q = makeProcessor();
    cleanPatch (*q, "Basic Shapes", 0.0f);
    setParam (*q, "pb_range", 12);
    auto c = render (*q, { { 0.0, 1.0, 57, 1.0f } }, 0.8, {}, { { 0.0, juce::MidiMessage::pitchWheel (1, 16383) } });
    REQUIRE_THAT (estimateFrequency (c.getReadPointer (0) + 24000, 12000, 48000.0), Catch::Matchers::WithinRel (220.0 * std::pow (2.0, 8191.0 / 8192.0), 0.003));
}
