// Phase 4: modulation engine.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "DSP/Envelope.h"
#include "DSP/Lfo.h"
#include "DSP/Mseg.h"
#include <cstdio>

using namespace prism;
using namespace prism::test;

static double midiHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

static int route (PrismProcessor& p, Src s, const std::string& dest, float depth, float curve = 0.0f, int aux = -1)
{
    const int slot = p.addRouting ((int) s, paramIndex (dest), depth);
    auto x = p.getExtras();
    x.routings[(size_t) slot].curve = curve;
    x.routings[(size_t) slot].aux = (int16_t) aux;
    p.setExtras (x);
    return slot;
}

static void clearRoutings (PrismProcessor& p)
{
    for (int s = 0; s < P::NumModSlots; ++s) p.removeRouting (s);
}

static void sinePatch (PrismProcessor& p)
{
    TableSource t; t.factory = "Basic Shapes"; p.setTableSource (0, t); waitForTables (p);
    setParam (p, "oscA_wt_pos", 0.0f);
    setParam (p, "oscA_rand_phase", 0.0f);
    setParam (p, "drift", 0.0f);
    setParam (p, "f1_enable", 0.0f);
    clearRoutings (p);
}

// instantaneous frequency per cycle (zero crossings)
static std::vector<std::pair<double, double>> cycleFreqs (const float* x, int n, double sr)
{
    std::vector<std::pair<double, double>> out;
    double last = -1;
    for (int i = 1; i < n; ++i)
        if (x[i - 1] < 0 && x[i] >= 0)
        {
            const double t = (i - 1) + (-x[i - 1] / (x[i] - x[i - 1]));
            if (last >= 0) out.push_back ({ t / sr, sr / (t - last) });
            last = t;
        }
    return out;
}

TEST_CASE ("Envelope stage timing within 1%", "[phase4]")
{
    const double sr = 48000.0;
    dsp::Envelope e;
    e.prepare (sr);
    dsp::Envelope::Times t;
    t.delay = 0.1f; t.attack = 0.5f; t.hold = 0.2f; t.decay = 0.3f; t.sustain = 0.4f; t.release = 0.7f;
    e.setTimes (t);
    e.noteOn();
    int n = 0, attackEnd = -1, decayEnd = -1, delayEnd = -1;
    while (n < (int) (3 * sr))
    {
        const float v = e.next();
        ++n;
        if (delayEnd < 0 && v > 0.0f) delayEnd = n - 1;
        if (attackEnd < 0 && v >= 1.0f) attackEnd = n;
        if (decayEnd < 0 && e.getStage() == dsp::Envelope::Stage::Sustain) { decayEnd = n; break; }
    }
    REQUIRE_THAT (delayEnd / sr, Catch::Matchers::WithinRel (0.1, 0.01));
    REQUIRE_THAT (attackEnd / sr, Catch::Matchers::WithinRel (0.6, 0.01));
    REQUIRE_THAT (decayEnd / sr, Catch::Matchers::WithinRel (1.1, 0.01));
    REQUIRE_THAT (e.getLevel(), Catch::Matchers::WithinAbs (0.4, 1e-4));
    e.noteOff();
    int r = 0;
    while (e.isActive()) { e.next(); ++r; }
    REQUIRE_THAT (r / sr, Catch::Matchers::WithinRel (0.7, 0.01));

    // retrigger starts from the current level (no reset)
    e.noteOn();
    for (int i = 0; i < (int) (0.3 * sr); ++i) e.next();
    const float before = e.getLevel();
    e.noteOn();
    const float after = e.next();
    REQUIRE (std::abs (after - before) < 0.002f);
}

TEST_CASE ("LFO shapes are bounded and cycle at the set rate", "[phase4]")
{
    for (int sh = 0; sh <= (int) LfoShape::Custom; ++sh)
    {
        dsp::Lfo l;
        l.prepare (48000.0);
        l.reseed (7);
        dsp::Lfo::Settings s;
        s.rateHz = 3.0f; s.shape = (LfoShape) sh; s.bipolar = true;
        l.set (s);
        l.trigger();
        auto custom = dsp::Shape::triangle();
        float mn = 9, mx = -9;
        for (int i = 0; i < 48000; ++i) { const float v = l.next (&custom); mn = std::min (mn, v); mx = std::max (mx, v); }
        INFO ("shape " << sh);
        REQUIRE (mn >= -1.0001f);
        REQUIRE (mx <= 1.0001f);
        REQUIRE (mx - mn > 0.3f);
    }
}

