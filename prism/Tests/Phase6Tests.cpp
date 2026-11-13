// Phase 6: FX rack and master.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include <chrono>
#include <cstdio>

using namespace prism;
using namespace prism::test;

static const char* kFxOn[] = { "dist_on", "tape_on", "dim_on", "chorus_on", "phaser_on", "flanger_on", "rotary_on",
                               "delay_on", "reverb_on", "comp_on", "mb_on", "eq_on" };

TEST_CASE ("Latency: reported 64 samples equals the measured onset delay", "[phase6]")
{
    auto p = makeProcessor();
    setParam (*p, "oscA_rand_phase", 0.0f);
    setParam (*p, "oscA_phase", 0.25f); // starts at the peak: first sample is non-zero
    setParam (*p, "env1_attack", 0.0f);
    auto b = render (*p, { { 1000.0 / 48000.0, 0.5, 69, 1.0f } }, 0.1);
    int first = -1;
    for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 1e-6f) { first = i; break; }
    REQUIRE (first - 1000 == p->getLatencySamples());
}

TEST_CASE ("Tails: FX tails end and modules sleep; reverb freeze holds", "[phase6]")
{
    auto p = makeProcessor();
    for (auto id : kFxOn) setParam (*p, id, 1.0f);
    setParam (*p, "reverb_decay", 1.5f);
    setParam (*p, "delay_fb", 0.4f);
    setParam (*p, "delay_sync", 0.0f);
    setParam (*p, "delay_time", 250.0f);
    render (*p, { { 0.0, 0.3, 60, 1.0f } }, 12.0);
    REQUIRE (p->getTailLengthSeconds() > 1.0);
    REQUIRE (p->getTailLengthSeconds() <= 30.0);
    int sleeping = 0;
    for (int f = 0; f < NumFx; ++f) sleeping += p->engine.fxSleeping (f) ? 1 : 0;
    REQUIRE (sleeping == NumFx);
    auto tail = render (*p, {}, 0.5);
    REQUIRE (peak (tail) < 1e-6);

    // freeze: the tank keeps ringing indefinitely
    auto q = makeProcessor();
    setParam (*q, "reverb_on", 1.0f);
    setParam (*q, "reverb_mix", 1.0f);
    setParam (*q, "reverb_decay", 3.0f);
    render (*q, { { 0.0, 0.4, 60, 1.0f }, { 0.0, 0.4, 67, 1.0f } }, 0.6);
    setParam (*q, "reverb_freeze", 1.0f);
    auto early = render (*q, {}, 2.0);
    auto late = render (*q, {}, 30.0);
    const double e = rms (early, 0, 48000, 48000), l = rms (late, 0, late.getNumSamples() - 96000, 96000);
    std::printf ("  freeze: rms after 2.6 s %.4f, after 32 s %.4f (%.2f dB)\n", e, l, 20 * std::log10 (l / e));
    REQUIRE (e > 1e-3);
    REQUIRE (std::abs (20 * std::log10 (l / e)) < 3.0);
    REQUIRE_THAT (q->getTailLengthSeconds(), Catch::Matchers::WithinAbs (30.0, 1e-9));
    REQUIRE_FALSE (q->engine.fxSleeping ((int) FxId::Reverb));
}

TEST_CASE ("Denormals: loud input then 60 s of silence keeps CPU flat", "[phase6][slow]")
{
    auto p = makeProcessor (48000.0, 128);
    for (auto id : kFxOn) setParam (*p, id, 1.0f);
    setParam (*p, "reverb_decay", 8.0f);
    setParam (*p, "delay_fb", 0.6f);
    render (*p, { { 0.0, 1.0, 48, 1.0f }, { 0.0, 1.0, 60, 1.0f }, { 0.0, 1.0, 72, 1.0f } }, 1.5);
    std::vector<double> perSecond;
    RenderOptions o; o.blocks = { 128 };
    for (int s = 0; s < 60; ++s)
    {
        const auto t0 = std::chrono::steady_clock::now();
        render (*p, {}, 1.0, o);
        perSecond.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    }
    double firstMax = 0, lastMax = 0;
    for (int s = 0; s < 10; ++s) firstMax = std::max (firstMax, perSecond[(size_t) s]);
    for (int s = 50; s < 60; ++s) lastMax = std::max (lastMax, perSecond[(size_t) s]);
    std::printf ("  denormal test: first 10 s worst %.2f %%, last 10 s worst %.2f %% of one core\n", firstMax * 100, lastMax * 100);
    REQUIRE (lastMax <= firstMax * 1.5 + 0.002);
}

