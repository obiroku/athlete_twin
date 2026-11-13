// Phase 8: factory bank, randomize, arp, stress.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "Engine/Arp.h"
#include <cstdio>
#include <set>

using namespace prism;
using namespace prism::test;

static std::vector<PresetData> factoryBank()
{
    std::vector<PresetData> out;
    for (auto& [res, text] : PresetLibrary::factoryPresetSources())
    {
        PresetData d; juce::String err;
        REQUIRE (presetFromString (text, d, err));
        out.push_back (d);
    }
    return out;
}

TEST_CASE ("Factory bank: all SPEC presets present, categories valid", "[phase8]")
{
    const auto bank = factoryBank();
    REQUIRE (bank.size() == 26);
    std::set<std::string> names;
    for (auto& d : bank)
    {
        names.insert (d.name);
        REQUIRE (PresetLibrary::categories().contains (juce::String (d.category)));
    }
    for (auto n : { "Init", "Neo-Soul Keys", "Gospel Pad", "Folk-Gaze Wash", "Upright Sub", "Tape Lead", "Choir Stack", "Donut Shop Bass",
                    "Shoegaze Shimmer", "Pew Organ", "Nebraska Winter", "Voicemail Keys", "Trail Pluck", "Haze Pad", "Basement Sermon Bass",
                    "Testimony Chop Keys", "Sunday Morning", "Glide Sub", "Ladder Funk Bass", "Reese Haze", "Breathing Choir", "Brass Swell",
                    "Harp Arp", "Dream Arp", "Kalimba Drift", "Frozen Drone" })
        REQUIRE (names.count (n) == 1);
}

TEST_CASE ("Every factory preset loads, saves and reloads identically", "[phase8]")
{
    const auto& defs = paramDefs();
    for (auto& d : factoryBank())
    {
        INFO (d.name);
        auto a = makeProcessor();
        loadPresetNow (*a, d);
        juce::MemoryBlock mb;
        a->getStateInformation (mb);
        auto b = makeProcessor();
        b->setStateInformation (mb.getData(), (int) mb.getSize());
        const auto pa = a->currentPreset(), pb = b->currentPreset();
        for (int i = 0; i < P::NumParams; ++i)
        {
            INFO (defs[(size_t) i].id);
            REQUIRE_THAT (pb.params[(size_t) i], Catch::Matchers::WithinAbs (pa.params[(size_t) i], 1e-4 * std::max (1.0f, std::abs (pa.params[(size_t) i]))));
            // and the loaded values are the preset's values
            REQUIRE_THAT (pa.params[(size_t) i], Catch::Matchers::WithinAbs (d.params[(size_t) i], 1e-3 * std::max (1.0f, std::abs (d.params[(size_t) i]))));
        }
        for (int s = 0; s < P::NumModSlots; ++s)
        {
            REQUIRE (pa.extras.routings[(size_t) s].src == pb.extras.routings[(size_t) s].src);
            REQUIRE (pa.extras.routings[(size_t) s].dest == pb.extras.routings[(size_t) s].dest);
            REQUIRE (pa.extras.routings[(size_t) s].dest == d.extras.routings[(size_t) s].dest);
        }
        REQUIRE (pa.extras.fxOrder == pb.extras.fxOrder);
        REQUIRE (pa.tables[0].factory == pb.tables[0].factory);
        REQUIRE (pa.tables[1].factory == pb.tables[1].factory);
        REQUIRE (pa.macroNames == pb.macroNames);
        REQUIRE (pa.extras.chord.count == pb.extras.chord.count);
    }
}