TEST_CASE ("MSEG one-shot, loop and sustain-loop", "[phase4]")
{
    dsp::Shape s = dsp::Shape::envelope();
    dsp::MsegPlayer m;
    m.trigger();
    for (int i = 0; i < 2000; ++i) m.next (s, MsegMode::OneShot, 1000.0);
    REQUIRE_THAT (m.current(), Catch::Matchers::WithinAbs (0.0, 1e-6)); // ended at the last point
    m.trigger();
    for (int i = 0; i < 5000; ++i) m.next (s, MsegMode::Loop, 1000.0);
    REQUIRE (m.position() < 0.5 + 1e-6); // still inside the loop region (0.1 .. 0.5)
    m.trigger();
    for (int i = 0; i < 5000; ++i) m.next (s, MsegMode::SustainLoop, 1000.0);
    REQUIRE (m.position() <= 0.5 + 1e-6);
    m.release();
    for (int i = 0; i < 2000; ++i) m.next (s, MsegMode::SustainLoop, 1000.0);
    REQUIRE (m.position() == 1.0);
}

TEST_CASE ("LFO phase locks to the host ppqPosition", "[phase4]")
{
    auto p = makeProcessor();
    setParam (*p, "lfo1_mode", (float) LfoMode::Free);
    setParam (*p, "lfo1_sync", 1.0f);
    setParam (*p, "lfo1_div", (float) syncDivIndex ("1/4"));
    route (*p, Src::Lfo1, "voice_pitch", 0.0f);
    HostTransport tr;
    tr.playing = true; tr.bpm = 123.0; tr.ppqAtZero = 3.37;
    RenderOptions o; o.transport = &tr; o.blocks = { 480, 333 };
    double worst = 0;
    int checks = 0;
    o.beforeBlock = [&] (PrismProcessor& pp, int64_t pos) {
        if (pos == 0) return;
        const double ppq = tr.ppqAtZero + (double) pos / 48000.0 * tr.bpm / 60.0;
        const double expect = ppq - std::floor (ppq);
        double d = std::abs (pp.engine.globalLfoPhase (0) - expect);
        d = std::min (d, 1.0 - d);
        worst = std::max (worst, d);
        ++checks;
    };
    render (*p, { { 0.0, 3.0, 60, 0.8f } }, 3.0, o);
    REQUIRE (checks > 100);
    REQUIRE (worst < 1e-4);
}

TEST_CASE ("Pitch test: LFO -> voice_pitch +-15 ct measures +-15 ct (+-1 ct)", "[phase4]")
{
    auto p = makeProcessor();
    sinePatch (*p);
    setParam (*p, "lfo1_rate", 2.0f);
    setParam (*p, "lfo1_shape", (float) LfoShape::Sine);
    route (*p, Src::Lfo1, "voice_pitch", 0.15f / 48.0f);
    auto b = render (*p, { { 0.0, 3.0, 69, 1.0f } }, 2.5);
    auto f = cycleFreqs (b.getReadPointer (0) + 4800, (int) (2.0 * 48000), 48000.0);
    double mn = 1e9, mx = -1e9;
    for (auto& [t, hz] : f) { const double c = 1200.0 * std::log2 (hz / 440.0); mn = std::min (mn, c); mx = std::max (mx, c); }
    std::printf ("  LFO pitch: %+.2f .. %+.2f cents\n", mn, mx);
    REQUIRE_THAT (mx, Catch::Matchers::WithinAbs (15.0, 1.0));
    REQUIRE_THAT (mn, Catch::Matchers::WithinAbs (-15.0, 1.0));
}

static double selfOscHz (std::function<void (PrismProcessor&)> setup)
{
    auto p = makeProcessor();
    sinePatch (*p);
    setParam (*p, "oscA_level", 0.0f);
    setParam (*p, "noise_level", 0.002f);
    setParam (*p, "f1_enable", 1.0f);
    setParam (*p, "f1_type", (float) FilterType::Ladder);
    setParam (*p, "f1_cutoff", 500.0f);
    setParam (*p, "f1_reso", 1.0f);
    setup (*p);
    auto b = render (*p, { { 0.0, 2.0, 60, 1.0f } }, 1.2);
    return estimateFrequency (b.getReadPointer (0) + 24000, 24000, 48000.0);
}

