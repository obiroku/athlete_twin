// Phase 5: Coach logic (tips, recipes, explain), content, presets.
#include <catch2/catch_test_macros.hpp>

#include <BinaryData.h>

#include "Coach/CoachState.h"
#include "Coach/Explain.h"
#include "Coach/Recipes.h"
#include "Coach/Settings.h"
#include "Coach/Tips.h"
#include "Presets/PresetManager.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

#include <regex>
#include <set>

using namespace lens::test;
using lens::coach::CoachState;
using lens::dsp::Shape;

namespace
{
juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

lens::dsp::BandParams bell (float f, float g, float q = 1.0f)
{
    lens::dsp::BandParams b;
    b.on = true;
    b.shape = Shape::Bell;
    b.freq = f;
    b.gain = g;
    b.q = q;
    return b;
}

lens::dsp::BandParams dynBell (float f, float range = -5.0f, float thresh = -24.0f)
{
    auto b = bell (f, 0.0f, 2.0f);
    b.dynOn = true;
    b.dynRange = range;
    b.dynThresh = thresh;
    return b;
}

CoachState neutral()
{
    CoachState s;
    s.trackType = "Bus";
    s.inputLevelDb = -20.0f;
    s.inputLufs = s.outputLufs = -20.0f;
    s.midLevelDb = -20.0f;
    s.sideLevelDb = -30.0f;
    s.sidechainLevelDb = -200.0f;
    s.params.global.autoGain = true;
    s.params.bands[0] = bell (1000.0f, 2.0f);
    return s;
}

void runFor (lens::coach::TipEngine& e, CoachState s, double seconds, double start = 0.0)
{
    for (double t = start; t <= start + seconds + 1e-9; t += 0.25)
    {
        s.time = t;
        e.update (s);
    }
}

juce::String lessonsMd()
{
    return juce::File (LENS_SOURCE_DIR).getChildFile ("LESSONS.md").loadFileAsString();
}
} // namespace

// ---------------------------------------------------------------- tips

TEST_CASE ("Every tip fires on its trigger and not in a neutral state", "[coach][tips]")
{
    struct TipCase
    {
        const char* id;
        std::function<void (CoachState&)> trigger;
        double seconds;
    };
    const std::vector<TipCase> cases = {
        { "tip_big_boost", [] (CoachState& s) { s.params.bands[0].gain = 9.0f; }, 0 },
        { "tip_narrow_boost", [] (CoachState& s) { s.params.bands[0] = bell (3000, 3, 8); }, 0 },
        { "tip_louder_trap", [] (CoachState& s) { s.params.global.autoGain = false; s.outputLufs = -18.0f; }, 3.25 },
        { "tip_solo_long", [] (CoachState& s) { s.soloBand = 0; }, 15.25 },
        { "tip_dyn_never", [] (CoachState& s) { s.params.bands[1] = dynBell (200); s.activity[1] = 0.0f; }, 10.25 },
        { "tip_dyn_always", [] (CoachState& s) { s.params.bands[1] = dynBell (200); s.activity[1] = 95.0f; }, 10.25 },
        { "tip_lowcut_bass", [] (CoachState& s) { s.trackType = "Bass"; s.params.bands[1] = bell (60, 0); s.params.bands[1].shape = Shape::LowCut; }, 0 },
        { "tip_lowcut_master", [] (CoachState& s) { s.trackType = "Master"; s.params.bands[0].gain = 1.0f; s.params.bands[1] = bell (40, 0); s.params.bands[1].shape = Shape::LowCut; }, 0 },
        { "tip_master_big", [] (CoachState& s) { s.trackType = "Master"; s.params.bands[0].gain = 4.0f; }, 0 },
        { "tip_clip", [] (CoachState& s) { s.outputPeak = 1.2f; }, 0 },
        { "tip_lp_punch", [] (CoachState& s) { s.trackType = "Kick & Drums"; s.params.global.linearPhase = true; }, 0 },
        { "tip_lp_latency", [] (CoachState& s) { s.params.global.linearPhase = true; s.latencyMs = 170.7f; }, 0 },
        { "tip_low_release", [] (CoachState& s) { s.params.bands[1] = dynBell (100); s.params.bands[1].dynRelease = 30.0f; s.activity[1] = 20.0f; }, 0 },
        { "tip_sc_missing", [] (CoachState& s) { s.params.bands[1] = dynBell (60); s.params.bands[1].detExternal = true; s.activity[1] = 20.0f; }, 5.25 },
        { "tip_many_bands", [] (CoachState& s) { for (int b = 1; b <= 10; ++b) s.params.bands[(size_t) b] = bell (100.0f * (float) (b + 1) * (float) (b + 1), -1.0f); }, 0 },
        { "tip_stacked", [] (CoachState& s) { s.params.bands[1] = bell (1100, 2); }, 0 },
        { "tip_sibilance_attack", [] (CoachState& s) { s.trackType = "Vocal"; s.params.bands[1] = dynBell (6500); s.params.bands[1].dynAttack = 20.0f; s.activity[1] = 20.0f; }, 0 },
        { "tip_mono_side", [] (CoachState& s) { s.params.bands[1] = bell (8000, 2); s.params.bands[1].stereo = lens::dsp::StereoMode::Side; s.sideLevelDb = -70.0f; }, 5.25 },
        { "tip_shelf_q", [] (CoachState& s) { s.params.bands[0] = bell (8000, 2, 2.0f); s.params.bands[0].shape = Shape::HighShelf; }, 0 },
        { "tip_try_delta", [] (CoachState& s) { s.params.bands[1] = dynBell (3000); s.activity[1] = 20.0f; }, 0 },
        { "tip_master_order", [] (CoachState& s) { s.trackType = "Master"; s.params.bands[0].gain = 1.0f; }, 0 },
    };

    // Every tip in LESSONS.md has a case.
    std::set<juce::String> covered;
    for (const auto& c : cases)
        covered.insert (c.id);
    for (const auto& t : lens::coach::Content::get().tips)
    {
        INFO (t.id);
        REQUIRE (covered.count (t.id) == 1);
    }

    // Neutral: nothing fires, even after 20 s.
    {
        lens::coach::TipEngine e;
        runFor (e, neutral(), 20.0);
        for (const auto& t : lens::coach::Content::get().tips)
        {
            INFO ("neutral " << t.id);
            REQUIRE_FALSE (e.isFiring (t.id));
        }
    }

    for (const auto& c : cases)
    {
        INFO (c.id);
        auto s = neutral();
        c.trigger (s);

        lens::coach::TipEngine e;
        // Prime with a neutral state first (for transition tips), then run.
        runFor (e, neutral(), 0.0);
        if (c.seconds > 0.5)
        {
            runFor (e, s, c.seconds - 1.0, 0.25);
            REQUIRE_FALSE (e.isFiring (c.id)); // not yet
        }
        lens::coach::TipEngine e2;
        runFor (e2, neutral(), 0.0);
        runFor (e2, s, c.seconds, 0.25);
        REQUIRE (e2.isFiring (c.id));
        REQUIRE_FALSE (e2.text (c.id).contains ("{"));
        REQUIRE_FALSE (e2.text (c.id).contains ("TODO"));
    }
}