TEST_CASE ("Loudness: factory presets within +-3 dB on the C3-E3-G3 chord, peaks below -1 dBFS", "[phase8][loudness]")
{
    double lo = 1e9, hi = -1e9;
    std::string loName, hiName;
    for (auto& d : factoryBank())
    {
        const auto l = measurePreset (d);
        INFO (d.name << " rms " << l.rmsDb << " peak " << l.peak);
        REQUIRE (l.peak < 0.8913); // -1 dBFS
        if (l.rmsDb < lo) { lo = l.rmsDb; loName = d.name; }
        if (l.rmsDb > hi) { hi = l.rmsDb; hiName = d.name; }
    }
    std::printf ("  loudness: %.2f dB (%s) .. %.2f dB (%s), spread %.2f dB\n", lo, loName.c_str(), hi, hiName.c_str(), hi - lo);
    const double mid = 0.5 * (lo + hi);
    REQUIRE (hi - mid <= 3.0);
    REQUIRE (mid - lo <= 3.0);
}

TEST_CASE ("Stress: 1,000 rapid preset switches while notes play", "[phase8][stress][slow]")
{
    const auto bank = factoryBank();
    // reference: the largest steady-state sample-to-sample delta of any preset on the chord
    double steady = 0;
    for (auto& d : bank)
    {
        auto p = makeProcessor();
        loadPresetNow (*p, d);
        auto b = render (*p, { { 0.0, 2.0, 60, 0.8f }, { 0.0, 2.0, 64, 0.8f }, { 0.0, 2.0, 67, 0.8f } }, 1.5);
        steady = std::max (steady, std::max (maxDelta (b, 0, 24000), maxDelta (b, 1, 24000)));
    }
    auto p = makeProcessor (48000.0, 128);
    RenderOptions o;
    o.blocks = { 128 };
    o.nonRealtime = false;
    int switches = 0;
    o.beforeBlock = [&] (PrismProcessor& pp, int64_t pos) {
        if (pos > 0 && (pos / 128) % 5 == 0 && switches < 1000)
        {
            pp.loadPreset (bank[(size_t) (switches * 7) % bank.size()], true);
            ++switches;
        }
    };
    std::vector<Note> notes;
    for (double t = 0.0; t < 14.0; t += 0.5) for (int n : { 48, 60, 64, 67 }) notes.push_back ({ t, 0.45, n + (int) (t * 2) % 5, 0.8f });
    auto b = render (*p, notes, 14.0, o);
    bool finite = true;
    for (int c = 0; c < 2; ++c) for (int s = 0; s < b.getNumSamples(); ++s) finite &= std::isfinite (b.getSample (c, s));
    const double worst = std::max (maxDelta (b, 0), maxDelta (b, 1));
    std::printf ("  stress: %d switches, worst delta %.4f vs steady-state %.4f (ratio %.2f)\n", switches, worst, steady, worst / steady);
    REQUIRE (switches == 1000);
    REQUIRE (finite);
    REQUIRE (peak (b) < 0.8913);
    REQUIRE (worst <= 1.5 * steady);
}

TEST_CASE ("Randomize x500: never silent, never above 0 dBFS", "[phase8][random][slow]")
{
    auto p = makeProcessor();
    RandomLocks locks;
    locks.arp = true;
    double quietest = 1e9, loudest = 0;
    for (int i = 0; i < 500; ++i)
    {
        const auto d = randomizePreset (PresetData::init(), locks, 1000 + (uint64_t) i);
        loadPresetNow (*p, d);
        p->engine.panic();
        // held long enough for the longest allowed amp attack (2 s)
        auto b = render (*p, { { 0.0, 3.0, 60, 0.8f }, { 0.0, 3.0, 64, 0.8f }, { 0.0, 3.0, 67, 0.8f } }, 3.0);
        const double pk = peak (b);
        const double r = std::max (rms (b, 0), rms (b, 1));
        INFO ("random patch " << i << " peak " << pk << " rms " << r);
        REQUIRE (pk <= 1.0);
        REQUIRE (r > 1e-3); // -60 dBFS
        quietest = std::min (quietest, r);
        loudest = std::max (loudest, pk);
    }
    std::printf ("  randomize x500: quietest rms %.1f dBFS, loudest peak %.2f dBFS\n", 20 * std::log10 (quietest), 20 * std::log10 (loudest));
}