static double clickRatioDuring (std::function<void (PrismProcessor&, int64_t)> action)
{
    auto p = makeProcessor();
    setParam (*p, "oscA_rand_phase", 0.0f);
    setParam (*p, "drift", 0.0f);
    for (auto id : { "chorus_on", "delay_on", "dist_on", "eq_on" }) setParam (*p, id, 1.0f);
    setParam (*p, "dist_drive", 0.0f);
    RenderOptions o; o.blocks = { 64 };
    o.beforeBlock = std::move (action);
    auto b = render (*p, { { 0.0, 3.0, 69, 1.0f } }, 2.0, o);
    const double steady = maxDelta (b, 0, 24000, 24000);
    const double during = maxDelta (b, 0, 48000 - 512, 2048);
    return during / steady;
}

TEST_CASE ("Reorder and bypass while playing: no click", "[phase6][click]")
{
    const double bypass = clickRatioDuring ([] (PrismProcessor& p, int64_t pos) { if (pos == 48000) setParam (p, "chorus_on", 0.0f); });
    const double enable = clickRatioDuring ([] (PrismProcessor& p, int64_t pos) { if (pos == 48000) setParam (p, "phaser_on", 1.0f); });
    const double reorder = clickRatioDuring ([] (PrismProcessor& p, int64_t pos) {
        if (pos == 48000) { auto x = p.getExtras(); std::swap (x.fxOrder[0], x.fxOrder[7]); std::swap (x.fxOrder[3], x.fxOrder[11]); p.setExtras (x); }
    });
    std::printf ("  FX click ratios: bypass %.3f, enable %.3f, reorder %.3f\n", bypass, enable, reorder);
    REQUIRE (bypass <= 1.5);
    REQUIRE (enable <= 1.5);
    REQUIRE (reorder <= 1.5);
}

TEST_CASE ("Block-size test with every FX module on", "[phase6][blocksize]")
{
    auto run = [] (std::vector<int> blocks) {
        auto p = makeProcessor();
        for (auto id : kFxOn) setParam (*p, id, 1.0f);
        setParam (*p, "reverb_shimmer", 0.4f);
        setParam (*p, "dist_mode", (float) DistMode::Bitcrush);
        setParam (*p, "tape_hiss", 0.3f);
        setParam (*p, "bass_mono", 150.0f);
        setParam (*p, "master_width", 1.4f);
        RenderOptions o; o.blocks = std::move (blocks);
        return render (*p, { { 0.0, 0.5, 48, 0.9f }, { 0.21, 0.5, 64, 0.7f } }, 1.2, o);
    };
    const auto ref = run ({ 64 });
    for (auto pat : std::vector<std::vector<int>> { { 1 }, { 517 }, { 4096 }, { 9, 64, 300, 1, 2000 } })
        REQUIRE (maxAbsDiff (ref, run (pat)) <= 1e-6);
}

TEST_CASE ("Every FX module produces finite, bounded output and does something", "[phase6]")
{
    auto dry = makeProcessor();
    const auto ref = render (*dry, { { 0.0, 0.6, 57, 1.0f } }, 1.0);
    for (int f = 0; f < NumFx; ++f)
    {
        auto p = makeProcessor();
        setParam (*p, kFxOn[f], 1.0f);
        if (f == (int) FxId::EQ) { setParam (*p, "eq_ls_gain", 6.0f); setParam (*p, "eq_hp", 200.0f); }
        const auto b = render (*p, { { 0.0, 0.6, 57, 1.0f } }, 1.0);
        INFO (fxName (f));
        bool finite = true;
        for (int c = 0; c < 2; ++c) for (int s = 0; s < b.getNumSamples(); ++s) finite &= std::isfinite (b.getSample (c, s));
        REQUIRE (finite);
        REQUIRE (peak (b) < 0.89);
        REQUIRE (maxAbsDiff (ref, b) > 1e-4); // the module changes the sound
    }
}

TEST_CASE ("Master: bass mono collapses lows, limiter holds the ceiling", "[phase6]")
{
    auto p = makeProcessor();
    setParam (*p, "master_vol", 6.0f);
    setParam (*p, "softclip", 0.0f);
    setParam (*p, "oscA_unison", 8);
    setParam (*p, "oscA_spread", 1.0f);
    std::vector<Note> chord;
    for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 }) chord.push_back ({ 0.0, 1.0, n, 1.0f });
    const auto b = render (*p, chord, 1.0);
    REQUIRE (peak (b) <= 0.8913);
    // bass mono: a hard-panned low sub appears in both channels
    auto q = makeProcessor();
    setParam (*q, "oscA_level", 0.0f);
    setParam (*q, "sub_level", 0.8f);
    setParam (*q, "sub_pan", -1.0f);
    setParam (*q, "bass_mono", 200.0f);
    const auto m = render (*q, { { 0.0, 1.0, 48, 1.0f } }, 0.8);
    const double l = rms (m, 0, 19200, 9600), r = rms (m, 1, 19200, 9600);
    REQUIRE (r > 0.8 * l);
}
