// Phase 2: the Zero Latency engine through the plugin.
#include <catch2/catch_test_macros.hpp>

#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

using namespace lens::test;
namespace pb = lens::params::band;

namespace
{
RenderOptions opts (double fs = 48000.0, std::vector<int> pattern = { 512 })
{
    RenderOptions o;
    o.sampleRate = fs;
    o.blockPattern = std::move (pattern);
    return o;
}
} // namespace

TEST_CASE ("Null: all 24 bands at 0 dB differ from the input by less than -120 dB", "[zl][null]")
{
    lens::LensProcessor proc;
    const int gainShapes[] = { 0, 1, 2, 7 }; // Bell, shelves, Tilt
    for (int b = 1; b <= 24; ++b)
        setBand (proc, b, { gainShapes[b % 4], 30.0f * (float) b * b, 0.0f, 0.3f + 0.4f * (float) (b % 5), 1, b % 5 });

    const auto input = makeNoise (2, 48000, 11);
    const auto out = render (proc, input, opts());
    REQUIRE (peakDifferenceDb (out, input) < -120.0);
}

TEST_CASE ("Stereo modes: Side on mono input, Left leaves Right untouched, Mid/Side transparency", "[zl][stereo]")
{
    const auto monoSum = makePink (2, 48000, 5, -18.0, true);
    const auto stereoIn = makeNoise (2, 48000, 6);

    SECTION ("A Side band on mono-summed input leaves the output unchanged")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 12.0f, 1.0f, 1, 2 });
        setBand (proc, 2, { 2, 5000.0f, -9.0f, 0.7f, 1, 2 });
        setBand (proc, 3, { 3, 200.0f, 0.0f, 1.0f, 3, 2 });
        setParam (proc, "auto_gain", 0.0f);
        REQUIRE (peakDifferenceDb (render (proc, monoSum, opts()), monoSum) < -120.0);
    }

    SECTION ("A Left band leaves the right channel bit-identical")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 12.0f, 1.0f, 1, 3 });
        setBand (proc, 2, { 3, 300.0f, 0.0f, 1.0f, 7, 3 });
        setParam (proc, "auto_gain", 0.0f);
        const auto out = render (proc, stereoIn, opts());
        REQUIRE (std::memcmp (out.getReadPointer (1), stereoIn.getReadPointer (1), sizeof (float) * 48000) == 0);
        REQUIRE (peakDifferenceDb (out, stereoIn) > -40.0); // the left channel did change
    }

    SECTION ("Mid/Side encode-decode with 0 dB bands is transparent")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 0.0f, 1.0f, 1, 1 });
        setBand (proc, 2, { 1, 200.0f, 0.0f, 0.7f, 1, 2 });
        setBand (proc, 3, { 2, 6000.0f, 0.0f, 0.7f, 1, 1 });
        REQUIRE (peakDifferenceDb (render (proc, stereoIn, opts()), stereoIn) < -120.0);
    }

    SECTION ("On a mono track a Side band does nothing")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 12.0f, 1.0f, 1, 2 });
        const auto mono = makeNoise (1, 48000, 8);
        REQUIRE (bitIdentical (render (proc, mono, opts()), mono));
    }
}

