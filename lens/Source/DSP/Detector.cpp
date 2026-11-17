#include "Detector.h"

#include <algorithm>
#include <cmath>

#include "Smoothers.h"

namespace lens::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double kneeWidth = 6.0;
} // namespace

double LearnHistogram::percentile (double fraction) const noexcept
{
    if (count <= 0)
        return minDb;
    const auto targetCount = (long long) std::ceil (fraction * (double) count);
    long long acc = 0;
    for (int i = 0; i < numBins; ++i)
    {
        acc += bins[(size_t) i];
        if (acc >= targetCount)
            return minDb + (i + 0.5) * binDb;
    }
    return minDb + numBins * binDb;
}

double kneeOver (double over) noexcept
{
    const double h = 0.5 * kneeWidth;
    if (over <= -h)
        return 0.0;
    if (over >= h)
        return over;
    return (over + h) * (over + h) / (2.0 * kneeWidth);
}

double dynamicGainDb (DynMode mode, double levelDb, double thresholdDb, double ratio, double rangeDb) noexcept
{
    const double over = mode == DynMode::Above ? levelDb - thresholdDb : thresholdDb - levelDb;
    const double eff = kneeOver (over);
    if (eff <= 0.0)
        return 0.0;
    const double amount = std::min (std::abs (rangeDb), eff * (1.0 - 1.0 / ratio));
    return rangeDb < 0.0 ? -amount : amount;
}

void Detector::prepare (double sampleRate)
{
    fs = sampleRate;
    windowLen = std::max (1, (int) std::lround (0.002 * fs));
    window.assign ((size_t) windowLen, 0.0);
    reset();
}

void Detector::reset() noexcept
{
    configured = false;
    a.reset();
    b.reset();
    std::fill (window.begin(), window.end(), 0.0);
    windowPos = 0;
    windowSum = 0.0;
    sinceRecompute = 0;
    levelDb = -200.0;
    gain = 0.0;
    bucketMoving = bucketSamples = 0;
    bucketGainSum = 0.0;
    movingBuckets.fill (0);
    sampleBuckets.fill (0);
    gainBuckets.fill (0.0);
    bucketIndex = 0;
}

void Detector::configure (const BandParams& params, double bandFreq, double bandQ) noexcept
{
    // Skip when nothing the detector depends on has changed.
    if (configured && bandFreq == lastFreq && bandQ == lastQ && params.shape == p.shape // NOLINT exact compare
        && params.detCustom == p.detCustom && params.detHp == p.detHp && params.detLp == p.detLp
        && params.dynAttack == p.dynAttack && params.dynRelease == p.dynRelease)
    {
        p = params;
        return;
    }
    configured = true;
    lastFreq = bandFreq;
    lastQ = bandQ;
    p = params;
    auto tanOf = [this] (double f) { return std::tan (pi * std::clamp (f, 10.0, 0.45 * fs) / fs); };

    if (p.detCustom)
    {
        a.set (highPassSection (tanOf (p.detHp), 0.7071067811865476));
        b.set (lowPassSection (tanOf (p.detLp), 0.7071067811865476));
        twoStage = true;
    }
    else
    {
        twoStage = false;
        switch (p.shape)
        {
            case Shape::LowShelf: a.set (lowPassSection (tanOf (bandFreq), 0.7071067811865476)); break;
            case Shape::HighShelf: a.set (highPassSection (tanOf (bandFreq), 0.7071067811865476)); break;
            case Shape::Bell:
            default: a.set (bandPassSection (tanOf (bandFreq), std::max (0.1, bandQ))); break;
        }
    }

    attackCoeff = onePoleCoeff (std::max (0.1, (double) p.dynAttack) * 0.001, fs);
    releaseCoeff = onePoleCoeff (std::max (1.0, (double) p.dynRelease) * 0.001, fs);
}

