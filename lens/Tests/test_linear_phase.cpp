// Phase 6: Linear Phase.
#include <catch2/catch_test_macros.hpp>

#include "DSP/FilterDesign.h"
#include "DSP/LinearPhase.h"
#include "DSP/Warm.h"
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

void staticSet (lens::LensProcessor& p)
{
    setBand (p, 1, { 3, 25.0f, 0.0f, 1.0f, 1 });   // Low Cut 25 Hz 12 dB
    setBand (p, 2, { 0, 100.0f, 3.0f, 1.0f });      // Bell
    setBand (p, 3, { 0, 400.0f, -4.0f, 2.0f });     // Bell
    setBand (p, 4, { 1, 180.0f, -2.0f, 0.7f });     // Low Shelf
    setBand (p, 5, { 0, 2500.0f, 2.0f, 1.5f });     // Bell
    setBand (p, 6, { 2, 8000.0f, 2.0f, 0.7f });     // High Shelf
    setBand (p, 7, { 7, 1000.0f, 1.0f });           // Tilt
}

lens::dsp::EngineParams paramsOf (lens::LensProcessor& p)
{
    lens::dsp::EngineParams e;
    p.readParams (e);
    return e;
}

double analogDb (const lens::dsp::EngineParams& e, double f)
{
    std::complex<double> h (1.0, 0.0);
    for (const auto& b : e.bands)
        if (b.on)
            h *= lens::dsp::analogPrototype (b.shape, b.freq, b.gain, b.q, b.slope, f);
    return lens::dsp::toDb (std::abs (h));
}

juce::AudioBuffer<float> impulseThrough (lens::LensProcessor& p, double fs, int length, int at = 100)
{
    juce::AudioBuffer<float> imp (2, length);
    imp.clear();
    imp.setSample (0, at, 1.0f);
    imp.setSample (1, at, 1.0f);
    return render (p, imp, opts (fs, { 512 }));
}
} // namespace

TEST_CASE ("Linear Phase: static impulse response is symmetric around its centre", "[lp]")
{
    lens::LensProcessor p;
    staticSet (p);
    setParam (p, "phase_mode", 1.0f);
    setParam (p, "auto_gain", 0.0f);
    const int taps = 16384;
    const auto out = impulseThrough (p, 48000.0, taps * 2);
    const int c = 100 + p.getLatencySamples();
    REQUIRE (p.getLatencySamples() == lens::dsp::LinearPhaseEngine::latencyFor (taps));

    double peak = 0.0, asym = 0.0;
    for (int k = 0; k < taps / 2; ++k)
    {
        const double a = out.getSample (0, c + k), b = out.getSample (0, c - k);
        peak = std::max (peak, std::abs (a));
        asym = std::max (asym, std::abs (a - b));
    }
    const double db = 20.0 * std::log10 (asym / peak);
    INFO ("asymmetry " << db << " dB");
    REQUIRE (db < -100.0);
    WARN ("Linear Phase symmetry: asymmetry " << db << " dB below the peak");
}

TEST_CASE ("Linear Phase: magnitude matches the analog target", "[lp]")
{
    struct Res
    {
        int index, taps;
        double from, tol;
    };
    for (const auto& r : { Res { 1, 16384, 40.0, 0.2 }, Res { 2, 65536, 20.0, 0.5 } })
    {
        lens::LensProcessor p;
        staticSet (p);
        setParam (p, "phase_mode", 1.0f);
        setParam (p, "lp_resolution", (float) r.index);
        setParam (p, "auto_gain", 0.0f);
        const auto out = impulseThrough (p, 48000.0, r.taps * 2);
        std::vector<double> h ((size_t) r.taps);
        const int skip = lens::dsp::LinearPhaseEngine::partitionSize (r.taps);
        for (int i = 0; i < r.taps; ++i)
            h[(size_t) i] = out.getSample (0, 100 + skip + i);
        const auto e = paramsOf (p);
        double worst = 0.0, worstF = 0.0;
        for (double f = r.from; f <= 20000.0; f *= std::pow (2.0, 1.0 / 24.0))
        {
            const double measured = lens::dsp::toDb (std::abs (dftAt (h, 48000.0, f)));
            const double target = analogDb (e, f);
            if (std::abs (measured - target) > worst)
            {
                worst = std::abs (measured - target);
                worstF = f;
            }
        }
        INFO ("taps " << r.taps << ": worst " << worst << " dB at " << worstF << " Hz");
        REQUIRE (worst <= r.tol);
        WARN ("Linear Phase " << (r.index == 1 ? "Medium" : "High") << ": worst error " << worst << " dB at " << worstF << " Hz");
    }
}