TEST_CASE ("Arp stays in sync with the host transport over 64 bars", "[phase8][arp]")
{
    Arp arp;
    const double sr = 48000.0, bpm = 117.0;
    arp.prepare (sr);
    ArpSettings s;
    s.on = true; s.mode = (int) ArpMode::Up; s.octaves = 2; s.div = syncDivIndex ("1/16"); s.gate = 0.5f;
    PatchExtras x;
    const double spb = sr * 60.0 / bpm;
    const double ppq0 = 0.0;
    const int64_t total = (int64_t) std::llround (64 * 4 * spb);
    ChunkEvents in, out;
    int64_t pos = 0;
    int chunkIdx = 0;
    std::vector<int64_t> onsets;
    const int sizes[] = { 512, 137, 1, 333, 64, 480 };
    while (pos < total)
    {
        const int n = (int) std::min<int64_t> (sizes[chunkIdx++ % 6] % 512 + 1, total - pos);
        in.clear();
        if (pos == 0)
            for (int nn : { 60, 64, 67 }) { Event e; e.pos = 0; e.type = Event::NoteOn; e.num = (int16_t) nn; e.value = 0.8f; in.add (e); }
        const double ppq = ppq0 + (double) pos / spb;
        arp.process (in, out, n, s, x, true, true, ppq, bpm);
        for (int i = 0; i < out.count; ++i) if (out[i].type == Event::NoteOn) onsets.push_back (pos + out[i].pos);
        pos += n;
    }
    // one onset per 1/16 step, each exactly on the grid (ceil to the sample)
    REQUIRE ((int) onsets.size() == 64 * 16);
    int64_t worst = 0;
    for (size_t k = 0; k < onsets.size(); ++k)
    {
        const int64_t expect = (int64_t) std::ceil ((double) k * 0.25 * spb - 1e-9);
        worst = std::max (worst, std::abs (onsets[k] - expect));
    }
    std::printf ("  arp sync over 64 bars: %zu steps, worst offset %lld samples\n", onsets.size(), (long long) worst);
    REQUIRE (worst <= 1);
}

TEST_CASE ("Determinism: a 16-bar arp render repeats bit-identically after a transport restart", "[phase8][arp]")
{
    auto p = makeProcessor (48000.0, 256);
    setParam (*p, "arp_on", 1.0f);
    setParam (*p, "arp_mode", (float) ArpMode::Random);
    setParam (*p, "arp_octaves", 3);
    setParam (*p, "drift", 1.0f);
    setParam (*p, "oscA_unison", 4);
    setParam (*p, "env1_release", 0.1f);
    HostTransport tr;
    tr.bpm = 120.0;
    const double bars16 = 16 * 4 * 0.5; // seconds at 120 bpm
    auto pass = [&] {
        tr.playing = true;
        RenderOptions o; o.transport = &tr; o.blocks = { 256 };
        std::vector<Note> notes { { 0.0, bars16 - 0.01, 60, 0.8f }, { 0.0, bars16 - 0.01, 63, 0.8f }, { 0.0, bars16 - 0.01, 67, 0.8f } };
        auto b = render (*p, notes, bars16, o);
        tr.playing = false;
        render (*p, {}, 1.0, o); // stopped: tails die away
        return b;
    };
    const auto a = pass();
    const auto b = pass();
    REQUIRE (peak (a) > 0.01);
    REQUIRE (maxAbsDiff (a, b) == 0.0);
}

TEST_CASE ("Chord mode plays the learned chord, strummed", "[phase8]")
{
    auto p = makeProcessor();
    auto x = p->getExtras();
    x.chord.count = 3; x.chord.intervals = { 0, 4, 7 };
    p->setExtras (x);
    setParam (*p, "chord_on", 1.0f);
    setParam (*p, "chord_strum", 20.0f);
    render (*p, { { 0.0, 0.5, 60, 0.8f } }, 0.3);
    REQUIRE (p->engine.voiceCount.load() == 3);
}