TEST_CASE ("Tip text fills its values; Got it, Don't show again and Quiet hide tips", "[coach][tips]")
{
    auto s = neutral();
    s.params.global.autoGain = false;
    s.outputLufs = -17.5f;
    lens::coach::TipEngine e;
    runFor (e, s, 4.0);
    REQUIRE (e.text ("tip_louder_trap").startsWith ("LENS is making this louder by 2.5 dB."));
    s.outputLufs = -22.0f;
    runFor (e, s, 4.0, 5.0);
    REQUIRE (e.text ("tip_louder_trap").startsWith ("LENS is making this quieter by 2.0 dB."));

    auto d = neutral();
    d.params.bands[4] = dynBell (300);
    d.activity[4] = 0.0f;
    lens::coach::TipEngine e3;
    runFor (e3, d, 11.0);
    REQUIRE (e3.text ("tip_dyn_never").startsWith ("Band 5 isn't moving."));

    REQUIRE (e.visible().size() == 1);
    e.gotIt ("tip_louder_trap");
    REQUIRE (e.visible().empty());

    lens::coach::TipEngine e4;
    auto big = neutral();
    big.params.bands[0].gain = 10.0f;
    runFor (e4, big, 0.0);
    REQUIRE (e4.visible().size() == 1);
    e4.setNeverShow ({ "tip_big_boost" });
    REQUIRE (e4.visible().empty());
    e4.setNeverShow ({});
    e4.setQuiet (true);
    REQUIRE (e4.visible().empty());
}

// ---------------------------------------------------------------- recipes