TEST_CASE ("Linear Phase: reported latency equals measured latency at every resolution and sample rate", "[lp][latency]")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int res : { 0, 1, 2 })
        {
            lens::LensProcessor p;
            setBand (p, 1, { 0, 1000.0f, 3.0f, 1.0f });
            setBand (p, 2, { 0, 120.0f, -2.0f, 1.0f });
            setParam (p, "phase_mode", 1.0f);
            setParam (p, "lp_resolution", (float) res);
            setParam (p, "auto_gain", 0.0f);
            const int taps = lens::dsp::Engine::linearPhaseTaps ((lens::dsp::LpResolution) res, fs);
            const auto out = impulseThrough (p, fs, taps + 4 * lens::dsp::LinearPhaseEngine::partitionSize (taps));
            int peak = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak)))
                    peak = i;
            INFO ("fs " << fs << " res " << res << ": reported " << p.getLatencySamples() << ", measured " << peak - 100);
            REQUIRE (p.getLatencySamples() == lens::dsp::LinearPhaseEngine::latencyFor (taps));
            REQUIRE (peak - 100 == p.getLatencySamples());
            REQUIRE (p.getTailLengthSeconds() >= 1.0);
        }
}

TEST_CASE ("Linear Phase: Mid/Side and Left/Right tests from Phase 2", "[lp][stereo]")
{
    const auto monoSum = makePink (2, 96000, 5, -18.0, true);
    const auto stereoIn = makeNoise (2, 96000, 6);
    const int lat = lens::dsp::LinearPhaseEngine::latencyFor (16384);

    SECTION ("A Side band on mono-summed input leaves the output unchanged")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 12.0f, 1.0f, 1, 2 });
        setBand (proc, 2, { 2, 5000.0f, -9.0f, 0.7f, 1, 2 });
        setParam (proc, "auto_gain", 0.0f);
        setParam (proc, "phase_mode", 1.0f);
        const auto out = render (proc, monoSum, opts());
        REQUIRE (proc.getLatencySamples() == lat);
        const double d = peakDifferenceDb (out, delayed (monoSum, lat));
        INFO ("difference " << d << " dB");
        REQUIRE (d < -120.0);
    }

    SECTION ("A Left band leaves the right channel bit-identical")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 12.0f, 1.0f, 1, 3 });
        setBand (proc, 2, { 3, 300.0f, 0.0f, 1.0f, 7, 3 });
        setParam (proc, "auto_gain", 0.0f);
        setParam (proc, "phase_mode", 1.0f);
        const auto out = render (proc, stereoIn, opts());
        const auto ref = delayed (stereoIn, lat);
        REQUIRE (std::memcmp (out.getReadPointer (1), ref.getReadPointer (1), sizeof (float) * 96000) == 0);
        REQUIRE (peakDifferenceDb (out, ref) > -40.0);
    }

    SECTION ("Mid/Side encode-decode with 0 dB bands is transparent")
    {
        lens::LensProcessor proc;
        setBand (proc, 1, { 0, 1000.0f, 0.0f, 1.0f, 1, 1 });
        setBand (proc, 2, { 1, 200.0f, 0.0f, 0.7f, 1, 2 });
        setParam (proc, "phase_mode", 1.0f);
        const auto out = render (proc, stereoIn, opts());
        REQUIRE (peakDifferenceDb (out, delayed (stereoIn, lat)) < -120.0);
    }

    SECTION ("All bands off is a bit-exact delay")
    {
        lens::LensProcessor proc;
        setParam (proc, "phase_mode", 1.0f);
        const auto out = render (proc, stereoIn, opts());
        REQUIRE (bitIdentical (out, delayed (stereoIn, lat)));
    }
}

