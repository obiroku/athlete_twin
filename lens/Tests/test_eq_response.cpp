// Phase 2: filter response, slopes and Tilt (DSP level).
#include <catch2/catch_test_macros.hpp>

#include "DSP/EqBand.h"
#include "DSP/FilterDesign.h"
#include "helpers/Signals.h"

#include <cmath>

using namespace lens::dsp;

namespace
{
std::vector<double> impulseResponse (const BandParams& bp, double fs, int length)
{
    EqBand band;
    band.prepare (fs, 1);
    band.setParams (bp, true);
    std::vector<double> h ((size_t) length, 0.0), dummy ((size_t) length, 0.0);
    h[0] = 1.0;
    band.process (h.data(), dummy.data(), length, nullptr);
    return h;
}

int lengthFor (double freq, double q, double fs)
{
    const double tau = 2.0 * std::max (q, 0.75) / (juce::MathConstants<double>::twoPi * std::max (freq, 20.0));
    return (int) juce::jlimit (8192.0, 262144.0, 40.0 * tau * fs + 4096.0);
}

std::vector<double> logSpaced (double lo, double hi, int n)
{
    std::vector<double> v;
    for (int i = 0; i < n; ++i)
        v.push_back (lo * std::pow (hi / lo, (double) i / (n - 1)));
    return v;
}
} // namespace

TEST_CASE ("Every shape matches its digital design within 0.1 dB and the analog prototype within 0.5 dB below fs/4", "[response]")
{
    const double fs = 48000.0;
    const auto bandFreqs = logSpaced (20.0, 20000.0, 20);
    const auto checkFreqs = logSpaced (20.0, 21000.0, 16);
    const double gains[] = { -18.0, -6.0, 0.0, 6.0, 18.0 };
    const double qs[] = { 0.5, 1.5, 6.0 };

    double worstDigital = 0.0, worstAnalog = 0.0;
    int cases = 0;

    for (int shapeIdx = 0; shapeIdx <= (int) Shape::AllPass; ++shapeIdx)
    {
        const auto shape = (Shape) shapeIdx;
        const bool gainShape = usesGain (shape);
        const bool qShape = ! isCut (shape) && shape != Shape::Tilt;

        for (double f : bandFreqs)
            for (double g : gains)
            {
                if (! gainShape && g != 0.0) // NOLINT
                    continue;
                for (double q : qs)
                {
                    if (! qShape && q != 1.5) // NOLINT
                        continue;

                    BandParams bp;
                    bp.on = true;
                    bp.shape = shape;
                    bp.freq = (float) f;
                    bp.gain = (float) g;
                    bp.q = (float) q;
                    bp.slope = 1;

                    const auto h = impulseResponse (bp, fs, lengthFor (f, q, fs));
                    const auto design = designBand (shape, bp.freq, bp.gain, bp.q, bp.slope, fs);
                    const auto analog = designAnalog (shape, bp.freq, bp.gain, bp.q, bp.slope);
                    ++cases;

                    for (double cf : checkFreqs)
                    {
                        const double expected = toDb (std::abs (responseDigital (design, cf, fs)));
                        if (expected < -60.0)
                            continue;
                        const double measured = toDb (std::abs (lens::test::dftAt (h, fs, cf)));
                        const double dErr = std::abs (measured - expected);
                        worstDigital = std::max (worstDigital, dErr);
                        if (dErr > 0.1)
                        {
                            INFO ("shape " << shapeIdx << " f " << f << " g " << g << " q " << q << " at " << cf << ": " << measured << " vs " << expected);
                            REQUIRE (dErr <= 0.1);
                        }

                        if (cf < fs / 4.0 && f < fs / 4.0)
                        {
                            const double a = toDb (std::abs (responseAnalog (analog, cf)));
                            if (a < -50.0)
                                continue;
                            const double aErr = std::abs (measured - a);
                            worstAnalog = std::max (worstAnalog, aErr);
                            if (aErr > 0.5)
                            {
                                INFO ("analog: shape " << shapeIdx << " f " << f << " g " << g << " q " << q << " at " << cf << ": " << measured << " vs " << a);
                                REQUIRE (aErr <= 0.5);
                            }
                        }
                    }
                }
            }
    }
    WARN ("response cases " << cases << ", worst digital error " << worstDigital << " dB, worst analog error " << worstAnalog << " dB");
}

TEST_CASE ("Cut slopes: attenuation two octaves past the corner within 1 dB of Butterworth", "[response]")
{
    const double fs = 48000.0;
    for (int slope = 0; slope < 8; ++slope)
    {
        const int order = slopeOrder (slope);
        const double butter = -10.0 * std::log10 (1.0 + std::pow (4.0, 2.0 * order));

        for (bool low : { true, false })
        {
            BandParams bp;
            bp.on = true;
            bp.shape = low ? Shape::LowCut : Shape::HighCut;
            bp.freq = low ? 1000.0f : 250.0f;
            bp.slope = slope;
            const auto h = impulseResponse (bp, fs, 1 << 16);
            const double at = low ? 250.0 : 1000.0;
            const double measured = toDb (std::abs (lens::test::dftAt (h, fs, at)));
            INFO ((low ? "low" : "high") << " cut " << slopeDbPerOct (slope) << " dB/oct: " << measured << " vs " << butter);
            REQUIRE (std::abs (measured - butter) <= 1.0);

            // −3 dB at the corner.
            const double corner = toDb (std::abs (lens::test::dftAt (h, fs, bp.freq)));
            REQUIRE (std::abs (corner + 3.01) < 0.1);
        }
    }
}

TEST_CASE ("Tilt: positive gain brightens, lows down gain/2 and highs up gain/2", "[response]")
{
    for (double g : { -6.0, 4.0, 12.0 })
    {
        const auto d = designAnalog (Shape::Tilt, 1000.0, g, 1.0, 0);
        REQUIRE (std::abs (toDb (std::abs (responseAnalog (d, 5.0))) + g / 2) < 0.1);
        REQUIRE (std::abs (toDb (std::abs (responseAnalog (d, 200000.0))) - g / 2) < 0.1);
        REQUIRE (std::abs (toDb (std::abs (responseAnalog (d, 1000.0)))) < 0.01);
    }
}

TEST_CASE ("Bands set above 0.45 x sample rate are clamped", "[response]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto d = designBand (Shape::Bell, 30000.0, 6.0, 1.0, 1, fs);
        REQUIRE (d.fc <= 0.45 * fs + 1e-9);
        REQUIRE (std::isfinite (std::abs (responseDigital (d, 1000.0, fs))));
    }
}
