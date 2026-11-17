// Phase 4: dynamic EQ.
#include <catch2/catch_test_macros.hpp>

#include "DSP/Detector.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

using namespace lens::test;
namespace pb = lens::params::band;

namespace
{
RenderOptions opts (double fs = 48000.0, std::vector<int> pattern = { 256 })
{
    RenderOptions o;
    o.sampleRate = fs;
    o.blockPattern = std::move (pattern);
    return o;
}

double toneRmsDb (double amp) { return 20.0 * std::log10 (amp / std::sqrt (2.0)); }
} // namespace

TEST_CASE ("Gain computer: steady tones give the SPEC formula's gain within 0.5 dB, all four moves", "[dyn][gain]")
{
    const double fs = 48000.0;
    struct Case
    {
        int mode;
        float range, thresh;
        double levelDb; // RMS of the tone
    };
    const Case cases[] = {
        { 0, -6.0f, -30.0f, -20.0 },  // Above, cut: capped at range
        { 0, -12.0f, -30.0f, -24.0 }, // Above, cut: 6 dB over × (1 − 1/3) = −4
        { 0, 6.0f, -30.0f, -22.0 },   // Above, boost
        { 1, 8.0f, -20.0f, -30.0 },   // Below, boost
        { 1, -9.0f, -20.0f, -27.0 },  // Below, cut
        { 0, -6.0f, -20.0f, -30.0 },  // Above, below threshold: no gain
    };

    for (const auto& c : cases)
    {
        const double amp = std::pow (10.0, c.levelDb / 20.0) * std::sqrt (2.0);
        const auto tone = makeSine (2, (int) fs * 2, fs, 1000.0, (float) amp);
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 2.0f, 1.0f });
        setDyn (proc, 1, { c.mode, c.range, c.thresh, 3.0f, 5.0f, 50.0f });
        setParam (proc, "auto_gain", 0.0f);
        const auto out = render (proc, tone, opts());

        const double expected = lens::dsp::dynamicGainDb ((lens::dsp::DynMode) c.mode, c.levelDb, c.thresh, 3.0, c.range);
        const int start = (int) fs;
        const double measured = 20.0 * std::log10 (sineAmplitude (out.getReadPointer (0), start, (int) fs, fs, 1000.0) / amp) - 2.0;
        const double meter = proc.stats().bands[0].dynGainDb.load();
        INFO ("mode " << c.mode << " range " << c.range << " thresh " << c.thresh << " level " << c.levelDb << ": expected " << expected << ", applied "
                      << measured << ", meter " << meter);
        REQUIRE (std::abs (measured - expected) <= 0.5);
        // Meter: the ring meter matches the gain actually applied.
        REQUIRE (std::abs (meter - measured) <= 0.5);
    }
}

TEST_CASE ("Timing: a level step reaches 63% of its gain change within 10% of attack and release", "[dyn][timing]")
{
    const double fs = 48000.0;
    for (double attackMs : { 20.0, 50.0 })
        for (double releaseMs : { 120.0, 400.0 })
        {
            lens::dsp::Detector det;
            det.prepare (fs);
            lens::dsp::BandParams p;
            p.on = p.dynOn = true;
            p.shape = lens::dsp::Shape::Bell;
            p.dynRange = -6.0f;
            p.dynThresh = -30.0f;
            p.dynRatio = 4.0f;
            p.dynAttack = (float) attackMs;
            p.dynRelease = (float) releaseMs;
            det.configure (p, 1000.0, 1.0);

            const int up = (int) fs, down = 2 * (int) fs;
            const int n = down + (int) (fs * releaseMs * 0.001 * 12.0); // let the release settle fully
            std::vector<double> gain ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double amp = (i >= up && i < down) ? 0.5 : 0.001;
                const double x = amp * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / fs);
                double l, r;
                gain[(size_t) i] = det.process (x, x, 2, l, r);
            }
            auto crossing = [&] (int from, double target)
            {
                const double start = gain[(size_t) from - 1];
                const double level = start + 0.632 * (target - start);
                for (int i = from; i < n; ++i)
                    if ((target < start && gain[(size_t) i] <= level) || (target > start && gain[(size_t) i] >= level))
                        return 1000.0 * (i - from) / fs;
                return 1.0e9;
            };
            const double tA = crossing (up, gain[(size_t) down - 1]);
            const double tR = crossing (down, gain[(size_t) n - 1]);
            INFO ("attack " << attackMs << " ms -> " << tA << " ms; release " << releaseMs << " ms -> " << tR << " ms");
            REQUIRE (std::abs (tA - attackMs) <= 0.1 * attackMs);
            REQUIRE (std::abs (tR - releaseMs) <= 0.1 * releaseMs);
        }
}