TEST_CASE ("Every recipe step's completion check passes on its trigger and fails without it", "[coach][recipes]")
{
    const auto& content = lens::coach::Content::get();
    std::set<juce::String> tested;

    auto base = [] { auto s = neutral(); s.params.bands[0].on = false; return s; };

    struct StepCase
    {
        juce::String key;
        std::function<void (CoachState&)> setup;                                  // before entering the step
        std::function<void (CoachState&, lens::coach::RecipeRunner&)> pass;       // makes it pass
    };

    auto setTrack = [] (const char* t) { return [t] (CoachState& s, lens::coach::RecipeRunner&) { s.trackType = t; }; };
    auto events = [] (std::vector<const char*> ev) { return [ev] (CoachState&, lens::coach::RecipeRunner& r) { for (auto* e : ev) r.event (e); }; };
    auto nop = [] (CoachState&) {};

    const std::vector<StepCase> cases = {
        { "rec_first_eq:1", nop, [] (CoachState& s, auto&) { s.params.bands[3] = bell (500, 0); } },
        { "rec_first_eq:2", [] (CoachState& s) { s.params.bands[3] = bell (500, 0); }, [] (CoachState& s, auto&) { s.params.bands[3].gain = 4.0f; } },
        { "rec_first_eq:3", [] (CoachState& s) { s.params.bands[3] = bell (500, 0); }, [] (CoachState& s, auto&) { s.params.bands[3].q = 3.0f; } },
        { "rec_first_eq:4", nop, events ({ "bypass toggled", "bypass toggled" }) },
        { "rec_first_eq:5", [] (CoachState& s) { s.params.global.autoGain = false; }, [] (CoachState& s, auto&) { s.params.global.autoGain = true; } },

        { "rec_boomy_notes:1", nop, setTrack ("Bass") },
        { "rec_boomy_notes:2", [] (CoachState& s) { s.inputLevelDb = -80.0f; }, [] (CoachState& s, auto&) { s.inputLevelDb = -20.0f; } },
        { "rec_boomy_notes:3", nop, [] (CoachState& s, auto&) { s.params.bands[2] = bell (150, 0); } },
        { "rec_boomy_notes:4", [] (CoachState& s) { s.params.bands[2] = bell (150, 0); }, [] (CoachState& s, auto&) { s.params.bands[2] = dynBell (150, -5); } },
        { "rec_boomy_notes:5", [] (CoachState& s) { s.params.bands[2] = dynBell (150); }, events ({ "learn finished" }) },
        { "rec_boomy_notes:6", [] (CoachState& s) { s.params.bands[2] = dynBell (150); s.params.bands[2].dynAttack = 1.0f; },
          [] (CoachState& s, auto&) { s.params.bands[2].dynAttack = 10.0f; s.params.bands[2].dynRelease = 120.0f; } },
        { "rec_boomy_notes:7", [] (CoachState& s) { s.params.bands[2] = dynBell (150); s.activity[2] = 80.0f; }, [] (CoachState& s, auto&) { s.activity[2] = 25.0f; } },
        { "rec_boomy_notes:8", nop, events ({ "delta on", "delta off" }) },

        { "rec_deess:1", nop, setTrack ("Choir & Vocal Bus") },
        { "rec_deess:2", nop, [] (CoachState& s, auto&) { s.params.bands[5] = dynBell (6500, -6); } },
        { "rec_deess:3", [] (CoachState& s) { s.params.bands[5] = dynBell (6500, -6); },
          [] (CoachState& s, auto&) { auto& b = s.params.bands[5]; b.detCustom = true; b.detHp = 5000.0f; b.detLp = 10000.0f; } },
        { "rec_deess:4", nop, events ({ "listen on", "listen off" }) },
        { "rec_deess:5", [] (CoachState& s) { s.params.bands[5] = dynBell (6500); }, events ({ "learn finished" }) },
        { "rec_deess:6", [] (CoachState& s) { s.params.bands[5] = dynBell (6500); },
          [] (CoachState& s, auto&) { s.params.bands[5].dynAttack = 1.0f; s.params.bands[5].dynRelease = 50.0f; } },
        { "rec_deess:7", nop, events ({ "delta on", "delta off" }) },

        { "rec_kick_bass:1", nop, [] (CoachState& s, auto&) { s.sidechainLevelDb = -20.0f; } },
        { "rec_kick_bass:2", nop, [] (CoachState& s, auto&) { s.params.bands[1] = dynBell (60); } },
        { "rec_kick_bass:3", [] (CoachState& s) { s.params.bands[1] = dynBell (60); }, [] (CoachState& s, auto&) { s.params.bands[1].detExternal = true; } },
        { "rec_kick_bass:4", [] (CoachState& s) { s.params.bands[1] = dynBell (60); }, [] (CoachState& s, auto&) { s.params.bands[1].detCustom = true; } },
        { "rec_kick_bass:5", [] (CoachState& s) { s.params.bands[1] = dynBell (60); }, events ({ "learn finished" }) },
        { "rec_kick_bass:6", [] (CoachState& s) { s.params.bands[1] = dynBell (60); },
          [] (CoachState& s, auto&) { s.params.bands[1].dynAttack = 1.0f; s.params.bands[1].dynRelease = 120.0f; } },

        { "rec_vocal_pocket:1", nop, [] (CoachState& s, auto&) { s.sidechainLevelDb = -20.0f; } },
        { "rec_vocal_pocket:2", nop, [] (CoachState& s, auto&) { s.params.bands[1] = dynBell (2500, -3); } },
        { "rec_vocal_pocket:3", [] (CoachState& s) { s.params.bands[1] = dynBell (2500); }, [] (CoachState& s, auto&) { s.params.bands[1].detExternal = true; } },
        { "rec_vocal_pocket:4", [] (CoachState& s) { s.params.bands[1] = dynBell (2500); }, events ({ "learn finished" }) },
        { "rec_vocal_pocket:5", [] (CoachState& s) { s.params.bands[1] = dynBell (2500); s.params.bands[1].dynRelease = 50.0f; },
          [] (CoachState& s, auto&) { s.params.bands[1].dynRelease = 200.0f; } },

        { "rec_harsh_hits:1", [] (CoachState& s) { s.inputLevelDb = -80.0f; }, [] (CoachState& s, auto&) { s.inputLevelDb = -20.0f; } },
        { "rec_harsh_hits:2", nop, [] (CoachState& s, auto&) { s.params.bands[2] = bell (3000, 10, 8); } },
        { "rec_harsh_hits:3", [] (CoachState& s) { s.params.bands[2] = bell (3000, 10, 8); },
          [] (CoachState& s, auto&) { s.params.bands[2].gain = -4.0f; s.params.bands[2].q = 2.0f; } },
        { "rec_harsh_hits:4", [] (CoachState& s) { s.params.bands[2] = bell (3000, -4, 2); }, [] (CoachState& s, auto&) { s.params.bands[2].dynOn = true; } },
        { "rec_harsh_hits:5", [] (CoachState& s) { s.params.bands[2] = dynBell (3000); }, events ({ "learn finished" }) },
        { "rec_harsh_hits:6", [] (CoachState& s) { s.params.bands[2] = dynBell (3000); s.activity[2] = 2.0f; }, [] (CoachState& s, auto&) { s.activity[2] = 30.0f; } },

        { "rec_master_pass:1", nop, setTrack ("Master") },
        { "rec_master_pass:3", nop, [] (CoachState& s, auto&) { s.params.bands[0] = bell (25, 0); s.params.bands[0].shape = Shape::LowCut; } },
        { "rec_master_pass:6", [] (CoachState& s) { s.params.global.autoGain = false; }, [] (CoachState& s, auto&) { s.params.global.autoGain = true; } },
    };

    for (const auto& c : cases)
    {
        INFO (c.key);
        const auto id = c.key.upToFirstOccurrenceOf (":", false, false);
        const int n = c.key.fromFirstOccurrenceOf (":", false, false).getIntValue();
        const auto* r = content.recipe (id);
        REQUIRE (r != nullptr);
        REQUIRE (n >= 1);
        REQUIRE (n <= (int) r->steps.size());

        auto s = base();
        c.setup (s);
        // Earlier steps may pick the target band ("that band").
        for (int b = 0; b < lens::dsp::kNumBands; ++b)
            if (s.params.bands[(size_t) b].on)
            {
                // find first on band
                break;
            }

        lens::coach::RecipeRunner runner;
        s.time = 0.0;
        runner.start (id, s);
        for (int k = 1; k < n; ++k)
            runner.next (s);
        REQUIRE (runner.stepIndex() == n - 1);
        // Point "that band" at the first band that is on.
        for (int b = 0; b < lens::dsp::kNumBands; ++b)
            if (s.params.bands[(size_t) b].on)
            {
                runner.targetBand = b;
                break;
            }
        // Re-enter so the step snapshot includes the target band.
        if (n > 1)
        {
            runner.back (s);
            runner.next (s);
        }

        // Without the trigger: no advance, even after 6 s.
        for (double t = 0.25; t <= 6.0; t += 0.25)
        {
            s.time = t;
            runner.update (s);
        }
        REQUIRE (runner.stepIndex() == n - 1);
        REQUIRE (runner.isRunning());

        // With it: advances (stateful checks need a few seconds).
        c.pass (s, runner);
        for (double t = 6.25; t <= 12.5 && runner.isRunning() && runner.stepIndex() == n - 1; t += 0.25)
        {
            s.time = t;
            runner.update (s);
        }
        REQUIRE ((! runner.isRunning() || runner.stepIndex() == n));
        tested.insert (c.key);
    }

    // Coverage: every step with a check in LESSONS.md is tested.
    for (const auto& r : content.recipes)
        for (size_t i = 0; i < r.steps.size(); ++i)
            if (r.steps[i].check.isNotEmpty() && r.steps[i].check != "none")
            {
                const auto key = r.id + ":" + juce::String ((int) i + 1);
                INFO ("untested check " << key << " (" << r.steps[i].check << ")");
                REQUIRE (tested.count (key) == 1);
            }
}

