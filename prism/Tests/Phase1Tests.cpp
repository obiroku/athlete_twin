// Phase 1: scaffold, parameters, state, block-size independence.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"

using namespace prism;
using namespace prism::test;

TEST_CASE ("Parameter registry is consistent", "[phase1]")
{
    const auto& d = paramDefs();
    REQUIRE ((int) d.size() == P::NumParams);
    REQUIRE (paramIndex ("master_vol") == P::master_vol);
    REQUIRE (paramIndex ("oscB_wt_pos") == P::osc (1, P::wt_pos));
    REQUIRE (paramIndex ("f2_cutoff") == P::filt (1, P::f_cutoff));
    REQUIRE (paramIndex ("mod64_depth") == P::modDepth (63));
    // unique ids, defaults in range, norm round trip
    std::set<std::string> ids;
    for (auto& p : d)
    {
        REQUIRE (ids.insert (p.id).second);
        REQUIRE (p.def >= p.min);
        REQUIRE (p.def <= p.max);
        for (float n : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
            if (p.type == PType::Float)
                REQUIRE_THAT (p.toNorm (p.fromNorm (n)), Catch::Matchers::WithinAbs (n, 1e-4));
    }
    // cutoff is pure log: midpoint ~632 Hz, 0.1 normalized ~ 1 octave
    const auto& c = d[(size_t) P::filt (0, P::f_cutoff)];
    REQUIRE_THAT (c.fromNorm (0.5f), Catch::Matchers::WithinRel (632.46f, 0.01f));
    REQUIRE_THAT (std::log2 (c.fromNorm (0.6f) / c.fromNorm (0.5f)), Catch::Matchers::WithinAbs (0.9966, 0.01));
    // skew midpoints
    REQUIRE_THAT (d[(size_t) P::glide_time].fromNorm (0.5f), Catch::Matchers::WithinRel (0.2f, 0.001f));
    REQUIRE_THAT (d[(size_t) P::env (0, P::e_decay)].fromNorm (0.5f), Catch::Matchers::WithinRel (0.5f, 0.001f));
    REQUIRE_THAT (d[(size_t) P::osc (0, P::detune)].fromNorm (0.5f), Catch::Matchers::WithinRel (0.15f, 0.001f));
}

TEST_CASE ("Every parameter is exposed with version hint 1", "[phase1]")
{
    auto p = makeProcessor();
    const auto& all = p->getParameters();
    REQUIRE (all.size() == P::NumParams);
    for (auto* prm : all)
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        REQUIRE (r != nullptr);
        REQUIRE (r->getVersionHint() == 1);
    }
    REQUIRE (p->getLatencySamples() == 64);
}

TEST_CASE ("State round-trip: save -> load -> identical parameters", "[phase1]")
{
    auto a = makeProcessor();
    // change a spread of parameters and the non-parameter state
    juce::Random r (1234);
    const auto& defs = paramDefs();
    for (int i = 0; i < P::NumParams; i += 3)
        a->param (i)->setValueNotifyingHost (r.nextFloat());
    auto x = a->getExtras();
    x.routings[5].src = (int16_t) Src::Lfo2;
    x.routings[5].dest = (int16_t) P::osc (0, P::wt_pos);
    x.routings[5].curve = 0.3f;
    x.mseg.count = 5;
    x.mseg.pts[2] = { 0.4f, 0.8f, 0.2f };
    x.mseg.pts[3] = { 0.7f, 0.3f, -0.5f };
    x.mseg.pts[4] = { 1.0f, 0.0f, 0.0f };
    x.mseg.sanitize();
    x.fxOrder = { 8, 7, 6, 5, 4, 3, 2, 1, 0, 9, 10, 11 };
    a->setExtras (x);
    a->setMacroName (0, "Bark");

    juce::MemoryBlock mb;
    a->getStateInformation (mb);
    auto b = makeProcessor();
    b->setStateInformation (mb.getData(), (int) mb.getSize());

    for (int i = 0; i < P::NumParams; ++i)
    {
        INFO (defs[(size_t) i].id);
        const auto& def = defs[(size_t) i];
        const float va = def.snap (a->param (i)->convertFrom0to1 (a->param (i)->getValue()));
        const float vb = def.snap (b->param (i)->convertFrom0to1 (b->param (i)->getValue()));
        REQUIRE_THAT (vb, Catch::Matchers::WithinAbs (va, 1e-5 * std::max (1.0f, std::abs (va))));
    }
    const auto bx = b->getExtras();
    REQUIRE (bx.routings[5].src == (int16_t) Src::Lfo2);
    REQUIRE (bx.routings[5].dest == (int16_t) P::osc (0, P::wt_pos));
    REQUIRE_THAT (bx.routings[5].curve, Catch::Matchers::WithinAbs (0.3, 1e-6));
    REQUIRE (bx.mseg.count == 5);
    REQUIRE_THAT (bx.mseg.pts[3].y, Catch::Matchers::WithinAbs (0.3, 1e-6));
    REQUIRE (bx.fxOrder == x.fxOrder);
    REQUIRE (b->getMacroNames()[0] == "Bark");

    // a second save is numerically identical (float formatting may differ in the last digit)
    juce::MemoryBlock mb2;
    b->getStateInformation (mb2);
    const auto pa = juce::JSON::parse (mb.toString())["params"], pb = juce::JSON::parse (mb2.toString())["params"];
    for (auto& nv : pa.getDynamicObject()->getProperties())
    {
        const double x = nv.value, y = pb[nv.name];
        REQUIRE_THAT (y, Catch::Matchers::WithinAbs (x, 1e-5 * std::max (1.0, std::abs (x))));
    }
}

TEST_CASE ("State has format_version and migration chain accepts v1", "[phase1]")
{
    auto p = makeProcessor();
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    auto v = juce::JSON::parse (mb.toString());
    REQUIRE ((int) v["format_version"] == 1);
    auto* o = v.getDynamicObject();
    o->removeProperty ("format_version"); // pre-versioned file
    REQUIRE (migratePreset (*o) == kFormatVersion);
    PresetData d; juce::String err;
    REQUIRE (presetFromJson (v, d, err));
}

static juce::AudioBuffer<float> renderPattern (std::vector<int> blocks)
{
    auto p = makeProcessor();
    RenderOptions o;
    o.blocks = std::move (blocks);
    std::vector<Note> notes { { 0.01, 0.6, 60, 0.9f }, { 0.1234, 0.5, 64, 0.7f }, { 0.31, 0.4, 67, 0.5f }, { 0.52, 0.3, 72, 1.0f } };
    return render (*p, notes, 1.2, o);
}

TEST_CASE ("Block-size test: identical output for 1, 64, 517, 4096 and a variable pattern", "[phase1][blocksize]")
{
    const auto ref = renderPattern ({ 64 });
    REQUIRE (peak (ref) > 0.01);
    for (auto pattern : std::vector<std::vector<int>> { { 1 }, { 517 }, { 4096 }, { 13, 200, 1, 77, 999, 32, 5 } })
    {
        const auto b = renderPattern (pattern);
        INFO ("pattern starts with " << pattern[0]);
        REQUIRE (maxAbsDiff (ref, b) <= 1e-6);
    }
}

TEST_CASE ("Sine voice plays MIDI at the right pitch", "[phase1]")
{
    auto p = makeProcessor();
    const auto b = render (*p, { { 0.0, 1.0, 69, 1.0f } }, 1.0);
    const double f = estimateFrequency (b.getReadPointer (0) + 9600, 24000, 48000.0);
    REQUIRE_THAT (f, Catch::Matchers::WithinRel (440.0, 0.001));
}