TEST_CASE ("Detector: Custom 5-10 kHz ignores a 1 kHz tone at 0 dBFS and reacts to 7 kHz", "[dyn][detector]")
{
    const double fs = 48000.0;
    for (double f : { 1000.0, 7000.0 })
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 7000.0f, 0.0f, 3.0f });
        setDyn (proc, 1, { 0, -6.0f, -20.0f, 3.0f, 1.0f, 50.0f, false, true, 5000.0f, 10000.0f });
        const auto tone = makeSine (2, (int) fs, fs, f, 1.0f);
        render (proc, tone, opts());
        const float g = proc.stats().bands[0].dynGainDb.load();
        INFO (f << " Hz: dynamic gain " << g);
        if (f < 2000.0)
            REQUIRE (std::abs (g) < 0.05f);
        else
            REQUIRE (g < -3.0f);
    }
}

TEST_CASE ("External side-chain: the band reacts only to the side-chain signal", "[dyn][sidechain]")
{
    const double fs = 48000.0;
    const auto loud = makeSine (2, (int) fs, fs, 80.0, 0.5f);
    juce::AudioBuffer<float> silent (2, (int) fs);
    silent.clear();

    auto run = [&] (const juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& sc)
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 60.0f, 0.0f, 1.5f });
        setDyn (proc, 1, { 0, -5.0f, -30.0f, 3.0f, 1.0f, 120.0f, true, true, 40.0f, 120.0f });
        auto o = opts();
        o.sidechain = &sc;
        render (proc, main, o);
        return proc.stats().bands[0].dynGainDb.load();
    };

    REQUIRE (std::abs (run (loud, silent)) < 0.05f); // loud main, silent side-chain
    REQUIRE (run (silent, loud) < -3.0f);            // silent main, loud side-chain
    REQUIRE (run (makeSine (1, (int) fs, fs, 80.0, 0.5f), makeSine (1, (int) fs, fs, 80.0, 0.5f)) < -3.0f); // mono buses
}

TEST_CASE ("Learn: afterwards the band is active 15-25% of the time", "[dyn][learn]")
{
    const double fs = 48000.0;
    // A 1 kHz tone stepping every 50 ms through 40 levels, -50 to -11 dB
    // in a fixed shuffled order (a 2 s cycle): known level statistics
    // that are the same in the Learn window and the activity window.
    const int n = (int) (fs * 24);
    juce::AudioBuffer<float> sig (2, n);
    std::vector<int> order (40);
    for (int k = 0; k < 40; ++k)
        order[(size_t) k] = (k * 17) % 40;
    double amp = 0.01;
    for (int i = 0; i < n; ++i)
    {
        if (i % 2400 == 0)
            amp = std::pow (10.0, (-50.0 + order[(size_t) ((i / 2400) % 40)]) / 20.0) * std::sqrt (2.0);
        const float x = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / fs));
        sig.setSample (0, i, x);
        sig.setSample (1, i, x);
    }

    lens::LensProcessor proc;
    setBand (proc, 1, { 0, 1000.0f, 0.0f, 1.0f });
    setDyn (proc, 1, { 0, -6.0f, 0.0f, 3.0f, 1.0f, 30.0f });
    proc.toggleLearn (0);
    REQUIRE (proc.isLearning (0));

    int finishedAt = -1;
    proc.onLearnFinished = [&] (int) { finishedAt = 1; };
    auto o = opts();
    o.beforeBlock = [&] (juce::int64) { proc.serviceMessageThread(); };
    render (proc, sig, o);

    REQUIRE (finishedAt == 1);
    REQUIRE_FALSE (proc.isLearning (0));
    const float thresh = getParam (proc, "b01_dyn_thresh");
    const float act = proc.stats().bands[0].activity.load();
    INFO ("threshold " << thresh << " dB, activity " << act << "%");
    REQUIRE (std::abs (thresh * 2.0f - std::round (thresh * 2.0f)) < 1e-4f); // rounded to 0.5 dB
    REQUIRE (act >= 15.0f);
    REQUIRE (act <= 25.0f);
    WARN ("Learn: threshold " << thresh << " dB, activity afterwards " << act << "%");

    // Learn is undoable.
    REQUIRE (proc.undo());
    REQUIRE (getParam (proc, "b01_dyn_thresh") == 0.0f);
}

