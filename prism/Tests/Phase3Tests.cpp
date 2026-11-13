// Phase 3: warp modes + oversampling, routing, remaining filters, factory tables, import.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "DSP/Wavetable.h"
#include "DSP/FactoryTables.h"
#include "DSP/Filters.h"
#include "DSP/FFT.h"
#include "DSP/Halfband.h"
#include "DSP/Common.h"
#include "Engine/TableIO.h"
#include <cstdio>

using namespace prism;
using namespace prism::test;

static double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

static double nonHarmonicDb (const float* x, int n, double sr, double f0)
{
    auto db = spectrumDb (x, n);
    const double binHz = sr / n;
    double fund = -300;
    const int k0 = (int) std::lround (f0 / binHz);
    for (int k = std::max (1, k0 - 3); k <= k0 + 3; ++k) fund = std::max (fund, db[(size_t) k]);
    std::vector<bool> mask (db.size(), false);
    for (int k = 0; k < 8; ++k) mask[(size_t) k] = true;
    for (int h = 1; h * f0 < sr / 2; ++h)
    {
        const int c = (int) std::lround (h * f0 / binHz);
        for (int k = c - 6; k <= c + 6; ++k) if (k >= 0 && k < (int) mask.size()) mask[(size_t) k] = true;
    }
    // audible band only (< 20 kHz): content just above Nyquist folds into 20-24 kHz
    double worst = -300;
    const size_t top = (size_t) (20000.0 / binHz);
    for (size_t k = 0; k < std::min (top, db.size()); ++k) if (! mask[k]) worst = std::max (worst, db[k]);
    return worst - fund;
}

TEST_CASE ("Warp aliasing at Eco / High / Ultra (report)", "[phase3][warp]")
{
    const char* names[] = { "None", "Sync", "Bend+", "Bend-", "Bend+/-", "PWM", "Mirror", "Asym+", "Asym-", "Quantize", "Remap", "FM (Other)", "FM (Sub)", "AM", "RM" };
    const int note = 84; // C6 in Logic naming (1046.5 Hz)
    std::printf ("\n  Warp aliasing below 20 kHz, saw (Saw Sweep pos 1), MIDI %d, warp 50%%, 48 kHz\n  %-11s %8s %8s %8s\n", note, "Mode", "Eco", "High", "Ultra");
    int highFails = 0, ultraFails = 0;
    for (int w = 1; w < (int) WarpMode::Count; ++w)
    {
        double res[3];
        for (int q = 0; q < 3; ++q)
        {
            auto p = makeProcessor();
            TableSource t; t.factory = "Saw Sweep"; p->setTableSource (0, t); p->setTableSource (1, t);
            waitForTables (*p);
            setParam (*p, "oscA_wt_pos", 1.0f);
            setParam (*p, "oscA_rand_phase", 0.0f);
            setParam (*p, "drift", 0.0f);
            setParam (*p, "f1_enable", 0.0f);
            setParam (*p, "oscA_warp_mode", (float) w);
            setParam (*p, "oscA_warp_amt", 0.5f);
            setParam (*p, "quality", (float) q);
            setParam (*p, "env1_vel_amt", 0.0f);
            // FM/AM/RM partner: osc B at the same pitch, silent in the mix (level 0)
            setParam (*p, "oscB_wt_pos", 0.0f);
            setParam (*p, "oscB_rand_phase", 0.0f);
            auto b = render (*p, { { 0.0, 2.0, note, 1.0f } }, 1.0);
            // FM from the sub (one octave down) is periodic at f0 / 2
            const double f0 = w == (int) WarpMode::FMSub ? midiHz (note) * 0.5 : midiHz (note);
            res[q] = nonHarmonicDb (b.getReadPointer (0) + 9600, 32768, 48000.0, f0);
        }
        std::printf ("  %-11s %8.1f %8.1f %8.1f\n", names[w], res[0], res[1], res[2]);
        if (res[1] > -50.0) ++highFails;
        if (res[2] > -65.0) ++ultraFails;
    }
    std::printf ("  modes over target: High %d, Ultra %d\n", highFails, ultraFails);
    CHECK (highFails == 0);
    CHECK (ultraFails == 0);
}