TEST_CASE ("Linear Phase: a dynamic band's gain change matches Zero Latency within 1 dB and 1 ms", "[lp][dyn]")
{
    const double fs = 48000.0;
    const int n = (int) (fs * 3);
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double amp = (i >= (int) fs && i < 2 * (int) fs) ? 0.5 : 0.02;
        const float x = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / fs));
        in.setSample (0, i, x);
        in.setSample (1, i, x);
    }
    auto run = [&] (bool lp)
    {
        lens::LensProcessor p;
        setBand (p, 1, { 0, 1000.0f, 0.0f, 2.0f });
        setDyn (p, 1, { 0, -8.0f, -30.0f, 4.0f, 10.0f, 100.0f });
        setBand (p, 2, { 2, 9000.0f, 2.0f, 0.7f }); // a non-overlapping static band
        setParam (p, "auto_gain", 0.0f);
        setParam (p, "phase_mode", lp ? 1.0f : 0.0f);
        auto out = render (p, in, opts (fs, { 256 }));
        return std::make_pair (out, p.getLatencySamples());
    };
    const auto [zl, zlLat] = run (false);
    const auto [lp, lpLat] = run (true);
    REQUIRE (zlLat == 0);

    // Gain envelope (dB re input) in 1 ms windows, aligned by latency.
    auto envelope = [&] (const juce::AudioBuffer<float>& out, int lat)
    {
        std::vector<double> env;
        const int w = 48;
        for (int start = 0; start + lat + w < n; start += w)
        {
            const double a = sineAmplitude (out.getReadPointer (0), start + lat, w, fs, 1000.0);
            const double ref = sineAmplitude (in.getReadPointer (0), start, w, fs, 1000.0);
            env.push_back (20.0 * std::log10 (std::max (1e-9, a) / std::max (1e-9, ref)));
        }
        return env;
    };
    const auto eZ = envelope (zl, 0), eL = envelope (lp, lpLat);
    // Compare the gain curves; skip the 1 ms windows within 20 ms of the
    // input's own 28 dB level steps, where a linear-phase filter's
    // symmetric transient (pre-ringing) dominates the measurement.
    double worst = 0.0;
    for (size_t i = 10; i < std::min (eZ.size(), eL.size()) - 10; ++i)
    {
        const bool nearStep = (i >= 980 && i <= 1020) || (i >= 1980 && i <= 2020);
        if (! nearStep)
            worst = std::max (worst, std::abs (eZ[i] - eL[i]));
    }

    auto crossing = [] (const std::vector<double>& e, size_t from)
    {
        const double target = e[from + 400];
        const double start = e[from - 2];
        for (size_t i = from; i < from + 400; ++i)
            if (e[i] <= start + 0.632 * (target - start))
                return (double) (i - from);
        return 1e9;
    };
    const double tZ = crossing (eZ, 1000), tL = crossing (eL, 1000);
    INFO ("worst gain difference " << worst << " dB; 63% at " << tZ << " ms (ZL) vs " << tL << " ms (LP)");
    REQUIRE (worst <= 1.0);
    REQUIRE (std::abs (tZ - tL) <= 1.0);
    WARN ("Linear Phase dynamic band vs Zero Latency: worst " << worst << " dB, timing " << tZ << " vs " << tL << " ms");
}

TEST_CASE ("Linear Phase: mode switch fades without clicks and reports the new latency", "[lp][switch]")
{
    const double fs = 48000.0;
    const auto in = makeSine (2, (int) fs * 5, fs, 1000.0, 0.3f);
    lens::LensProcessor p;
    setBand (p, 1, { 0, 3000.0f, 3.0f, 1.0f });
    setParam (p, "auto_gain", 0.0f);
    std::vector<int> latencies;
    auto o = opts (fs, { 256 });
    o.beforeBlock = [&] (juce::int64 pos)
    {
        if (pos == 48128)
            setParam (p, "phase_mode", 1.0f);
        if (pos == 3 * 48000 - 128)
            setParam (p, "phase_mode", 0.0f);
        p.serviceMessageThread();
        latencies.push_back (p.getLatencySamples());
        juce::Thread::sleep (pos % 4096 == 0 ? 1 : 0); // give the kernel builder time
    };
    const auto out = render (p, in, o);
    const double steady = maxStep (in, 0, 1000, 20000) * 1.42;
    const double worst = maxStep (out, 0, 1, out.getNumSamples());
    INFO ("worst step " << worst << ", steady " << steady);
    REQUIRE (worst <= 1.5 * steady);
    REQUIRE (std::find (latencies.begin(), latencies.end(), lens::dsp::LinearPhaseEngine::latencyFor (16384)) != latencies.end());
    REQUIRE (latencies.back() == 0);
    // Sound came back after the switch to Linear Phase.
    REQUIRE (sineAmplitude (out.getReadPointer (0), (int) (2.0 * fs), 4800, fs, 1000.0) > 0.3);
}