TEST_CASE ("Recipes: Back and Skip always work; finishing calls back", "[coach][recipes]")
{
    lens::coach::RecipeRunner r;
    juce::String finished;
    r.onFinished = [&] (const juce::String& id) { finished = id; };
    auto s = neutral();
    r.start ("rec_master_pass", s);
    REQUIRE (r.isRunning());
    r.back (s);
    REQUIRE (r.stepIndex() == 0);
    for (int i = 0; i < 7; ++i)
        r.next (s);
    REQUIRE (r.isLastStep());
    r.next (s);
    REQUIRE_FALSE (r.isRunning());
    REQUIRE (finished == "rec_master_pass");
}

// ---------------------------------------------------------------- explain

TEST_CASE ("Explain line: every shape, static and dynamic, both modes, cut and boost, word for word", "[coach][explain]")
{
    using lens::coach::explain;
    auto shaped = [] (Shape sh, float f, float g = 0.0f, float q = 1.0f, int slope = 1)
    {
        auto b = bell (f, g, q);
        b.shape = sh;
        b.slope = slope;
        return b;
    };

    struct Ex
    {
        lens::dsp::BandParams b;
        const char* track;
        float activity;
        const char* expected;
    };
    auto dynShelf = [] (Shape sh, float f, float range)
    {
        auto b = dynBell (f, range);
        b.shape = sh;
        return b;
    };
    auto withStereo = [] (lens::dsp::BandParams b, lens::dsp::StereoMode m) { b.stereo = m; return b; };
    auto below = [] (lens::dsp::BandParams b) { b.dynMode = lens::dsp::DynMode::Below; return b; };
    auto custom = [] (lens::dsp::BandParams b) { b.detCustom = true; b.detHp = 5000.0f; b.detLp = 10000.0f; return b; };
    auto external = [] (lens::dsp::BandParams b) { b.detExternal = true; return b; };

    const std::vector<Ex> cases = {
        { bell (250, 3, 1), "Bus", 0, "Boosts +3.0 dB around 250 Hz (Mud). Broad: musical shaping." },
        { bell (2500, -4.5f, 3), "Vocal", 0, "Cuts \xe2\x88\x92" "4.5 dB around 2.5 kHz (Clarity). Focused." },
        { bell (100, 0), "Bass", 0, "Sits at 0 dB around 100 Hz (Weight). Drag it up or down to hear what lives here." },
        { bell (3200, -6, 8), "Bus", 0, "Cuts \xe2\x88\x92" "6.0 dB around 3.2 kHz (Presence). Very narrow: surgical, for one ringing frequency." },
        { bell (10000, 2, 0.5f), "Master", 0, "Boosts +2.0 dB around 10 kHz (Air). Very broad: a tone move." },
        { shaped (Shape::LowShelf, 100, 2), "Bus", 0, "Raises everything below about 100 Hz by +2.0 dB." },
        { shaped (Shape::LowShelf, 100, -3), "Bus", 0, "Lowers everything below about 100 Hz by \xe2\x88\x92" "3.0 dB." },
        { shaped (Shape::HighShelf, 12000, 1.5f), "Bus", 0, "Raises everything above about 12 kHz by +1.5 dB." },
        { shaped (Shape::HighShelf, 6000, -2), "Bus", 0, "Lowers everything above about 6 kHz by \xe2\x88\x92" "2.0 dB." },
        { shaped (Shape::LowCut, 35, 0, 1, 3), "Bass", 0, "Removes everything below 35 Hz, at 24 dB per octave." },
        { shaped (Shape::HighCut, 9000, 0, 1, 1), "Bass", 0, "Removes everything above 9 kHz, at 12 dB per octave." },
        { shaped (Shape::Notch, 60), "Bus", 0, "Removes a thin slice at 60 Hz. Use it for hum or one ringing note." },
        { shaped (Shape::BandPass, 1000), "Bus", 0, "Keeps only the region around 1 kHz. Mostly for effects or listening." },
        { shaped (Shape::Tilt, 1000, 1), "Master", 0, "Tilts the whole sound brighter around 1 kHz: highs up 0.5 dB, lows down 0.5 dB." },
        { shaped (Shape::Tilt, 1000, -3), "Master", 0, "Tilts the whole sound darker around 1 kHz: highs down 1.5 dB, lows up 1.5 dB." },
        { shaped (Shape::AllPass, 500), "Bus", 0, "Shifts phase around 500 Hz without changing the level. An advanced tool; you usually won't need it." },
        { withStereo (bell (250, 3), lens::dsp::StereoMode::Mid), "Bus", 0, "Boosts +3.0 dB around 250 Hz (Mud). Broad: musical shaping. Only on the Mid (center) channel." },
        { withStereo (shaped (Shape::LowCut, 100, 0, 1, 1), lens::dsp::StereoMode::Side), "Bus", 0, "Removes everything below 100 Hz, at 12 dB per octave. Only on the Side (edges) channel." },
        { withStereo (bell (250, 3), lens::dsp::StereoMode::Left), "Bus", 0, "Boosts +3.0 dB around 250 Hz (Mud). Broad: musical shaping. Only on the Left channel." },
        { withStereo (bell (250, 3), lens::dsp::StereoMode::Right), "Bus", 0, "Boosts +3.0 dB around 250 Hz (Mud). Broad: musical shaping. Only on the Right channel." },
        // Dynamic: four moves, three detectors, shelves, activity suffixes.
        { dynBell (180, -5, -24), "Bass", 23, "Cuts up to \xe2\x88\x92" "5.0 dB around 180 Hz (Boom) when this frequency area gets louder than \xe2\x88\x92" "24.0 dB. Moving 23% of the time." },
        { dynBell (180, 3, -24), "Bass", 23, "Boosts up to +3.0 dB around 180 Hz (Boom) when this frequency area gets louder than \xe2\x88\x92" "24.0 dB. Moving 23% of the time." },
        { custom (below (dynBell (7000, 4, -40))), "Vocal", 50,
          "Boosts up to +4.0 dB around 7 kHz (Sibilance & edge) when 5 kHz\xe2\x80\x93" "10 kHz on this track drops below \xe2\x88\x92" "40.0 dB. Moving 50% of the time." },
        { external (below (dynBell (60, -3, -30))), "Bass", 12, "Cuts up to \xe2\x88\x92" "3.0 dB around 60 Hz (Weight) when the side-chain track drops below \xe2\x88\x92" "30.0 dB. Moving 12% of the time." },
        { dynShelf (Shape::LowShelf, 60, -4), "Master", 0,
          "Cuts up to \xe2\x88\x92" "4.0 dB below 60 Hz (Low end) when this frequency area gets louder than \xe2\x88\x92" "24.0 dB. Moving 0% of the time. Not moving yet: the threshold may be too high." },
        { dynShelf (Shape::HighShelf, 8000, 2), "Master", 95,
          "Boosts up to +2.0 dB above 8 kHz (Presence) when this frequency area gets louder than \xe2\x88\x92" "24.0 dB. Moving 95% of the time. Almost always moving: it's acting like a normal EQ." },
    };

    for (const auto& c : cases)
    {
        const auto got = explain (c.b, c.track, c.activity);
        INFO (got);
        REQUIRE (got == U (c.expected));
    }

    // Width phrases at their boundaries.
    REQUIRE (lens::coach::widthPhrase (0.69) == "Very broad: a tone move.");
    REQUIRE (lens::coach::widthPhrase (0.7) == "Broad: musical shaping.");
    REQUIRE (lens::coach::widthPhrase (2.0) == "Focused.");
    REQUIRE (lens::coach::widthPhrase (6.0) == "Very narrow: surgical, for one ringing frequency.");
}