TEST_CASE ("Halfband filters: passband flat, stopband below -80 dB", "[phase3]")
{
    for (double f : { 0.01, 0.1, 0.2 })
    {
        dsp::HalfbandDecimator<8> d;
        double e = 0; int n = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const float y = d.process ((float) std::sin (2 * dsp::kPi * f * 2 * i), (float) std::sin (2 * dsp::kPi * f * (2 * i + 1)), dsp::steepCoefs());
            if (i > 1000) { e += y * y; ++n; }
        }
        REQUIRE (std::abs (10.0 * std::log10 (2 * e / n)) < 0.05);
    }
    for (double f : { 0.27, 0.35, 0.45 })
    {
        dsp::HalfbandDecimator<8> d;
        double e = 0; int n = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const float y = d.process ((float) std::sin (2 * dsp::kPi * f * 2 * i), (float) std::sin (2 * dsp::kPi * f * (2 * i + 1)), dsp::steepCoefs());
            if (i > 1000) { e += y * y; ++n; }
        }
        REQUIRE (10.0 * std::log10 (2 * e / n + 1e-30) < -80.0);
    }
}

TEST_CASE ("Reported latency is the same at Eco, High and Ultra", "[phase3]")
{
    auto p = makeProcessor();
    const int base = p->getLatencySamples();
    for (int q = 0; q < 3; ++q)
    {
        setParam (*p, "quality", (float) q);
        setParam (*p, "oscA_warp_mode", 2.0f);
        render (*p, { { 0.0, 0.1, 60, 1.0f } }, 0.1);
        REQUIRE (p->getLatencySamples() == base);
    }
    REQUIRE (base == 64);
}

// Routing: send osc A to each destination and check which filter shapes it.
TEST_CASE ("Routing: every destination option and serial/parallel", "[phase3][routing]")
{
    auto level = [] (int dest, int routing, bool f1Kills, bool f2Kills) {
        auto p = makeProcessor();
        TableSource t; t.factory = "Saw Sweep"; p->setTableSource (0, t); waitForTables (*p);
        setParam (*p, "oscA_wt_pos", 1.0f);
        setParam (*p, "drift", 0.0f);
        setParam (*p, "oscA_dest", (float) dest);
        setParam (*p, "filter_routing", (float) routing);
        setParam (*p, "f1_enable", 1.0f); setParam (*p, "f2_enable", 1.0f);
        setParam (*p, "f1_type", (float) FilterType::LP24); setParam (*p, "f2_type", (float) FilterType::LP24);
        setParam (*p, "f1_cutoff", f1Kills ? 20.0f : 20000.0f);
        setParam (*p, "f2_cutoff", f2Kills ? 20.0f : 20000.0f);
        setParam (*p, "f1_reso", 0.0f); setParam (*p, "f2_reso", 0.0f);
        auto b = render (*p, { { 0.0, 1.0, 69, 1.0f } }, 0.5);
        return rms (b, 0, 9600, 9600);
    };
    const double open = level (0, 0, false, false);
    REQUIRE (open > 0.05);
    // Serial: F1 -> F2
    REQUIRE (level (0, 0, true, false) < open * 0.05);   // F1 dest blocked by F1
    REQUIRE (level (0, 0, false, true) < open * 0.05);   // ... and by F2 after it
    REQUIRE (level (1, 0, true, false) > open * 0.8);    // F2 dest skips F1
    REQUIRE (level (1, 0, false, true) < open * 0.05);
    REQUIRE (level (3, 0, true, true) > open * 0.8);     // Direct skips both
    REQUIRE (level (2, 0, true, false) < open * 0.05);   // Both (serial) = through F1 then F2
    // Parallel
    REQUIRE (level (0, 1, false, true) > open * 0.8);    // F1 dest unaffected by F2
    REQUIRE (level (1, 1, true, false) > open * 0.8);
    const double both = level (2, 1, true, false);       // Both: half the energy through F2 only
    REQUIRE (both > open * 0.3);
    REQUIRE (both < open * 1.2);
    REQUIRE (level (3, 1, true, true) > open * 0.8);
}

TEST_CASE ("Formant filter: each vowel's three peaks within 5% of target", "[phase3][formant]")
{
    const double sr = 48000.0;
    const int n = 65536;
    for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        dsp::FilterCore f;
        f.prepare (sr);
        f.setType (FilterType::Formant);
        std::vector<float> ir ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float l = i == 0 ? 1.0f : 0.0f, r = l;
            f.process (l, r, 1000.0f, 0.5f, v);
            ir[(size_t) i] = l;
        }
        std::vector<dsp::cplx> a ((size_t) n);
        for (int i = 0; i < n; ++i) a[(size_t) i] = ir[(size_t) i];
        dsp::fft (a, false);
        const auto target = dsp::vowelAt (v);
        for (int k = 0; k < 3; ++k)
        {
            // strongest local maximum within +-10% of the target (a neighbour's skirt is not a peak)
            const double ft = target.f[k];
            int best = 0; double bm = 0;
            for (int b = (int) (ft * 0.9 / sr * n); b <= (int) (ft * 1.1 / sr * n); ++b)
            {
                const double m = std::abs (a[(size_t) b]);
                if (m > std::abs (a[(size_t) b - 1]) && m >= std::abs (a[(size_t) b + 1]) && m > bm) { bm = m; best = b; }
            }
            const double fp = best * sr / n;
            INFO ("vowel " << v << " formant " << k << ": target " << ft << " Hz, peak " << fp << " Hz");
            REQUIRE (std::abs (fp - ft) / ft <= 0.05);
        }
    }
}

