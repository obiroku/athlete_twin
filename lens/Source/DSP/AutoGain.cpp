#include "AutoGain.h"

#include <cmath>

#include "Loudness.h"

namespace lens::dsp
{
void AutoGainEstimator::prepare (double fs)
{
    KWeighting kw;
    kw.prepare (fs);
    const double lo = 20.0;
    const double hi = std::min (20000.0, 0.45 * fs);
    weightSum = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        // Log-spaced points: pink noise has equal power per octave.
        const double f = lo * std::pow (hi / lo, (i + 0.5) / numPoints);
        w[(size_t) i] = std::tan (3.14159265358979323846 * f / fs);
        weight[(size_t) i] = kw.powerAt (f, fs);
        weightSum += weight[(size_t) i];
    }
}

double AutoGainEstimator::loudnessChangeDb (const BandCurve* bands, int numBands, int numChannels) const noexcept
{
    double acc = 0.0;
    for (int i = 0; i < numPoints; ++i)
    {
        // Power per path: stereo, left-only, right-only, mid-only, side-only.
        double pSt = 1.0, pL = 1.0, pR = 1.0, pM = 1.0, pS = 1.0;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& bc = bands[b];
            if (bc.design == nullptr || bc.design->isIdentity())
                continue;
            const double p = std::norm (responseAtW (*bc.design, w[(size_t) i]));
            StereoMode m = bc.mode;
            if (numChannels < 2)
            {
                if (m == StereoMode::Side)
                    continue;
                m = StereoMode::Stereo;
            }
            switch (m)
            {
                case StereoMode::Stereo: pSt *= p; break;
                case StereoMode::Left: pL *= p; break;
                case StereoMode::Right: pR *= p; break;
                case StereoMode::Mid: pM *= p; break;
                case StereoMode::Side: pS *= p; break;
            }
        }
        // Left/right and mid/side halves treated as uncorrelated halves.
        const double total = pSt * 0.5 * (pL + pR) * 0.5 * (pM + pS);
        acc += weight[(size_t) i] * total;
    }
    const double ratio = acc / weightSum;
    return 10.0 * std::log10 (ratio > 1.0e-12 ? ratio : 1.0e-12);
}
} // namespace lens::dsp