// ---------------------------------------------------------------- content

TEST_CASE ("Content: every ID in LESSONS.md is in the JSON and vice versa; no TODO", "[coach][content]")
{
    const auto md = lessonsMd();
    REQUIRE (md.isNotEmpty());

    std::set<juce::String> mdIds;
    juce::StringArray lines;
    lines.addLines (md);
    const std::regex idRe ("\\b((tip|rec|les|step)_[a-z_]+)\\b");
    bool inExplain = false;
    for (const auto& line : lines)
    {
        if (line.startsWith ("## "))
            inExplain = line.startsWith ("## 1.");
        const auto str = line.toStdString();
        for (std::sregex_iterator it (str.begin(), str.end(), idRe), end; it != end; ++it)
            mdIds.insert (juce::String ((*it)[1].str()));
        if (inExplain)
        {
            std::smatch m;
            static const std::regex tplRe ("^- ([a-z]+_[a-z_]+|[a-z]+): \"");
            if (std::regex_search (str, m, tplRe))
                mdIds.insert (juce::String (m[1].str()));
        }
    }

    const auto& content = lens::coach::Content::get();
    std::set<juce::String> jsonIds;
    for (const auto& id : content.allIds())
        jsonIds.insert (id);

    for (const auto& id : mdIds)
    {
        INFO ("missing from JSON: " << id);
        REQUIRE (jsonIds.count (id) == 1);
    }
    // Reverse: every ID-shaped key in the JSON comes from LESSONS.md.
    for (const auto& id : jsonIds)
        if (id.startsWith ("tip_") || id.startsWith ("rec_") || id.startsWith ("les_") || id.startsWith ("step_") || ! id.containsChar ('_')
            || md.contains ("- " + id + ": \""))
        {
            INFO ("not in LESSONS.md: " << id);
            REQUIRE (mdIds.count (id) == 1);
        }

    REQUIRE (content.tips.size() == 21);
    REQUIRE (content.recipes.size() == 7);
    REQUIRE (content.lessons.size() == 22);
    REQUIRE (content.controls.size() == 33);

    // No TODO in the embedded JSON.
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String name (BinaryData::namedResourceList[i]);
        if (! name.endsWith ("_json"))
            continue;
        int size = 0;
        const auto* data = BinaryData::getNamedResource (name.toRawUTF8(), size);
        INFO (name);
        REQUIRE_FALSE (juce::String::fromUTF8 (data, size).contains ("TODO"));
    }

    // Tooltips exist for the controls the GUI uses.
    for (auto* c : { "Frequency", "Gain", "Q", "Learn", "Bypass", "Track type", "Peak marker", "Coach" })
        REQUIRE (content.tooltip (c).isNotEmpty());
}