TEST_CASE ("Filter types are stable and pass signal; type change crossfades without a click", "[phase3]")
{
    for (int t = 0; t < (int) FilterType::Count; ++t)
    {
        auto p = makeProcessor();
        TableSource s; s.factory = "Saw Sweep"; p->setTableSource (0, s); waitForTables (*p);
        setParam (*p, "oscA_wt_pos", 1.0f);
        setParam (*p, "f1_type", (float) t);
        setParam (*p, "f1_cutoff", 800.0f);
        setParam (*p, "f1_reso", 0.9f);
        auto b = render (*p, { { 0.0, 1.0, 48, 1.0f } }, 0.6);
        INFO ("filter type " << t);
        REQUIRE (std::isfinite (peak (b)));
        REQUIRE (rms (b, 0, 9600, 9600) > 1e-4);
    }
    // switching type mid-note: compare the delta at the switch with the steady state
    auto p = makeProcessor();
    setParam (*p, "oscA_rand_phase", 0.0f);
    setParam (*p, "f1_cutoff", 2000.0f);
    RenderOptions o; o.blocks = { 64 };
    o.beforeBlock = [] (PrismProcessor& pp, int64_t pos) { if (pos == 19200) setParam (pp, "f1_type", (float) FilterType::HP12); };
    auto b = render (*p, { { 0.0, 1.0, 69, 1.0f } }, 0.6, o);
    const double ss = maxDelta (b, 0, 9600, 4800);
    const double sw = maxDelta (b, 0, 19100, 2000);
    REQUIRE (sw <= ss * 1.5);
}

TEST_CASE ("Frame morph: no factory table drops more than 3 dB RMS between adjacent positions", "[phase3][tables]")
{
    for (int t = 0; t < (int) dsp::factoryTableNames().size(); ++t)
    {
        auto table = dsp::Wavetable::fromFrames (dsp::generateFactoryFrames (t), dsp::factoryTableNames()[(size_t) t]);
        const int F = table->numFrames();
        double prev = -1, worst = 0;
        // 4 sub-steps between frames, as the WT position sweeps 0 -> 1
        for (int s = 0; s <= (F - 1) * 4; ++s)
        {
            const double x = s / 4.0;
            const int f0 = std::min (F - 1, (int) x), f1 = std::min (F - 1, f0 + 1);
            const double u = x - f0;
            double e = 0;
            const float* a = table->frame0 (f0);
            const float* b = table->frame0 (f1);
            for (int i = 0; i < dsp::Wavetable::N; ++i) { const double v = a[i] + (b[i] - a[i]) * u; e += v * v; }
            const double db = 10.0 * std::log10 (e / dsp::Wavetable::N + 1e-20);
            if (prev > -1e9 && s > 0) worst = std::max (worst, prev - db);
            prev = db;
        }
        INFO (dsp::factoryTableNames()[(size_t) t] << ": worst drop " << worst << " dB");
        REQUIRE (worst <= 3.0);
    }
}

static juce::File tempDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("prism_tests");
    d.createDirectory();
    return d;
}

static void writeFloatWav (const juce::File& f, const std::vector<float>& x, double sr)
{
    juce::AudioBuffer<float> b (1, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i) b.setSample (0, i, x[(size_t) i]);
    REQUIRE (writeWav (f, b, sr));
}

static void checkHygiene (const ImportResult& r, int expectFrames)
{
    REQUIRE (r.ok);
    REQUIRE ((int) r.frames.size() == expectFrames);
    double pk = 0;
    for (auto& fr : r.frames)
    {
        double dc = 0;
        for (float v : fr) { dc += v; pk = std::max (pk, (double) std::abs (v)); }
        REQUIRE (std::abs (dc / fr.size()) < 0.001);
        auto h = dsp::realToHarmonics (fr.data(), (int) fr.size());
        for (size_t k = 513; k < h.size(); ++k) REQUIRE (std::abs (h[k]) < 1e-5);
    }
    REQUIRE_THAT (20.0 * std::log10 (pk), Catch::Matchers::WithinAbs (-1.0, 0.02));
}