TEST_CASE ("Cutoff test: depth 0.1 moves the cutoff one octave (+-5%)", "[phase4]")
{
    const double base = selfOscHz ([] (PrismProcessor&) {});
    const double up = selfOscHz ([] (PrismProcessor& p) { route (p, Src::Macro1, "f1_cutoff", 0.1f); setParam (p, "macro1", 1.0f); });
    std::printf ("  cutoff: %.1f Hz -> %.1f Hz (ratio %.4f)\n", base, up, up / base);
    REQUIRE_THAT (up / base, Catch::Matchers::WithinRel (2.0, 0.05));
}

TEST_CASE ("Matrix: sum, clamp, curves, aux, macro to several destinations", "[phase4]")
{
    auto measure = [] (std::function<void (PrismProcessor&)> setup) {
        auto p = makeProcessor();
        sinePatch (*p);
        setup (*p);
        auto b = render (*p, { { 0.0, 1.0, 57, 1.0f } }, 0.6);
        return std::make_pair (estimateFrequency (b.getReadPointer (0) + 14400, 9600, 48000.0), rms (b, 0, 14400, 9600));
    };
    // macro 1 at 1.0, depth 0.125 -> +6 st; two such routings sum to +12 st
    auto one = measure ([] (PrismProcessor& p) { route (p, Src::Macro1, "voice_pitch", 0.125f); setParam (p, "macro1", 1.0f); });
    REQUIRE_THAT (one.first, Catch::Matchers::WithinRel (220.0 * std::pow (2.0, 0.5), 0.003));
    auto two = measure ([] (PrismProcessor& p) {
        route (p, Src::Macro1, "voice_pitch", 0.125f); route (p, Src::Macro2, "voice_pitch", 0.125f);
        setParam (p, "macro1", 1.0f); setParam (p, "macro2", 1.0f); });
    REQUIRE_THAT (two.first, Catch::Matchers::WithinRel (440.0, 0.003));
    // clamp: base +12 st (norm 0.75) plus +0.5 -> clamps at +24 st
    auto cl = measure ([] (PrismProcessor& p) {
        setParam (p, "voice_pitch", 12.0f);
        route (p, Src::Macro1, "voice_pitch", 0.5f); setParam (p, "macro1", 1.0f); });
    REQUIRE_THAT (cl.first, Catch::Matchers::WithinRel (880.0, 0.003));
    // curve +0.5: exponent 4^0.5 = 2 -> macro 0.5 contributes 0.25 * depth
    auto cv = measure ([] (PrismProcessor& p) { route (p, Src::Macro1, "voice_pitch", 0.25f, 0.5f); setParam (p, "macro1", 0.5f); });
    REQUIRE_THAT (cv.first, Catch::Matchers::WithinRel (220.0 * std::pow (2.0, 0.25 * 0.25 * 48 / 12.0), 0.003));
    // aux scales depth: macro2 = 0.5 halves the routing
    auto ax = measure ([] (PrismProcessor& p) {
        route (p, Src::Macro1, "voice_pitch", 0.25f, 0.0f, (int) Src::Macro2);
        setParam (p, "macro1", 1.0f); setParam (p, "macro2", 0.5f); });
    REQUIRE_THAT (ax.first, Catch::Matchers::WithinRel (220.0 * std::pow (2.0, 0.125 * 48 / 12.0), 0.003));
    // one macro to several destinations (pitch + level)
    auto base = measure ([] (PrismProcessor&) {});
    auto multi = measure ([] (PrismProcessor& p) {
        route (p, Src::Macro3, "voice_pitch", 0.25f); route (p, Src::Macro3, "oscA_level", -0.5f);
        setParam (p, "macro3", 1.0f); });
    REQUIRE_THAT (multi.first, Catch::Matchers::WithinRel (440.0, 0.003));
    REQUIRE_THAT (multi.second / base.second, Catch::Matchers::WithinRel (0.25 / 0.75, 0.02));
    // mod-on-mod: macro 4 drives the depth of the macro 1 routing
    auto mm = measure ([] (PrismProcessor& p) {
        const int s = route (p, Src::Macro1, "voice_pitch", 0.0f);
        route (p, Src::Macro4, "mod" + std::string (s + 1 < 10 ? "0" : "") + std::to_string (s + 1) + "_depth", 0.25f);
        setParam (p, "macro1", 1.0f); setParam (p, "macro4", 0.5f); });
    // depth param: base 0 -> norm 0.5, + 0.25 * 0.5 = 0.625 -> depth 0.25 -> +12 st
    REQUIRE_THAT (mm.first, Catch::Matchers::WithinRel (440.0, 0.003));
}