// ---------------------------------------------------------------- safety

TEST_CASE ("The Coach never changes the sound by itself; Coach actions are undoable", "[coach][safety]")
{
    lens::LensProcessor proc;
    setEightMixedBands (proc);
    setDyn (proc, 3, {});
    const auto bandsBefore = juce::JSON::toString (proc.getBands());
    const auto globalsBefore = proc.getStateJson();

    auto& tips = proc.tipEngine();
    auto& recipes = proc.recipeRunner();
    recipes.start ("rec_boomy_notes", lens::coach::sampleProcessor (proc, 0.0));
    for (int i = 0; i < 200; ++i)
    {
        auto s = lens::coach::sampleProcessor (proc, i * 0.25);
        tips.update (s);
        recipes.update (s);
        recipes.event ("bypass toggled");
        recipes.event ("learn finished");
    }
    REQUIRE (juce::JSON::toString (proc.getBands()) == bandsBefore);
    REQUIRE (proc.getStateJson() == globalsBefore);

    // Try it (lessons) and peak-marker actions add bands, undoably, and
    // never edit existing ones.
    lens::dsp::BandParams bp;
    bp.shape = Shape::Bell;
    bp.freq = 1000.0f;
    bp.gain = 10.0f;
    bp.q = 8.0f;
    const int b = proc.addBand (bp, "Try it");
    REQUIRE (b == 8);
    const auto after = proc.getBands();
    for (int i = 0; i < 8; ++i)
        REQUIRE (juce::JSON::toString (after[i]) == juce::JSON::toString (juce::JSON::parse (bandsBefore)[i]));
    REQUIRE (proc.undo());
    // Restored to float precision (JUCE stores the real value, so a
    // normalised write can move it by one float step).
    const auto restored = proc.getBands();
    const auto original = juce::JSON::parse (bandsBefore);
    for (int band = 0; band < 24; ++band)
        for (auto* key : { "freq", "gain", "q" })
        {
            const double x = original[band][key], y = restored[band][key];
            REQUIRE (std::abs (x - y) <= 1e-5 * std::max (1.0, std::abs (x)));
        }
    for (int band = 0; band < 24; ++band)
    {
        REQUIRE ((bool) original[band]["on"] == (bool) restored[band]["on"]);
        REQUIRE (original[band]["shape"].toString() == restored[band]["shape"].toString());
    }
}