double Detector::process (double inL, double inR, int numChannels, double& listenL, double& listenR) noexcept
{
    double power;
    if (numChannels < 2)
    {
        const double y = filterTick (0, inL);
        listenL = listenR = y;
        power = y * y;
    }
    else
    {
        switch (p.stereo)
        {
            case StereoMode::Mid:
            {
                const double y = filterTick (0, 0.5 * (inL + inR));
                listenL = listenR = y;
                power = y * y;
                break;
            }
            case StereoMode::Side:
            {
                const double y = filterTick (0, 0.5 * (inL - inR));
                listenL = listenR = y;
                power = y * y;
                break;
            }
            case StereoMode::Left:
            {
                const double y = filterTick (0, inL);
                listenL = listenR = y;
                power = y * y;
                break;
            }
            case StereoMode::Right:
            {
                const double y = filterTick (1, inR);
                listenL = listenR = y;
                power = y * y;
                break;
            }
            case StereoMode::Stereo:
            default:
            {
                const double yl = filterTick (0, inL);
                const double yr = filterTick (1, inR);
                listenL = yl;
                listenR = yr;
                power = 0.5 * (yl * yl + yr * yr);
                break;
            }
        }
    }

    // 2 ms boxcar RMS. The running sum is recomputed periodically so
    // rounding can never accumulate.
    windowSum += power - window[(size_t) windowPos];
    window[(size_t) windowPos] = power;
    if (++windowPos >= windowLen)
        windowPos = 0;
    if (++sinceRecompute >= 65536)
    {
        sinceRecompute = 0;
        windowSum = 0.0;
        for (double v : window)
            windowSum += v;
    }
    const double ms = std::max (0.0, windowSum / windowLen);
    levelDb = ms > 1.0e-20 ? 10.0 * std::log10 (ms) : -200.0;

    // Gain computer, then attack/release on the gain in dB: attack while
    // the band moves further from 0 dB, release while it returns.
    const double targetGain = dynamicGainDb (p.dynMode, levelDb, p.dynThresh, p.dynRatio, p.dynRange);
    const double coeff = std::abs (targetGain) > std::abs (gain) ? attackCoeff : releaseCoeff;
    gain += coeff * (targetGain - gain);

    const double over = p.dynMode == DynMode::Above ? levelDb - p.dynThresh : p.dynThresh - levelDb;
    if (over > 0.0)
        ++bucketMoving;
    ++bucketSamples;
    bucketGainSum += gain;

    const int ls = learnState.load (std::memory_order_relaxed);
    if (ls == LearnRecording)
    {
        histogram.add (levelDb);
        if (histogram.count >= learnTargetSamples)
            learnState.store (LearnDone, std::memory_order_release);
    }

    return gain;
}

void Detector::closeBucket() noexcept
{
    movingBuckets[(size_t) bucketIndex] = bucketMoving;
    sampleBuckets[(size_t) bucketIndex] = bucketSamples;
    gainBuckets[(size_t) bucketIndex] = bucketGainSum;
    bucketIndex = (bucketIndex + 1) % bucketsPer10s;
    bucketMoving = bucketSamples = 0;
    bucketGainSum = 0.0;
}

double Detector::activityPercent() const noexcept
{
    long long moving = 0, total = 0;
    for (int i = 0; i < bucketsPer10s; ++i)
    {
        moving += movingBuckets[(size_t) i];
        total += sampleBuckets[(size_t) i];
    }
    return total > 0 ? 100.0 * (double) moving / (double) total : 0.0;
}

double Detector::averageGainDb() const noexcept
{
    double sum = 0.0;
    long long total = 0;
    for (int k = 1; k <= 30; ++k)
    {
        const int i = (bucketIndex - k + bucketsPer10s) % bucketsPer10s;
        sum += gainBuckets[(size_t) i];
        total += sampleBuckets[(size_t) i];
    }
    return total > 0 ? sum / (double) total : gain;
}
} // namespace lens::dsp