TEST_CASE ("Linear Phase: block size does not change the output", "[lp][blocksize]")
{
    const auto input = makePink (2, 60000, 9, -18.0);
    auto run = [&] (std::vector<int> pattern)
    {
        lens::LensProcessor proc;
        setEightMixedBands (proc);
        setDyn (proc, 3, { 0, -4.0f, -40.0f, 3.0f, 5.0f, 100.0f });
        setParam (proc, "phase_mode", 1.0f);
        setParam (proc, "lp_resolution", 0.0f);
        return render (proc, input, opts (48000.0, std::move (pattern)));
    };
    const auto reference = run ({ 512 });
    for (const auto& pattern : std::vector<std::vector<int>> { { 1 }, { 64 }, { 517 }, { 4096 }, variableBlockPattern() })
    {
        const double d = peakDifferenceDb (run (pattern), reference);
        INFO ("pattern " << pattern.front() << ": " << d << " dB");
        REQUIRE (d < -100.0);
    }
}

// ---------------------------------------------------------------- Phase 7: brickwall, Warm

TEST_CASE ("Brickwall cuts in Linear Phase: at least 60 dB one-sixth octave past the corner", "[lp][brickwall]")
{
    for (int shape : { 3, 4 }) // Low Cut, High Cut
        for (float corner : { 200.0f, 1000.0f, 5000.0f })
        {
            lens::LensProcessor p;
            setBand (p, 1, { shape, corner, 0.0f, 1.0f, 8 });
            setParam (p, "phase_mode", 1.0f);
            setParam (p, "auto_gain", 0.0f);
            const int taps = 16384;
            const auto out = impulseThrough (p, 48000.0, taps * 2);
            std::vector<double> h ((size_t) taps);
            const int skip = lens::dsp::LinearPhaseEngine::partitionSize (taps);
            for (int i = 0; i < taps; ++i)
                h[(size_t) i] = out.getSample (0, 100 + skip + i);
            const double past = shape == 3 ? corner * std::pow (2.0, -1.0 / 6.0) : corner * std::pow (2.0, 1.0 / 6.0);
            const double att = -lens::dsp::toDb (std::abs (dftAt (h, 48000.0, past)));
            const double pass = lens::dsp::toDb (std::abs (dftAt (h, 48000.0, shape == 3 ? corner * 2.0 : corner / 2.0)));
            INFO ((shape == 3 ? "low" : "high") << " cut at " << corner << ": " << att << " dB down 1/6 octave past, passband " << pass << " dB");
            REQUIRE (att >= 60.0);
            REQUIRE (std::abs (pass) < 0.1);
        }
}

TEST_CASE ("Warm: level within 0.5 dB of Clean on pink noise at -18 dBFS; latency reported", "[warm]")
{
    const double fs = 48000.0;
    const auto pink = makePink (2, (int) fs * 4, 41, -18.0);
    auto run = [&] (bool warm)
    {
        lens::LensProcessor p;
        setBand (p, 1, { 0, 1000.0f, 2.0f, 1.0f });
        setParam (p, "character", warm ? 1.0f : 0.0f);
        auto out = render (p, pink, opts());
        return std::make_pair (out, p.getLatencySamples());
    };
    const auto [clean, latC] = run (false);
    const auto [warm, latW] = run (true);
    REQUIRE (latC == 0);
    REQUIRE (latW > 0);
    const double dc = rmsDb (clean, 0, (int) fs), dw = rmsDb (warm, 0, (int) fs);
    INFO ("clean " << dc << " dB, warm " << dw << " dB, latency " << latW);
    REQUIRE (std::abs (dw - dc) <= 0.5);
    // It does change the sound: a loud sine gains harmonics.
    REQUIRE (lens::dsp::WarmStage::shape (0.0f) == 0.0f);
    REQUIRE (std::abs (lens::dsp::WarmStage::shape (0.01f) - 0.01f) < 1e-4f);
    REQUIRE (lens::dsp::WarmStage::shape (0.9f) < 0.9f);
}