TEST_CASE ("Settings round-trip and live outside presets and state", "[coach][settings]")
{
    const auto f = juce::File::createTempFile (".json");
    {
        lens::coach::Settings s (f);
        s.firstRunDone = true;
        s.quiet = true;
        s.tipsNever = { "tip_clip" };
        s.recipesDone = { "rec_first_eq" };
        s.favorites = { "factory:Init" };
        s.earLevel = 3;
        s.earBestStreak = 7;
        REQUIRE (s.save());
    }
    lens::coach::Settings t (f);
    REQUIRE (t.firstRunDone);
    REQUIRE (t.quiet);
    REQUIRE (t.tipsNever == juce::StringArray { "tip_clip" });
    REQUIRE (t.recipesDone == juce::StringArray { "rec_first_eq" });
    REQUIRE (t.favorites == juce::StringArray { "factory:Init" });
    REQUIRE (t.earLevel == 3);
    REQUIRE (t.earBestStreak == 7);
    f.deleteFile();

    lens::LensProcessor proc;
    proc.coachSettings().recipesDone.add ("rec_deess");
    REQUIRE_FALSE (proc.getStateJson().contains ("rec_deess"));
    REQUIRE_FALSE (proc.getPresetJson().contains ("rec_deess"));
}

// ---------------------------------------------------------------- presets

TEST_CASE ("Factory bank: 17 presets that load, save and reload identically", "[presets]")
{
    lens::coach::Settings settings (juce::File::createTempFile (".json"));
    lens::presets::PresetManager pm (settings, juce::File::createTempFile ("dir"));
    int factory = 0;
    for (const auto& e : pm.all())
    {
        if (! e.factory)
            continue;
        ++factory;
        INFO (e.name);
        lens::LensProcessor a;
        REQUIRE (a.setPresetJson (pm.jsonFor (e)));
        REQUIRE (a.getSession().presetName == e.name);
        const auto saved = a.getPresetJson();
        lens::LensProcessor b;
        REQUIRE (b.setPresetJson (saved));
        REQUIRE (b.getPresetJson() == saved);
        REQUIRE (juce::JSON::toString (a.getBands()) == juce::JSON::toString (b.getBands()));
        REQUIRE (lens::presets::PresetManager::categories().contains (e.category));
        // Dynamic thresholds ship at -24 dB, and the note says to press Learn.
        bool anyDyn = false;
        for (int band = 0; band < 24; ++band)
            if (a.bandValue (band, lens::params::band::on) > 0.5f && a.bandValue (band, lens::params::band::dynOn) > 0.5f)
            {
                anyDyn = true;
                REQUIRE (a.bandValue (band, lens::params::band::dynThresh) == -24.0f);
            }
        REQUIRE (anyDyn == e.note.contains ("Learn"));
    }
    REQUIRE (factory == 17);

    // Spot-check one preset against the SPEC table.
    lens::LensProcessor p;
    p.setPresetJson (pm.jsonFor (pm.all()[(size_t) pm.indexOfName ("Kick and Bass Split")]));
    REQUIRE (getParam (p, "b01_on") == 1.0f);
    REQUIRE (std::abs (getParam (p, "b01_freq") - 60.0f) < 1e-3f);
    REQUIRE (getParam (p, "b01_det_source") == 1.0f);
    REQUIRE (getParam (p, "b01_det_custom") == 1.0f);
    REQUIRE (std::abs (getParam (p, "b01_det_hp") - 40.0f) < 1e-3f);
    REQUIRE (std::abs (getParam (p, "b01_dyn_attack") - 1.0f) < 1e-4f);
    REQUIRE (getParam (p, "b02_on") == 0.0f);
}

