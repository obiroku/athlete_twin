// Phase 7: reference and EQ Match.
#include <catch2/catch_test_macros.hpp>

#include "DSP/EqBand.h"
#include "Match/EqMatch.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

using namespace lens::test;

namespace
{
juce::AudioBuffer<float> eqd (const juce::AudioBuffer<float>& in, std::vector<lens::dsp::BandParams> bands)
{
    juce::AudioBuffer<float> out (in);
    std::vector<double> l ((size_t) in.getNumSamples()), r (l.size());
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        l[(size_t) i] = in.getSample (0, i);
        r[(size_t) i] = in.getSample (1, i);
    }
    for (auto& b : bands)
    {
        lens::dsp::EqBand band;
        band.prepare (48000.0, 2);
        b.on = true;
        band.setParams (b, true);
        band.process (l.data(), r.data(), (int) l.size(), nullptr);
    }
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        out.setSample (0, i, (float) l[(size_t) i]);
        out.setSample (1, i, (float) r[(size_t) i]);
    }
    return out;
}

lens::match::Spectrum ltasOf (const juce::AudioBuffer<float>& b, const char* name)
{
    lens::match::LtasAccumulator acc;
    acc.prepare (48000.0);
    std::vector<float> mono ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i)
        mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
    acc.add (mono.data(), (int) mono.size());
    return acc.result (name);
}

lens::dsp::BandParams bell (float f, float g, float q)
{
    lens::dsp::BandParams b;
    b.shape = lens::dsp::Shape::Bell;
    b.freq = f;
    b.gain = g;
    b.q = q;
    return b;
}
} // namespace

TEST_CASE ("EQ Match: matching a file to itself suggests nothing", "[match]")
{
    const auto pink = makePink (2, 48000 * 6, 31, -16.0);
    const auto file = juce::File::createTempFile (".wav");
    REQUIRE (writeWav (file, pink, 48000.0, 24));
    const auto a = lens::match::analyseFile (file);
    const auto b = lens::match::analyseFile (file);
    file.deleteFile();
    REQUIRE (a.isValid());
    const auto d = lens::match::difference (a, b);
    REQUIRE_FALSE (d.db.empty());
    REQUIRE (d.freqs.front() >= 30.0);
    REQUIRE (d.freqs.back() <= 16000.0);
    REQUIRE (lens::match::suggest (d).empty());
}

TEST_CASE ("EQ Match: suggestions close a known gap and respect the limits", "[match]")
{
    const auto pink = makePink (2, 48000 * 8, 32, -16.0);
    const auto mine = ltasOf (pink, "mine");
    const auto ref = ltasOf (eqd (pink, { bell (1000, 4, 1), bell (150, -3, 1.5f) }), "ref");
    const auto s = lens::match::suggest (lens::match::difference (ref, mine));
    REQUIRE_FALSE (s.empty());
    REQUIRE (s.size() <= 6);
    bool near1k = false;
    for (const auto& x : s)
    {
        REQUIRE (x.q <= 2.0f);
        REQUIRE (std::abs (x.gainDb) <= 6.0f);
        if (std::abs (std::log2 (x.freq / 1000.0)) < 0.5 && x.gainDb > 2.0f)
            near1k = true;
    }
    REQUIRE (near1k);

    // A huge, jagged gap still gives at most 6 bands within the limits.
    const auto wild = ltasOf (eqd (pink, { bell (60, 15, 4), bell (300, -14, 6), bell (2000, 18, 3), bell (6000, -16, 5), bell (12000, 12, 2),
                                           bell (800, 10, 8), bell (100, -12, 8) }),
                              "wild");
    const auto s2 = lens::match::suggest (lens::match::difference (wild, mine));
    REQUIRE (s2.size() <= 6);
    for (const auto& x : s2)
    {
        REQUIRE (x.q <= 2.0f);
        REQUIRE (std::abs (x.gainDb) <= 6.0f);
    }
}

TEST_CASE ("EQ Match: suggesting never changes the EQ; references are remembered", "[match]")
{
    lens::LensProcessor p;
    setBand (p, 1, { 0, 500.0f, 2.0f, 1.0f });
    const auto before = juce::JSON::toString (p.getBands());
    const auto pink = makePink (2, 48000 * 4, 33, -16.0);
    const auto s = lens::match::suggest (lens::match::difference (ltasOf (eqd (pink, { bell (3000, 5, 1) }), "ref"), ltasOf (pink, "mine")));
    REQUIRE_FALSE (s.empty());
    REQUIRE (juce::JSON::toString (p.getBands()) == before);

    auto ref = ltasOf (pink, "My Favourite Song");
    REQUIRE (lens::match::saveReference (ref));
    const auto refs = lens::match::loadReferences();
    REQUIRE (refs.size() == 1);
    REQUIRE (refs[0].name == "My Favourite Song");
    REQUIRE (refs[0].db.size() == ref.db.size());
}
