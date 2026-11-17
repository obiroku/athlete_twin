#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Params/Parameters.h"
#include "PluginProcessor.h"
#include "State/StateIO.h"

using Catch::Approx;
namespace P = lens::params;

namespace
{
void randomise (lens::LensProcessor& proc, juce::int64 seed)
{
    juce::Random rng (seed);
    for (auto* p : proc.getParameters())
        p->setValueNotifyingHost (rng.nextFloat());
}

// Compares every parameter. Choices and bools must match exactly; floats
// must match to float rounding of the normalise → denormalise trip.
void requireSameParameters (lens::LensProcessor& a, lens::LensProcessor& b)
{
    auto& pa = a.getParameters();
    auto& pb = b.getParameters();
    REQUIRE (pa.size() == pb.size());

    for (int i = 0; i < pa.size(); ++i)
    {
        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (pa[i]);
        auto* rb = dynamic_cast<juce::RangedAudioParameter*> (pb[i]);
        REQUIRE (ra != nullptr);
        REQUIRE (rb != nullptr);
        INFO (ra->paramID);

        if (dynamic_cast<juce::AudioParameterFloat*> (ra) != nullptr)
        {
            REQUIRE (rb->getValue() == Approx (ra->getValue()).margin (2e-6));
            const float va = ra->convertFrom0to1 (ra->getValue());
            const float vb = rb->convertFrom0to1 (rb->getValue());
            REQUIRE (vb == Approx (va).epsilon (1e-5).margin (1e-5));
        }
        else
        {
            // Bools and choices: compare what the plugin acts on (on/off,
            // choice index), not the raw normalised float.
            REQUIRE (juce::roundToInt (rb->convertFrom0to1 (rb->getValue()))
                     == juce::roundToInt (ra->convertFrom0to1 (ra->getValue())));
        }
    }
}

juce::MemoryBlock save (lens::LensProcessor& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

void load (lens::LensProcessor& p, const juce::MemoryBlock& mb)
{
    p.setStateInformation (mb.getData(), (int) mb.getSize());
}
} // namespace

TEST_CASE ("State round-trip: save → load → identical parameters", "[state]")
{
    for (juce::int64 seed : { 1, 2, 3, 42, 20261117 })
    {
        lens::LensProcessor a;
        randomise (a, seed);

        auto s = a.getSession();
        s.presetName = "My \"quoted\" preset \xe2\x80\x94 test";
        s.category = "Bass";
        s.note = "One line.";
        s.description = "Line one\nline two";
        s.trackType = "Choir & Vocal Bus";
        s.workflowStep = "SHAPE";
        s.analyzerSmoothing = "1/3";
        s.analyzerSpeed = "very fast";
        s.analyzerTilt = 3.0;
        s.showPre = false;
        s.showSidechain = false;
        s.guiScale = 1.5;
        a.setSession (s);

        const auto blob = save (a);

        lens::LensProcessor b;
        load (b, blob);
        requireSameParameters (a, b);

        const auto t = b.getSession();
        REQUIRE (t.presetName == s.presetName);
        REQUIRE (t.category == s.category);
        REQUIRE (t.note == s.note);
        REQUIRE (t.description == s.description);
        REQUIRE (t.trackType == s.trackType);
        REQUIRE (t.workflowStep == s.workflowStep);
        REQUIRE (t.analyzerSmoothing == s.analyzerSmoothing);
        REQUIRE (t.analyzerSpeed == s.analyzerSpeed);
        REQUIRE (t.analyzerTilt == s.analyzerTilt);
        REQUIRE (t.showPre == s.showPre);
        REQUIRE (t.showPost == s.showPost);
        REQUIRE (t.showSidechain == s.showSidechain);
        REQUIRE (t.guiScale == s.guiScale);

        // A second generation is stable: saving the loaded state and
        // loading it again changes nothing.
        lens::LensProcessor c;
        load (c, save (b));
        requireSameParameters (b, c);
    }
}

TEST_CASE ("State is JSON with format_version 1 and both A/B slots", "[state]")
{
    lens::LensProcessor a;
    a.getApvts().getParameter ("b03_on")->setValueNotifyingHost (1.0f);
    a.getApvts().getParameter ("ab_slot")->setValueNotifyingHost (1.0f);

    const auto root = juce::JSON::parse (a.getStateJson());
    REQUIRE (root.isObject());
    REQUIRE ((int) root["format_version"] == 1);
    for (auto* key : { "name", "category", "track_type", "note", "description", "bands", "globals", "ab", "analyzer", "gui_scale" })
    {
        INFO (key);
        REQUIRE (root.hasProperty (key));
    }
    REQUIRE (root["bands"].size() == 24);
    REQUIRE (root["ab"]["A"].isArray());
    REQUIRE (root["ab"]["B"].size() == 24); // B is active, so it mirrors the live bands
    REQUIRE (root["globals"]["ab_slot"].toString() == "B");
    REQUIRE (root["globals"]["phase_mode"].toString() == "Zero Latency");

    const auto band3 = root["bands"][2];
    REQUIRE ((int) band3["slot"] == 3);
    REQUIRE ((bool) band3["on"]);
    REQUIRE (band3["shape"].toString() == "Bell");
    REQUIRE (band3["slope"].toString() == "12");
    REQUIRE (band3["dyn"].isObject());
    REQUIRE (band3["det"]["source"].toString() == "Internal");
    REQUIRE (band3["spec"].hasProperty ("amount"));
}

TEST_CASE ("The inactive A/B slot survives a state round-trip", "[state]")
{
    lens::LensProcessor a;
    auto s = a.getSession();
    s.abBands[1] = juce::JSON::parse (R"([{"slot":5,"shape":"Notch","freq":440.0,"gain":0.0,"q":8.0}])");
    a.setSession (s);

    lens::LensProcessor b;
    b.setStateJson (a.getStateJson());
    const auto stored = b.getSession().abBands[1];
    REQUIRE (stored.size() == 1);
    REQUIRE ((int) stored[0]["slot"] == 5);
    REQUIRE (stored[0]["shape"].toString() == "Notch");
}

TEST_CASE ("Bad state never crashes and leaves parameters alone", "[state]")
{
    lens::LensProcessor p;
    p.getApvts().getParameter ("b01_gain")->setValueNotifyingHost (0.75f);
    const float before = p.getApvts().getParameter ("b01_gain")->getValue();

    for (const char* junk : { "", "not json", "[1,2,3]", "42", "\"str\"", "{", "\xff\xfe\x00garbage" })
    {
        p.setStateInformation (junk, (int) std::strlen (junk));
        REQUIRE (p.getApvts().getParameter ("b01_gain")->getValue() == before);
    }
    p.setStateInformation (nullptr, 0);

    // Wrong types, out-of-range values, unknown keys and slots are skipped.
    REQUIRE (p.setStateJson (R"({
        "format_version": 1,
        "mystery": {"x": 1},
        "track_type": "Kazoo",
        "gui_scale": 99,
        "globals": {"output_gain": "loud", "phase_mode": "Sideways", "lookahead": 500, "auto_gain": 3},
        "bands": [ 7, {"slot": 0}, {"slot": 99}, {"slot": "two"},
                   {"slot": 2, "freq": 1e9, "gain": "x", "shape": "Blob", "q": -4, "dyn": 5, "det": {"hp": null}} ]
    })"));
    auto& v = p.getApvts();
    REQUIRE (p.getSession().trackType == "Bus");
    REQUIRE (p.getSession().guiScale == 2.0);
    REQUIRE (v.getRawParameterValue ("output_gain")->load() == Approx (0.0f).margin (1e-5));
    REQUIRE (v.getRawParameterValue ("phase_mode")->load() == 0.0f);
    REQUIRE (v.getRawParameterValue ("lookahead")->load() == Approx (10.0f));
    REQUIRE (v.getRawParameterValue ("auto_gain")->load() == 1.0f);
    REQUIRE (v.getRawParameterValue ("b02_on")->load() == 1.0f);
    REQUIRE (v.getRawParameterValue ("b02_freq")->load() == Approx (30000.0f));
    REQUIRE (v.getRawParameterValue ("b02_q")->load() == Approx (0.025f));
    REQUIRE (v.getRawParameterValue ("b02_shape")->load() == 0.0f);
    REQUIRE (v.getRawParameterValue ("b01_gain")->load() == Approx (0.0f).margin (1e-5)); // reset: not listed
}

TEST_CASE ("Migration chain: v1 is current, missing version means v1, newer loads best-effort", "[state]")
{
    juce::var notObject (3);
    REQUIRE_FALSE (lens::state::migrate (notObject));

    auto v1 = juce::JSON::parse (R"({"format_version":1})");
    REQUIRE (lens::state::migrate (v1));
    REQUIRE ((int) v1["format_version"] == lens::state::currentFormatVersion);

    auto none = juce::JSON::parse (R"({"bands":[]})");
    REQUIRE (lens::state::migrate (none));
    REQUIRE ((int) none["format_version"] == lens::state::currentFormatVersion);

    auto future = juce::JSON::parse (R"({"format_version":7,"globals":{"output_gain":-3.0}})");
    REQUIRE (lens::state::migrate (future));
    REQUIRE ((int) future["format_version"] == 7);

    lens::LensProcessor p;
    REQUIRE (p.setStateJson (R"({"format_version":7,"globals":{"output_gain":-3.0},"from_the_future":true})"));
    REQUIRE (p.getApvts().getRawParameterValue ("output_gain")->load() == Approx (-3.0f));
}

TEST_CASE ("Preset JSON: format, round-trip, and what a preset may not touch", "[preset]")
{
    lens::LensProcessor a;
    auto& va = a.getApvts();
    auto set = [&] (const char* id, float value)
    {
        auto* p = va.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set ("b01_on", 1.0f);
    set ("b01_shape", 3.0f); // Low Cut
    set ("b01_freq", 35.0f);
    set ("b04_on", 1.0f);
    set ("b04_freq", 180.0f);
    set ("b04_q", 2.5f);
    set ("b04_dyn_on", 1.0f);
    set ("b04_dyn_range", -5.0f);
    set ("b04_dyn_thresh", -24.0f);
    set ("b04_dyn_attack", 8.0f);
    set ("b09_freq", 5000.0f); // off: not saved in a preset
    set ("output_gain", -1.5f);
    auto s = a.getSession();
    s.presetName = "Upright Bass Body";
    s.category = "Bass";
    s.trackType = "Bass";
    s.note = "Evens out boomy notes and brings out the wood.";
    a.setSession (s);

    const auto json = a.getPresetJson();
    const auto root = juce::JSON::parse (json);
    REQUIRE ((int) root["format_version"] == 1);
    REQUIRE (root["name"].toString() == "Upright Bass Body");
    REQUIRE (root["category"].toString() == "Bass");
    REQUIRE (root["track_type"].toString() == "Bass");
    REQUIRE (root["bands"].size() == 2);
    REQUIRE ((int) root["bands"][0]["slot"] == 1);
    REQUIRE (root["bands"][0]["shape"].toString() == "LowCut");
    REQUIRE ((int) root["bands"][1]["slot"] == 4);
    REQUIRE_FALSE (root["globals"].hasProperty ("bypass"));
    REQUIRE_FALSE (root["globals"].hasProperty ("ab_slot"));
    REQUIRE_FALSE (root.hasProperty ("ab"));
    REQUIRE_FALSE (root.hasProperty ("analyzer"));

    lens::LensProcessor b;
    auto& vb = b.getApvts();
    vb.getParameter ("bypass")->setValueNotifyingHost (1.0f);
    vb.getParameter ("ab_slot")->setValueNotifyingHost (1.0f);
    vb.getParameter ("b20_on")->setValueNotifyingHost (1.0f); // replaced by the preset
    REQUIRE (b.setPresetJson (json));

    REQUIRE (vb.getRawParameterValue ("bypass")->load() == 1.0f);
    REQUIRE (vb.getRawParameterValue ("ab_slot")->load() == 1.0f);
    REQUIRE (vb.getRawParameterValue ("b20_on")->load() == 0.0f);
    REQUIRE (vb.getRawParameterValue ("b01_on")->load() == 1.0f);
    REQUIRE (vb.getRawParameterValue ("b01_shape")->load() == 3.0f);
    REQUIRE (vb.getRawParameterValue ("b01_freq")->load() == Approx (35.0f).epsilon (1e-5));
    REQUIRE (vb.getRawParameterValue ("b04_q")->load() == Approx (2.5f).epsilon (1e-5));
    REQUIRE (vb.getRawParameterValue ("b04_dyn_on")->load() == 1.0f);
    REQUIRE (vb.getRawParameterValue ("b04_dyn_range")->load() == Approx (-5.0f));
    REQUIRE (vb.getRawParameterValue ("b04_dyn_thresh")->load() == Approx (-24.0f));
    REQUIRE (vb.getRawParameterValue ("b04_dyn_attack")->load() == Approx (8.0f).epsilon (1e-5));
    REQUIRE (vb.getRawParameterValue ("b09_freq")->load() == Approx (P::defaultBandFrequency (9)).epsilon (1e-5));
    REQUIRE (vb.getRawParameterValue ("output_gain")->load() == Approx (-1.5f));
    REQUIRE (b.getSession().trackType == "Bass");

    // Save → load → save gives the same preset text.
    REQUIRE (b.getPresetJson() == json);

    // A hand-written band without "on" is on.
    lens::LensProcessor c;
    REQUIRE (c.setPresetJson (R"({"format_version":1,"bands":[{"slot":6,"shape":"Tilt","freq":1000,"gain":1}]})"));
    REQUIRE (c.getApvts().getRawParameterValue ("b06_on")->load() == 1.0f);
    REQUIRE (c.getApvts().getRawParameterValue ("b06_shape")->load() == 7.0f);
    REQUIRE (c.getApvts().getRawParameterValue ("b06_freq")->load() == Approx (1000.0f).epsilon (1e-5));
}