TEST_CASE ("WAV import: fixed, pitch-detect, single-cycle and clm chunk", "[phase3][import]")
{
    const auto dir = tempDir();
    // fixed: 8 frames of 2048 with a DC offset and a harsh square (lots of harmonics)
    {
        std::vector<float> x (8 * 2048);
        for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] = 0.3f + ((i % 2048) < 1024 ? 0.9f : -0.5f) * (0.5f + (i / 2048) / 16.0f);
        writeFloatWav (dir.getChildFile ("fixed.wav"), x, 44100);
        checkHygiene (importWavetable (dir.getChildFile ("fixed.wav"), ImportMode::Fixed, 8), 8);
        checkHygiene (importWavetable (dir.getChildFile ("fixed.wav"), ImportMode::Auto), 8);
    }
    // pitch detect: a 220.5 Hz tone at 44.1 kHz (cycle 200 samples) for 1 s
    {
        std::vector<float> x (44100);
        for (int i = 0; i < (int) x.size(); ++i) x[(size_t) i] = 0.6f * std::sin (2 * 3.14159265 * i / 200.0) + 0.3f * std::sin (2 * 3.14159265 * 3 * i / 200.0);
        REQUIRE_THAT (detectCycleLength (x), Catch::Matchers::WithinAbs (200.0, 0.5));
        writeFloatWav (dir.getChildFile ("tone.wav"), x, 44100);
        checkHygiene (importWavetable (dir.getChildFile ("tone.wav"), ImportMode::PitchDetect), 220);
    }
    // single cycle of 600 samples
    {
        std::vector<float> x (600);
        for (int i = 0; i < 600; ++i) x[(size_t) i] = (float) (i / 300.0 - 1.0);
        writeFloatWav (dir.getChildFile ("single.wav"), x, 44100);
        checkHygiene (importWavetable (dir.getChildFile ("single.wav"), ImportMode::SingleCycle), 1);
    }
    // export -> clm chunk -> re-import identically
    {
        auto frames = dsp::generateFactoryFrames (dsp::factoryTableIndex ("Rezo"), 32);
        dsp::applyTableHygiene (frames);
        const auto f = dir.getChildFile ("export.wav");
        REQUIRE (exportWavetable (f, frames));
        REQUIRE (readClmFrameSize (f) == 2048);
        auto r = importWavetable (f, ImportMode::Auto);
        checkHygiene (r, 32);
        double md = 0;
        for (size_t k = 0; k < frames.size(); ++k)
            for (int i = 0; i < 2048; ++i) md = std::max (md, (double) std::abs (frames[k][(size_t) i] - r.frames[k][(size_t) i]));
        REQUIRE (md < 1e-4);
    }
}

TEST_CASE ("A preset with an embedded table round-trips on a clean user folder", "[phase3][import]")
{
    auto frames = dsp::generateFactoryFrames (dsp::factoryTableIndex ("Glass"), 16);
    dsp::applyTableHygiene (frames);
    auto p = makeProcessor();
    TableSource src; src.name = "My Glass"; src.frames = frames;
    p->setTableSource (0, src);
    waitForTables (*p);
    auto d = p->currentPreset();
    d.name = "Embedded Test";
    // write to a fresh folder and read back
    auto dir = tempDir().getChildFile ("clean_user");
    dir.deleteRecursively();
    dir.createDirectory();
    auto file = dir.getChildFile ("Embedded Test.prismpreset");
    REQUIRE (file.replaceWithText (presetToString (d, false)));
    PresetData back; juce::String err;
    REQUIRE (presetFromString (file.loadFileAsString(), back, err));
    REQUIRE (back.tables[0].factory.empty());
    REQUIRE (back.tables[0].name == "My Glass");
    REQUIRE (back.tables[0].frames.size() == frames.size());
    for (size_t k = 0; k < frames.size(); ++k)
        for (int i = 0; i < 2048; ++i) REQUIRE (back.tables[0].frames[k][(size_t) i] == frames[k][(size_t) i]);
    // and it plays
    auto q = makeProcessor();
    loadPresetNow (*q, back);
    auto b = render (*q, { { 0.0, 0.5, 60, 1.0f } }, 0.4);
    REQUIRE (peak (b) > 0.05);
}