TEST_CASE ("Preset browser model: search, category, favorites, save-as", "[presets]")
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lens-presets-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    lens::coach::Settings settings (dir.getChildFile ("settings.json"));
    lens::presets::PresetManager pm (settings, dir.getChildFile ("Presets"));

    REQUIRE (pm.filtered ("", "Master", false).size() == 3);
    REQUIRE (pm.filtered ("sibil", "", false).size() == 1);
    REQUIRE (pm.filtered ("", "", true).empty());
    pm.toggleFavorite (pm.all()[3]);
    REQUIRE (pm.filtered ("", "", true).size() == 1);
    REQUIRE (lens::coach::Settings (dir.getChildFile ("settings.json")).favorites.size() == 1);

    lens::LensProcessor proc;
    setBand (proc, 1, { 0, 440.0f, 3.0f, 2.0f });
    const int idx = pm.saveAs ("My Snare Fix", proc.getPresetJson());
    REQUIRE (idx >= 0);
    REQUIRE_FALSE (pm.all()[(size_t) idx].factory);
    REQUIRE (pm.all()[(size_t) idx].name == "My Snare Fix");
    lens::LensProcessor back;
    back.setPresetJson (pm.jsonFor (pm.all()[(size_t) idx]));
    REQUIRE (std::abs (getParam (back, "b01_freq") - 440.0f) < 1e-3f);
    REQUIRE_FALSE (pm.save (0, proc.getPresetJson())); // factory presets are read-only
    dir.deleteRecursively();
}

TEST_CASE ("Stress: 1000 rapid preset switches during playback, no crash, no click", "[presets][stress]")
{
    const double fs = 48000.0;
    lens::coach::Settings settings (juce::File::createTempFile (".json"));
    lens::presets::PresetManager pm (settings, juce::File::createTempFile ("dir"));
    std::vector<juce::String> jsons;
    for (const auto& e : pm.all())
        jsons.push_back (pm.jsonFor (e));

    const int blocks = 2000, bs = 128;
    const auto input = makeSine (2, blocks * bs, fs, 1000.0, 0.3f);

    // Reference: the largest steady-state step any preset produces.
    double steady = 0.0;
    for (const auto& j : jsons)
    {
        lens::LensProcessor p;
        p.setPresetJson (j);
        RenderOptions o;
        o.blockPattern = { bs };
        const auto out = render (p, makeSine (2, (int) fs / 2, fs, 1000.0, 0.3f), o);
        steady = std::max (steady, maxStep (out, 0, (int) fs / 4, (int) fs / 2));
    }

    lens::LensProcessor proc;
    int switches = 0;
    RenderOptions o;
    o.blockPattern = { bs };
    o.beforeBlock = [&] (juce::int64 pos)
    {
        if ((pos / bs) % 2 == 0)
        {
            proc.loadPreset (jsons[(size_t) switches % jsons.size()]);
            ++switches;
        }
        proc.serviceMessageThread();
    };
    const auto out = render (proc, input, o);
    REQUIRE (switches == 1000);
    REQUIRE (allFinite (out));
    const double worst = maxStep (out, 0, 1, out.getNumSamples());
    INFO ("worst step " << worst << ", steady max " << steady);
    REQUIRE (worst <= 1.5 * steady);
}

// ---------------------------------------------------------------- Phase 7: ear trainer

#include "Coach/EarTrainer.h"
#include "GUI/Editor.h"

TEST_CASE ("Ear trainer: levels from LESSONS.md, scoring, level up", "[ear]")
{
    const auto f = juce::File::createTempFile (".json");
    lens::coach::Settings s (f);
    lens::coach::EarTrainer game (s, lens::coach::Content::get(), 7);
    REQUIRE (lens::coach::EarTrainer::levels().size() == 5);
    REQUIRE (lens::coach::EarTrainer::levels()[1].choices.size() == 8);
    REQUIRE (lens::coach::EarTrainer::levels()[2].choices.size() == 10);
    REQUIRE (lens::coach::EarTrainer::levels()[3].gainDb < 0.0f);
    game.start();
    REQUIRE (game.instructions() == "Toggle the hidden band on and off, then click where you think it is.");

    // Ten right answers at level 1 → level 2.
    lens::coach::EarTrainer::Result r;
    for (int i = 0; i < 10; ++i)
        r = game.guess (game.targetFreq() * 1.05);
    REQUIRE (r.levelUp);
    REQUIRE (r.message.endsWith ("Level 2 unlocked."));
    REQUIRE (s.earLevel == 2);
    REQUIRE (s.earBestStreak == 10);

    // A wrong answer names both frequencies.
    const double t = game.targetFreq();
    const auto w = game.guess (t > 1000.0 ? 63.0 : 8000.0);
    REQUIRE_FALSE (w.right);
    REQUIRE (w.message.startsWith ("It was "));
    REQUIRE (lens::coach::Settings (f).earLevel == 2); // saved
    f.deleteFile();
}

TEST_CASE ("Ear trainer: never active after load, off when the editor closes, never in state", "[ear]")
{
    lens::LensProcessor p;
    REQUIRE_FALSE (p.earTrainer.on.load());
    const auto before = p.getStateJson();
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        p.earTrainer.on = true;
        p.earTrainer.freq = 2000.0f;
        p.earTrainer.gainDb = 12.0f;
        REQUIRE (p.getStateJson() == before); // never written to state
    }
    REQUIRE_FALSE (p.earTrainer.on.load()); // off after the editor closed

    lens::LensProcessor q;
    q.setStateJson (before);
    REQUIRE_FALSE (q.earTrainer.on.load());
}