TEST_CASE ("Default routings in Init: Env2 -> F1 cutoff (0) and aftertouch -> F1 cutoff (+0.1)", "[phase4]")
{
    auto p = makeProcessor();
    const auto x = p->getExtras();
    REQUIRE (x.routings[0].src == (int16_t) Src::Env2);
    REQUIRE (x.routings[0].dest == (int16_t) P::filt (0, P::f_cutoff));
    REQUIRE (x.routings[1].src == (int16_t) Src::ChanAT);
    REQUIRE_THAT (getParam (*p, "mod01_depth"), Catch::Matchers::WithinAbs (0.0, 1e-6));
    REQUIRE_THAT (getParam (*p, "mod02_depth"), Catch::Matchers::WithinAbs (0.1, 1e-6));
}

TEST_CASE ("Parameter sweep: every parameter across its range during a held chord", "[phase4][sweep]")
{
    const auto& defs = paramDefs();
    double worstPre = 0; std::string worstId;
    for (int i = 0; i < P::NumParams; ++i)
    {
        auto p = makeProcessor();
        p->engine.preLimiterPeak.store (0.0f);
        const auto& d = defs[(size_t) i];
        RenderOptions o; o.blocks = { 256 };
        const double secs = 0.35;
        o.beforeBlock = [&] (PrismProcessor& pp, int64_t pos) {
            const float n = (float) std::min (1.0, pos / (secs * 48000.0 * 0.9));
            pp.param (i)->setValueNotifyingHost (n);
        };
        auto b = render (*p, { { 0.0, 1.0, 60, 0.9f }, { 0.0, 1.0, 64, 0.9f }, { 0.0, 1.0, 67, 0.9f } }, secs, o);
        bool finite = true;
        for (int c = 0; c < 2; ++c) for (int s = 0; s < b.getNumSamples(); ++s) finite &= std::isfinite (b.getSample (c, s));
        const double pre = p->engine.preLimiterPeak.load();
        if (pre > worstPre) { worstPre = pre; worstId = d.id; }
        INFO (d.id << " pre-limiter peak " << pre);
        REQUIRE (finite);
        REQUIRE (pre <= 2.0); // +6 dBFS
    }
    std::printf ("  parameter sweep: worst pre-limiter peak %.3f (%s)\n", worstPre, worstId.c_str());
}

TEST_CASE ("Block-size test with every LFO and envelope routed", "[phase4][blocksize]")
{
    auto run = [] (std::vector<int> blocks) {
        auto p = makeProcessor();
        clearRoutings (*p);
        setParam (*p, "drift", 0.5f);
        setParam (*p, "lfo1_shape", (float) LfoShape::SampleHold); setParam (*p, "lfo1_rate", 7.0f);
        setParam (*p, "lfo2_shape", (float) LfoShape::SmoothRandom); setParam (*p, "lfo2_mode", (float) LfoMode::Free);
        setParam (*p, "lfo3_shape", (float) LfoShape::Noise); setParam (*p, "lfo3_smooth", 0.4f);
        setParam (*p, "lfo4_shape", (float) LfoShape::Custom); setParam (*p, "lfo4_delay", 0.05f); setParam (*p, "lfo4_fade", 0.1f);
        setParam (*p, "env2_decay", 0.2f); setParam (*p, "env3_attack", 0.1f); setParam (*p, "env3_sustain", 0.5f);
        route (*p, Src::Lfo1, "oscA_wt_pos", 0.3f);
        route (*p, Src::Lfo2, "f1_cutoff", 0.2f);
        route (*p, Src::Lfo3, "oscA_pan", 0.5f);
        route (*p, Src::Lfo4, "voice_pitch", 0.01f);
        route (*p, Src::Env2, "f1_cutoff", 0.4f);
        route (*p, Src::Env3, "oscA_warp_amt", 0.5f);
        route (*p, Src::Mseg, "f1_reso", 0.5f);
        route (*p, Src::RandomNote, "oscA_pitch", 0.01f);
        route (*p, Src::Alternate, "oscA_pan", 0.3f);
        setParam (*p, "oscA_warp_mode", (float) WarpMode::BendPlus);
        RenderOptions o; o.blocks = std::move (blocks);
        return render (*p, { { 0.0, 0.8, 48, 0.9f }, { 0.13, 0.6, 55, 0.7f }, { 0.29, 0.5, 60, 0.5f } }, 1.1, o);
    };
    const auto ref = run ({ 64 });
    for (auto pat : std::vector<std::vector<int>> { { 1 }, { 517 }, { 4096 }, { 7, 100, 1, 333, 2048 } })
        REQUIRE (maxAbsDiff (ref, run (pat)) <= 1e-6);
}
