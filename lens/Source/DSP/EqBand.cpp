#include "EqBand.h"

#include <algorithm>
#include <cmath>

namespace lens::dsp
{
// ---------------------------------------------------------------- BandFilter

void BandFilter::setTopology (StereoMode m, int numChannels) noexcept
{
    mode = m;
    channels = numChannels > 1 ? 2 : 1;
}

void BandFilter::setDesign (const BandDesign& d) noexcept
{
    const bool sameShape = d.numSvf == numSvf && d.hasOnePole == hasOnePole;
    numSvf = d.numSvf;
    for (int i = 0; i < numSvf; ++i)
        svf[(size_t) i] = SvfRun::from (d.svf[(size_t) i]);
    hasOnePole = d.hasOnePole;
    if (hasOnePole)
        onePole = OnePoleRun::from (d.onePole);
    identity = d.isIdentity();
    if (! sameShape)
        reset();
}

void BandFilter::setSection0 (const SvfSection& s) noexcept
{
    svf[0] = SvfRun::from (s);
}

void BandFilter::reset() noexcept
{
    for (auto& chState : st)
        chState.fill ({});
    opState[0] = opState[1] = 0.0;
}

void BandFilter::processSample (double& L, double& R) noexcept
{
    if (channels == 1)
    {
        if (mode != StereoMode::Side)
            L = tickChannel (0, L);
        return;
    }

    switch (mode)
    {
        case StereoMode::Stereo:
            L = tickChannel (0, L);
            R = tickChannel (1, R);
            break;
        case StereoMode::Mid:
        {
            const double m = 0.5 * (L + R), s = 0.5 * (L - R);
            const double mf = tickChannel (0, m);
            L = mf + s;
            R = mf - s;
            break;
        }
        case StereoMode::Side:
        {
            const double m = 0.5 * (L + R), s = 0.5 * (L - R);
            const double sf = tickChannel (0, s);
            L = m + sf;
            R = m - sf;
            break;
        }
        case StereoMode::Left:
            L = tickChannel (0, L);
            break;
        case StereoMode::Right:
            R = tickChannel (1, R);
            break;
    }
}

void BandFilter::process (double* L, double* R, int n) noexcept
{
    if (identity)
        return;
    for (int i = 0; i < n; ++i)
        processSample (L[i], R[i]);
}

// ---------------------------------------------------------------- EqBand

void EqBand::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    channels = numChannels > 1 ? 2 : 1;
    smoothLen = (int) std::lround (0.020 * fs);
    fadeLen = (int) std::lround (0.010 * fs);
    firstTick = true;
    setParams (target, true);
}

EqBand::Config EqBand::configOf (const BandParams& p) noexcept
{
    Config c;
    c.on = p.on;
    c.shape = p.shape;
    c.slope = isCut (p.shape) ? p.slope : 0;
    c.mode = p.stereo;
    return c;
}

void EqBand::applyConfig (BandFilter& f, const Config& c) noexcept
{
    f.setTopology (c.mode, channels);
    f.reset();
}

double EqBand::smoothedFreq() const noexcept { return std::exp (logFreq.current); }
double EqBand::smoothedQ() const noexcept { return std::exp (logQ.current); }

void EqBand::setParams (const BandParams& p, bool snap) noexcept
{
    target = p;
    wanted = configOf (p);

    const double lf = std::log (std::max (1.0, (double) p.freq));
    const double lq = std::log (std::max (0.001, (double) p.q));

    // A band that is off (and not fading out) follows its parameters
    // silently, so turning it on starts exactly where it is set.
    const bool silent = ! config.on && fadeLeft == 0;
    if (snap || silent)
    {
        logFreq.snap (lf);
        gainDb.snap (p.gain);
        logQ.snap (lq);
    }
    else
    {
        logFreq.setTarget (lf, smoothLen);
        gainDb.setTarget (p.gain, smoothLen);
        logQ.setTarget (lq, smoothLen);
    }

    if (snap)
    {
        config = wanted;
        fadeLeft = 0;
        applyConfig (cur, config);
        redesign();
        cur.reset();
        designed = false;
    }
}

void EqBand::redesign() noexcept
{
    if (! config.on)
    {
        currentDesign = {};
        cur.setDesign (currentDesign);
        return;
    }
    const double f = smoothedFreq();
    currentDesign = designBand (config.shape, f, gainDb.current, smoothedQ(), config.slope, fs);
    cur.setDesign (currentDesign);
    lastDynTotal = 1.0e9;
}

void EqBand::tick() noexcept
{
    if (! firstTick)
    {
        logFreq.skip (tickInterval);
        gainDb.skip (tickInterval);
        logQ.skip (tickInterval);
    }
    firstTick = false;

    if (wanted != config && fadeLeft == 0)
    {
        prev = cur;
        config = wanted;
        applyConfig (cur, config);
        // Snap smoothing when a band turns on: it has no previous sound.
        fadeLeft = fadeLen;
    }

    if (! (config.on || fadeLeft > 0))
        return;

    // Redesign only when something the design depends on has moved.
    const bool moved = logFreq.current != designedFreq || gainDb.current != designedGain // NOLINT exact compare
                       || logQ.current != designedQ || config != designedConfig || ! designed;
    if (moved)
    {
        redesign();
        designedFreq = logFreq.current;
        designedGain = gainDb.current;
        designedQ = logQ.current;
        designedConfig = config;
        designed = true;
    }
}

void EqBand::process (double* L, double* R, int n, const double* dynDb) noexcept
{
    const bool dynamic = dynDb != nullptr && config.on && canBeDynamic (config.shape);

    if (fadeLeft <= 0 && ! dynamic)
    {
        if (config.on)
            cur.process (L, R, n);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        if (dynamic)
        {
            // Recompute when the total gain has moved by more than
            // 0.005 dB: far below audibility, and it saves most updates.
            const double total = gainDb.current + dynDb[i];
            if (std::abs (total - lastDynTotal) > 0.005)
            {
                lastDynTotal = total;
                const double A = std::pow (10.0, total / 40.0);
                cur.setSection0 (gainSection (config.shape, currentDesign.fc, currentDesign.q, A, fs));
            }
        }

        if (fadeLeft > 0)
        {
            double l1 = L[i], r1 = R[i];
            double l2 = L[i], r2 = R[i];
            if (config.on)
                cur.processSample (l1, r1);
            prev.processSample (l2, r2);
            // Raised-cosine weight from old to new.
            const double x = (double) fadeLeft / (double) fadeLen;
            const double wOld = 0.5 - 0.5 * std::cos (3.14159265358979323846 * x);
            L[i] = l1 + (l2 - l1) * wOld;
            R[i] = r1 + (r2 - r1) * wOld;
            --fadeLeft;
        }
        else if (config.on)
        {
            cur.processSample (L[i], R[i]);
        }
    }
}
} // namespace lens::dsp
