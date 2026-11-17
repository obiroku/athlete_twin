#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "Svf.h"
#include "Types.h"

// The detector and gain computer of one dynamic band (SPEC §4).
namespace lens::dsp
{
// Learn histogram: 0.25 dB bins from −120 to +12 dB.
struct LearnHistogram
{
    static constexpr double minDb = -120.0;
    static constexpr double binDb = 0.25;
    static constexpr int numBins = 528;
    std::array<int, numBins> bins {};
    long long count = 0;

    void clear() noexcept
    {
        bins.fill (0);
        count = 0;
    }
    void add (double db) noexcept
    {
        int i = (int) ((db - minDb) / binDb);
        i = i < 0 ? 0 : (i >= numBins ? numBins - 1 : i);
        ++bins[(size_t) i];
        ++count;
    }
    // Level below which `fraction` of the samples fall.
    double percentile (double fraction) const noexcept;
};

// SPEC §4 gain computer: 6 dB soft knee.
double dynamicGainDb (DynMode, double levelDb, double thresholdDb, double ratio, double rangeDb) noexcept;
// The knee-shaped "over" amount (0 well below threshold).
double kneeOver (double over) noexcept;

class Detector
{
public:
    static constexpr int bucketsPer10s = 100;

    void prepare (double sampleRate);
    void reset() noexcept;

    // Called at every 8-sample tick with the band's smoothed values.
    void configure (const BandParams&, double bandFreq, double bandQ) noexcept;

    // One sample. inL/inR: the detector source (main input or side-chain).
    // Returns the smoothed dynamic gain in dB. listenL/R receive the
    // filtered detector signal.
    double process (double inL, double inR, int numChannels, double& listenL, double& listenR) noexcept;

    // Called when the engine's 100 ms grid closes a bucket.
    void closeBucket() noexcept;

    double currentGainDb() const noexcept { return gain; }
    double currentLevelDb() const noexcept { return levelDb; }
    double activityPercent() const noexcept;
    // Average dynamic gain over the last 3 s (for auto-gain).
    double averageGainDb() const noexcept;

    // Learn: recorded on the audio thread, read by the message thread
    // only after learnState reaches Done.
    enum LearnState : int
    {
        LearnIdle = 0,
        LearnRequested,
        LearnRecording,
        LearnStopRequested,
        LearnDone
    };
    std::atomic<int> learnState { LearnIdle };
    LearnHistogram histogram;
    long long learnTargetSamples = 0;

private:
    double filterTick (int ch, double x) noexcept
    {
        x = a.tick (ch, x);
        if (twoStage)
            x = b.tick (ch, x);
        return x;
    }

    double fs = 48000.0;
    BandParams p {};
    bool configured = false;
    double lastFreq = 0.0, lastQ = 0.0;
    SimpleSvf a, b;
    bool twoStage = false;

    // 2 ms RMS: running mean of squares over a boxcar window.
    std::vector<double> window;
    int windowLen = 96;
    int windowPos = 0;
    double windowSum = 0.0;
    int sinceRecompute = 0;

    double levelDb = -200.0;
    double gain = 0.0;
    double attackCoeff = 0.0, releaseCoeff = 0.0;

    // Activity and average gain in 100 ms buckets.
    int bucketMoving = 0, bucketSamples = 0;
    double bucketGainSum = 0.0;
    std::array<int, bucketsPer10s> movingBuckets {};
    std::array<int, bucketsPer10s> sampleBuckets {};
    std::array<double, bucketsPer10s> gainBuckets {};
    int bucketIndex = 0;
};
} // namespace lens::dsp