TEST_CASE ("Lookahead: reported latency equals measured latency at 0, 5 and 10 ms", "[dyn][lookahead]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (float ms : { 0.0f, 5.0f, 10.0f })
        {
            lens::LensProcessor proc;
            setBand (proc, 1, { 0, 1000.0f, 0.0f, 1.0f });
            setDyn (proc, 1, {});
            setParam (proc, "lookahead", ms);
            setParam (proc, "auto_gain", 0.0f);
            juce::AudioBuffer<float> imp (2, (int) fs / 2);
            imp.clear();
            imp.setSample (0, 1000, 1.0f);
            imp.setSample (1, 1000, 1.0f);
            const auto out = render (proc, imp, opts (fs, variableBlockPattern()));
            int peak = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak)))
                    peak = i;
            INFO ("fs " << fs << " lookahead " << ms << " ms: reported " << proc.getLatencySamples() << ", measured " << peak - 1000);
            REQUIRE (proc.getLatencySamples() == (int) std::lround (ms * 0.001 * fs));
            REQUIRE (peak - 1000 == proc.getLatencySamples());
        }
}

TEST_CASE ("Click test: 0.1 ms attack and 10 ms release on a bass-range dynamic band", "[dyn][click]")
{
    const double fs = 48000.0;
    const int n = (int) fs * 3;
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        // 1 kHz tone plus 60 Hz bursts with smooth 20 ms envelopes.
        const double t = (double) i / fs;
        const double phase = std::fmod (t, 0.25) / 0.25;
        const double env = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase);
        const double x = 0.2 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * t) + 0.5 * env * std::sin (juce::MathConstants<double>::twoPi * 60.0 * t);
        in.setSample (0, i, (float) x);
        in.setSample (1, i, (float) x);
    }
    lens::LensProcessor proc;
    setBand (proc, 1, { 0, 60.0f, 0.0f, 1.5f });
    setDyn (proc, 1, { 0, -12.0f, -40.0f, 6.0f, 0.1f, 10.0f });
    setParam (proc, "auto_gain", 0.0f);
    const auto out = render (proc, in, opts (fs, { 64 }));
    const double stepIn = maxStep (in, 0, 1, n);
    const double stepOut = maxStep (out, 0, 1, n);
    INFO ("max step in " << stepIn << ", out " << stepOut);
    REQUIRE (stepOut <= 1.5 * stepIn);
    REQUIRE (proc.stats().bands[0].activity.load() > 10.0f); // it really moved
}

TEST_CASE ("Dynamic bands keep the output independent of block size", "[dyn][blocksize]")
{
    const auto input = makePink (2, 60000, 19, -14.0);
    auto run = [&] (std::vector<int> pattern)
    {
        lens::LensProcessor proc;
        setEightMixedBands (proc);
        setDyn (proc, 2, { 0, -4.0f, -36.0f, 3.0f, 5.0f, 100.0f });
        setDyn (proc, 3, { 1, 3.0f, -40.0f, 2.0f, 1.0f, 60.0f });
        setDyn (proc, 6, { 0, -6.0f, -45.0f, 4.0f, 0.5f, 30.0f, false, true, 3000.0f, 9000.0f });
        setParam (proc, "lookahead", 3.0f);
        return render (proc, input, opts (48000.0, std::move (pattern)));
    };
    const auto reference = run ({ 512 });
    for (const auto& pattern : std::vector<std::vector<int>> { { 1 }, { 64 }, { 517 }, { 4096 }, variableBlockPattern() })
        REQUIRE (bitIdentical (run (pattern), reference));
}