TEST_CASE ("Click/zipper: sweeps, adding/removing bands and shape changes", "[zl][click]")
{
    const double fs = 48000.0;
    const int n = (int) (fs * 4.0);
    const auto input = makeSine (2, n, fs, 1000.0, 0.5f);

    auto checkRegions = [&] (const juce::AudioBuffer<float>& out, const std::vector<int>& changeTimes, int settle)
    {
        for (int t : changeTimes)
        {
            const double before = maxStep (out, 0, t - (int) (0.05 * fs), t);
            const double after = maxStep (out, 0, t + settle, t + settle + (int) (0.05 * fs));
            const double during = maxStep (out, 0, t, t + settle);
            INFO ("change at " << t << ": before " << before << " during " << during << " after " << after);
            REQUIRE (during <= 1.5 * std::max (before, after));
        }
    };

    SECTION ("A band sweeping 100 Hz -> 10 kHz in 1 s")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 100.0f, 3.0f, 1.0f });
        auto o = opts (fs, { 64 });
        o.beforeBlock = [&] (juce::int64 pos)
        {
            const double t = (double) pos / fs - 1.0;
            const double f = t <= 0.0 ? 100.0 : (t >= 1.0 ? 10000.0 : 100.0 * std::pow (100.0, t));
            setBandParam (proc, 1, pb::freq, (float) f);
        };
        const auto out = render (proc, input, o);
        const double before = maxStep (out, 0, (int) (0.5 * fs), (int) fs);
        const double during = maxStep (out, 0, (int) fs, (int) (2.1 * fs));
        const double after = maxStep (out, 0, (int) (2.2 * fs), (int) (3.0 * fs));
        INFO ("before " << before << " during " << during << " after " << after);
        REQUIRE (during <= 1.5 * std::max (before, after));
    }

    SECTION ("Bands added and removed")
    {
        lens::LensProcessor proc;
        setBand (proc, 2, { 2, 8000.0f, 3.0f, 0.7f });
        setBand (proc, 3, { 3, 50.0f, 0.0f, 1.0f, 3 });
        setBand (proc, 4, { 0, 1000.0f, -6.0f, 2.0f });
        for (int b : { 2, 3, 4 })
            setBandParam (proc, b, pb::on, 0.0f);

        std::vector<int> changes;
        for (int k = 0; k < 18; ++k)
            changes.push_back ((int) (0.2 * fs * (k + 1)));
        auto o = opts (fs, { 64 });
        o.beforeBlock = [&] (juce::int64 pos)
        {
            for (size_t k = 0; k < changes.size(); ++k)
                if (pos <= changes[k] && changes[k] < pos + 64)
                {
                    const int b = 2 + (int) (k % 3);
                    setBandParam (proc, b, pb::on, getParam (proc, lens::params::bandParamId (b, pb::on)) > 0.5f ? 0.0f : 1.0f);
                }
        };
        checkRegions (render (proc, input, o), changes, (int) (0.03 * fs));
    }

    SECTION ("Shape changes")
    {
        lens::LensProcessor proc;
        setBand (proc, 5, { 0, 2000.0f, 3.0f, 1.0f, 1 });
        std::vector<int> changes;
        for (int k = 0; k < 18; ++k)
            changes.push_back ((int) (0.2 * fs * (k + 1)));
        auto o = opts (fs, { 64 });
        o.beforeBlock = [&] (juce::int64 pos)
        {
            for (size_t k = 0; k < changes.size(); ++k)
                if (pos <= changes[k] && changes[k] < pos + 64)
                    setBandParam (proc, 5, pb::shape, (float) ((k + 1) % 9));
        };
        checkRegions (render (proc, input, o), changes, (int) (0.03 * fs));
    }
}

TEST_CASE ("Delta: delta output + aligned dry = processed output", "[zl][delta]")
{
    const auto input = makePink (2, 96000, 3, -18.0);
    auto setup = [] (lens::LensProcessor& p)
    {
        setEightMixedBands (p);
        setParam (p, "lookahead", 5.0f);
    };

    lens::LensProcessor a;
    setup (a);
    const auto processed = render (a, input, opts());

    lens::LensProcessor b;
    setup (b);
    setParam (b, "delta", 1.0f);
    const auto deltaOut = render (b, input, opts());
    const int latency = b.getLatencySamples();
    REQUIRE (latency == 240);

    auto sum = delayed (input, latency);
    for (int ch = 0; ch < 2; ++ch)
        sum.addFrom (ch, 0, deltaOut, ch, 0, sum.getNumSamples());
    REQUIRE (peakDifferenceDb (sum, processed) < -100.0);
}

TEST_CASE ("Auto-gain: pink noise through a +6 dB high shelf at 5 kHz stays within 1 dB", "[zl][autogain]")
{
    const double fs = 48000.0;
    const auto input = makePink (2, (int) (fs * 8), 21, -20.0);
    const double inLufs = lufsOf (input, fs, (int) fs);

    auto run = [&] (bool autoGain)
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 2, 5000.0f, 6.0f, 0.7071f });
        setParam (proc, "auto_gain", autoGain ? 1.0f : 0.0f);
        return lufsOf (render (proc, input, opts (fs)), fs, (int) fs);
    };

    const double withAuto = run (true);
    const double without = run (false);
    INFO ("in " << inLufs << " LUFS, auto-gain on " << withAuto << ", off " << without);
    REQUIRE (std::abs (withAuto - inLufs) <= 1.0);
    REQUIRE (without - inLufs > 1.5);
}

TEST_CASE ("Block size: 8 mixed bands give identical output at any block size", "[zl][blocksize]")
{
    const auto input = makePink (2, 60000, 9, -18.0);
    auto run = [&] (std::vector<int> pattern)
    {
        lens::LensProcessor proc;
        setEightMixedBands (proc);
        return render (proc, input, opts (48000.0, std::move (pattern)));
    };
    const auto reference = run ({ 512 });
    for (const auto& pattern : std::vector<std::vector<int>> { { 1 }, { 64 }, { 517 }, { 4096 }, variableBlockPattern() })
    {
        INFO ("pattern starting " << pattern.front() << " (" << pattern.size() << " sizes)");
        REQUIRE (bitIdentical (run (pattern), reference));
    }
}

TEST_CASE ("All sample rates run without NaN or inf", "[zl][rates]")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        lens::LensProcessor proc;
        setEightMixedBands (proc);
        setBand (proc, 9, { 0, 30000.0f, 18.0f, 40.0f });
        setBand (proc, 10, { 2, 25000.0f, 12.0f, 0.1f });
        setBand (proc, 11, { 3, 10.0f, 0.0f, 1.0f, 7 });
        setBand (proc, 12, { 7, 30000.0f, 30.0f });
        const auto input = makeNoise (2, (int) fs, 4, 0.9f);
        INFO ("fs " << fs);
        const auto out = render (proc, input, opts (fs, variableBlockPattern()));
        REQUIRE (allFinite (out));
    }
}

TEST_CASE ("Bypass crossfades to the latency-aligned dry signal", "[zl][bypass]")
{
    const auto input = makePink (2, 48000, 12, -18.0);
    lens::LensProcessor proc;
    setEightMixedBands (proc);
    setParam (proc, "lookahead", 2.0f);
    setParam (proc, "bypass", 1.0f);
    const auto out = render (proc, input, opts());
    REQUIRE (proc.getLatencySamples() == 96);
    REQUIRE (peakDifferenceDb (out, delayed (input, 96)) < -140.0);
}

TEST_CASE ("Output gain, A/B switching, preset crossfade and undo", "[zl][ab]")
{
    const double fs = 48000.0;
    const auto input = makeSine (2, (int) fs * 2, fs, 1000.0, 0.25f);

    SECTION ("Output gain")
    {
        lens::LensProcessor proc;
        setParam (proc, "output_gain", -6.0f);
        const auto out = render (proc, input, opts());
        REQUIRE (std::abs (rmsDb (out, 0, 24000) - rmsDb (input, 0, 24000) + 6.0) < 0.01);
    }

    SECTION ("A/B: each slot keeps its own bands, switching never clicks")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, -12.0f, 1.0f });
        setParam (proc, "auto_gain", 0.0f);
        proc.serviceMessageThread(); // slot A known

        setParam (proc, "ab_slot", 1.0f); // B starts as a copy of A
        proc.serviceMessageThread();
        proc.serviceMessageThread();
        setBandParam (proc, 1, pb::gain, 6.0f);

        setParam (proc, "ab_slot", 0.0f);
        // Without audio running, the swap applies on the next service.
        proc.serviceMessageThread();
        REQUIRE (getParam (proc, "b01_gain") == -12.0f);
        setParam (proc, "ab_slot", 1.0f);
        proc.serviceMessageThread();
        REQUIRE (getParam (proc, "b01_gain") == 6.0f);

        // While audio runs: toggle slots every 100 ms.
        auto o = opts (fs, { 64 });
        o.beforeBlock = [&] (juce::int64 pos)
        {
            if (pos > 0 && pos % 4800 < 64)
                setParam (proc, "ab_slot", getParam (proc, "ab_slot") > 0.5f ? 0.0f : 1.0f);
            proc.serviceMessageThread();
        };
        const auto out = render (proc, input, o);
        const double steadyMax = 0.25 * 2.0 * std::sin (juce::MathConstants<double>::pi * 1000.0 / fs) * 2.0; // +6 dB slot
        REQUIRE (maxStep (out, 0, 0, out.getNumSamples()) <= 1.5 * steadyMax);
    }

    SECTION ("Preset load crossfades and is undoable")
    {
        lens::LensProcessor proc;
        setBand (proc, 3, { 0, 500.0f, 4.0f, 1.0f });
        const auto before = proc.getBands();
        REQUIRE (proc.loadPreset (R"({"format_version":1,"name":"T","bands":[{"slot":1,"shape":"HighShelf","freq":8000,"gain":2,"q":0.7}]})"));
        proc.serviceMessageThread();
        REQUIRE (getParam (proc, "b01_on") == 1.0f);
        REQUIRE (getParam (proc, "b03_on") == 0.0f);
        REQUIRE (proc.undo());
        REQUIRE (getParam (proc, "b03_on") == 1.0f);
        REQUIRE (getParam (proc, "b01_on") == 0.0f);
        REQUIRE (proc.redo());
        REQUIRE (getParam (proc, "b01_on") == 1.0f);
    }

    SECTION ("Band edits undo and redo")
    {
        lens::LensProcessor proc;
        proc.beginEdit();
        setBand (proc, 2, { 0, 700.0f, 5.0f, 2.0f });
        proc.endEdit ("Add band");
        proc.beginEdit();
        setBandParam (proc, 2, pb::gain, -3.0f);
        proc.endEdit ("Gain");
        REQUIRE (proc.undo());
        REQUIRE (getParam (proc, "b02_gain") == 5.0f);
        REQUIRE (proc.undo());
        REQUIRE (getParam (proc, "b02_on") == 0.0f);
        REQUIRE_FALSE (proc.undo());
        REQUIRE (proc.redo());
        REQUIRE (proc.redo());
        REQUIRE (getParam (proc, "b02_gain") == -3.0f);
    }
}
