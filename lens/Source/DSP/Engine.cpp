#include "Engine.h"

#include <algorithm>
#include <cmath>

#include "LinearPhase.h"
#include "Spectral.h"
#include "Warm.h"

namespace lens::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;

float powerToDb (double p) noexcept { return p > 1.0e-20 ? (float) (10.0 * std::log10 (p)) : -200.0f; }

void fetchMax (std::atomic<float>& a, float v) noexcept
{
    float prev = a.load (std::memory_order_relaxed);
    while (v > prev && ! a.compare_exchange_weak (prev, v, std::memory_order_relaxed))
    {
    }
}

int rateScale (double fs) noexcept
{
    return fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
}
} // namespace

Engine::Engine()
    : lp (std::make_unique<LinearPhaseEngine>()),
      spec (std::make_unique<SpectralProcessor>()),
      warm (std::make_unique<WarmStage>())
{
}

Engine::~Engine() = default;

int Engine::linearPhaseTaps (LpResolution r, double sampleRate) noexcept
{
    const int base = r == LpResolution::Low ? 4096 : (r == LpResolution::High ? 65536 : 16384);
    return base * rateScale (sampleRate);
}

int Engine::spectralFrameSize() const noexcept
{
    return 2048 * rateScale (fs);
}

void Engine::prepare (double sampleRate, int maxBlockSize, int numChannels, int numSidechainChannels)
{
    (void) numSidechainChannels;
    fs = sampleRate;
    channels = numChannels > 1 ? 2 : 1;
    maxBlock = std::max (1, maxBlockSize);
    fadeLen10 = (int) std::lround (0.010 * fs);
    fadeLen20 = (int) std::lround (0.020 * fs);
    autoGainCoeff = onePoleCoeff (0.1, fs); // ~300 ms to settle
    bucketLen = (int) std::lround (0.1 * fs);
    bucketPos = 0;
    counter = 0;

    for (auto& b : bands)
        b.prepare (fs, channels);
    for (auto& d : detectors)
        d.prepare (fs);
    earBand.prepare (fs, channels);
    autoGain.prepare (fs);

    for (auto* v : { &inL, &inR, &scL, &scR, &wetL, &wetR, &listenL, &listenR })
        v->assign ((size_t) maxBlock, 0.0);
    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    for (auto& g : dynGain)
        g.assign ((size_t) maxBlock, 0.0);

    lp->prepare (fs, maxBlock, channels);
    spec->prepare (fs, maxBlock, channels);
    warm->prepare (fs, maxBlock, channels);

    const int maxLookahead = (int) std::lround (0.010 * fs);
    const int maxLatency = linearPhaseTaps (LpResolution::High, fs) / 2 + spectralFrameSize() + 64 + maxLookahead;
    for (auto& d : lookaheadDelay)
        d.prepare (maxLookahead + 1);
    for (auto& d : dryDelay)
        d.prepare (maxLatency);

    inLoudness.prepare (fs, channels);
    outLoudness.prepare (fs, channels);
    soloFilter.reset();

    latState = LatRunning;
    latencyGain.snap (1.0);
    swapState = SwapNone;
    swapMix.snap (0.0);
    snapNext = true;
    prepared = true;

    // Settle on the current parameters right away so latency is known.
    setParams (params);
}

Engine::LatencyConfig Engine::configFor (const EngineParams& p) const noexcept
{
    LatencyConfig c;
    c.linearPhase = p.global.linearPhase;
    c.resolution = p.global.lpResolution;
    c.lookahead = (int) std::lround (std::clamp ((double) p.global.lookaheadMs, 0.0, 10.0) * 0.001 * fs);
    c.spectral = false;
    for (const auto& b : p.bands)
        c.spectral = c.spectral || b.isSpectral();
    c.warm = p.global.warm;
    return c;
}

int Engine::latencyOf (const LatencyConfig& c) const noexcept
{
    int l = c.linearPhase ? linearPhaseTaps (c.resolution, fs) / 2 : c.lookahead;
    if (c.spectral)
        l += spectralFrameSize();
    if (c.warm)
        l += warm->latency();
    return l;
}

int Engine::latencyFor (const EngineParams& p) const noexcept
{
    return latencyOf (configFor (p));
}

void Engine::applyLatencyConfig (const LatencyConfig& c) noexcept
{
    activeConfig = c;
    activeLookahead = c.linearPhase ? 0 : c.lookahead;
    activeLatency = latencyOf (c);
    for (auto& d : lookaheadDelay)
        d.clear();
    for (auto& d : dryDelay)
        d.clear();
    lp->reset();
    spec->reset();
    warm->reset();
    for (int b = 0; b < kNumBands; ++b)
        bands[(size_t) b].setParams (params.bands[(size_t) b], true);

    engineStats.latencySamples.store (activeLatency);
    engineStats.linearPhaseActive.store (c.linearPhase);
    const double tail = c.linearPhase ? (double) linearPhaseTaps (c.resolution, fs) / fs : 0.0;
    engineStats.tailSeconds.store (std::max (1.0, tail));
}

void Engine::setEarBand (bool on, float freq, float gainDb, float q) noexcept
{
    earParams.on = on;
    earParams.shape = Shape::Bell;
    earParams.freq = freq;
    earParams.gain = gainDb;
    earParams.q = q;
}

void Engine::beginSwap() noexcept
{
    if (swapState == SwapNone || swapState == SwapIn)
    {
        swapState = SwapOut;
        swapMix.setTarget (1.0, fadeLen10);
        swapHoldSamples = 0;
    }
}

void Engine::endSwap() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
        bands[(size_t) b].setParams (params.bands[(size_t) b], true);
    for (int b = 0; b < kNumBands; ++b)
        if (dynamicBand[(size_t) b])
            detectors[(size_t) b].configure (params.bands[(size_t) b], params.bands[(size_t) b].freq, params.bands[(size_t) b].q);
    swapState = SwapIn;
    swapMix.setTarget (0.0, fadeLen10);
    engineStats.swapHolding.store (false);
    updateAutoGain();
}

void Engine::setParams (const EngineParams& p) noexcept
{
    params = p;
    if (! prepared)
        return;

    const auto want = configFor (p);

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bp = p.bands[(size_t) b];
        const bool dyn = bp.isDynamic();
        if (dyn && ! dynamicBand[(size_t) b])
            detectors[(size_t) b].reset();
        dynamicBand[(size_t) b] = dyn;
        externalBand[(size_t) b] = bp.detExternal;
        bands[(size_t) b].setParams (bp, snapNext);

        // Learn handshake with the message thread.
        auto& det = detectors[(size_t) b];
        const int ls = det.learnState.load (std::memory_order_acquire);
        if (ls == Detector::LearnRequested)
        {
            det.histogram.clear();
            det.learnTargetSamples = (long long) std::lround (8.0 * fs);
            det.learnState.store (Detector::LearnRecording, std::memory_order_release);
        }
        else if (ls == Detector::LearnStopRequested)
        {
            det.learnState.store (Detector::LearnDone, std::memory_order_release);
        }
    }
    earBand.setParams (earParams, snapNext);

    if (want.linearPhase)
        lp->pushParams (p);

    const auto& g = p.global;
    if (snapNext)
    {
        applyLatencyConfig (want);
        outputGainDb.snap (g.outputGain);
        outputGainLin = std::pow (10.0, g.outputGain / 20.0);
        deltaMix.snap (g.delta ? 1.0 : 0.0);
        bypassMix.snap (g.bypass ? 1.0 : 0.0);
        for (int b = 0; b < kNumBands; ++b)
            if (dynamicBand[(size_t) b])
                detectors[(size_t) b].configure (p.bands[(size_t) b], p.bands[(size_t) b].freq, p.bands[(size_t) b].q);
        updateAutoGain();
        autoGainLin = autoGainTargetLin;
        snapNext = false;
    }
    else
    {
        if (want != activeConfig)
        {
            if (latState == LatRunning)
            {
                latState = LatFadeOut;
                latencyGain.setTarget (0.0, fadeLen20);
            }
            wantedConfig = want;
        }
        outputGainDb.setTarget (g.outputGain, fadeLen20);
        deltaMix.setTarget (g.delta ? 1.0 : 0.0, fadeLen10);
        bypassMix.setTarget (g.bypass ? 1.0 : 0.0, fadeLen10);
    }

    // Solo and Listen (UI state).
    const int s = soloRequest;
    const bool soloValid = s >= 0 && s < kNumBands && p.bands[(size_t) s].on;
    if (soloValid)
        soloBand = s;
    soloMix.setTarget (soloValid ? 1.0 : 0.0, fadeLen10);

    const int l = listenRequest;
    const bool listenValid = l >= 0 && l < kNumBands && dynamicBand[(size_t) l];
    if (listenValid)
        listenBand = l;
    listenMix.setTarget (listenValid ? 1.0 : 0.0, fadeLen10);
}

void Engine::onTick() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        bands[(size_t) b].tick();
        if (dynamicBand[(size_t) b])
            detectors[(size_t) b].configure (params.bands[(size_t) b], bands[(size_t) b].smoothedFreq(), bands[(size_t) b].smoothedQ());
    }
    earBand.tick();

    if (soloBand >= 0)
    {
        const auto& sb = bands[(size_t) soloBand];
        const auto shape = params.bands[(size_t) soloBand].shape;
        const bool hasWidth = shape == Shape::Bell || shape == Shape::Notch || shape == Shape::BandPass || shape == Shape::AllPass;
        const double q = hasWidth ? sb.smoothedQ() : 0.7;
        const double f = clampFrequency (sb.smoothedFreq(), fs);
        soloFilter.set (bandPassSection (std::tan (pi * f / fs), std::max (0.1, q)));
    }

    if (counter % autoGainInterval == 0)
        updateAutoGain();
}

void Engine::updateAutoGain() noexcept
{
    std::array<AutoGainEstimator::BandCurve, kNumBands> curves {};
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& band = bands[(size_t) b];
        if (! band.isOn())
            continue;
        const auto& bp = params.bands[(size_t) b];
        curves[(size_t) b].mode = bp.stereo;
        if (dynamicBand[(size_t) b])
        {
            const double avg = detectors[(size_t) b].averageGainDb();
            dynDesigns[(size_t) b] = designBand (bp.shape, band.smoothedFreq(), band.smoothedGain() + avg, band.smoothedQ(), bp.slope, fs);
            curves[(size_t) b].design = &dynDesigns[(size_t) b];
        }
        else
        {
            curves[(size_t) b].design = &band.design();
        }
    }
    autoGainDb = -autoGain.loudnessChangeDb (curves.data(), kNumBands, channels);
    autoGainDb = std::clamp (autoGainDb, -30.0, 30.0);
    autoGainTargetLin = params.global.autoGain ? std::pow (10.0, autoGainDb / 20.0) : 1.0;
}

void Engine::closeBuckets() noexcept
{
    for (auto& d : detectors)
        d.closeBucket();
    const double n = (double) bucketLen;
    engineStats.inputLevelDb.store (powerToDb (inPowerAcc / n));
    engineStats.midLevelDb.store (powerToDb (midPowerAcc / n));
    engineStats.sideLevelDb.store (powerToDb (sidePowerAcc / n));
    engineStats.sidechainLevelDb.store (powerToDb (scPowerAcc / n));
    inPowerAcc = midPowerAcc = sidePowerAcc = scPowerAcc = 0.0;
    bucketPos = 0;
}

void Engine::process (float* const* io, int numCh, const float* const* sc, int numSc, int numSamples) noexcept
{
    (void) numCh;
    if (! prepared || numSamples <= 0)
        return;

    blockInPeak = blockOutPeak = 0.0f;
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - done);
        processPiece (io, sc, numSc, done, n);
        done += n;
    }
    publishStats();
}

void Engine::processPiece (float* const* io, const float* const* sc, int numSc, int offset, int len) noexcept
{
    if (latState == LatFadeOut && latencyGain.current <= 0.0 && ! latencyGain.isMoving())
    {
        applyLatencyConfig (wantedConfig);
        latState = LatFadeIn;
        latencyGain.setTarget (1.0, fadeLen20);
    }

    for (int i = 0; i < len; ++i)
    {
        inL[(size_t) i] = io[0][offset + i];
        inR[(size_t) i] = channels > 1 ? io[1][offset + i] : inL[(size_t) i];
        if (numSc > 0 && sc != nullptr)
        {
            scL[(size_t) i] = sc[0][offset + i];
            scR[(size_t) i] = numSc > 1 ? sc[1][offset + i] : scL[(size_t) i];
        }
        else
        {
            scL[(size_t) i] = scR[(size_t) i] = 0.0;
        }
    }

    // Linear Phase convolves the whole piece up front; everything else
    // runs chunk by chunk on the 8-sample tick grid, so per-tick updates
    // (auto-gain, solo filter) land on the same sample at any block size.
    if (activeConfig.linearPhase)
        lp->convolve (inL.data(), inR.data(), len);

    int pos = 0;
    while (pos < len)
    {
        const int toTick = tickInterval - (int) (counter % tickInterval);
        const int n = std::min (len - pos, toTick);
        if (counter % tickInterval == 0)
            onTick();
        frontChunk (pos, n);

        double* wl = wetL.data() + pos;
        double* wr = wetR.data() + pos;
        if (activeConfig.linearPhase)
            lp->combine (wl, wr, pos, n, dynGain.data(), params);
        if (earBand.isActive())
            earBand.process (wl, wr, n, nullptr);
        if (activeConfig.spectral)
            spec->process (wl, wr, n, params);
        outputStage (io, offset, pos, n);

        pos += n;
        counter += n;
    }
}

void Engine::frontChunk (int off, int n) noexcept
{
    const bool lpMode = activeConfig.linearPhase;
    const bool stereo = channels > 1;

    for (int i = off; i < off + n; ++i)
    {
        const double L = inL[(size_t) i], R = inR[(size_t) i];

        inLoudness.push (L, R);
        blockInPeak = std::max (blockInPeak, (float) std::max (std::abs (L), std::abs (R)));
        inPowerAcc += stereo ? 0.5 * (L * L + R * R) : L * L;
        if (stereo)
        {
            const double m = 0.5 * (L + R), s = 0.5 * (L - R);
            midPowerAcc += m * m;
            sidePowerAcc += s * s;
        }
        const double sl = scL[(size_t) i], sr = scR[(size_t) i];
        scPowerAcc += 0.5 * (sl * sl + sr * sr);

        for (int b = 0; b < kNumBands; ++b)
        {
            if (! dynamicBand[(size_t) b])
                continue;
            const bool ext = externalBand[(size_t) b];
            double lL, lR;
            const double g = detectors[(size_t) b].process (ext ? sl : L, ext ? sr : R, stereo ? 2 : 1, lL, lR);
            dynGain[(size_t) b][(size_t) i] = g;
            if (b == listenBand)
            {
                listenL[(size_t) i] = lL;
                listenR[(size_t) i] = lR;
            }
        }

        if (++bucketPos >= bucketLen)
            closeBuckets();

        if (! lpMode)
        {
            wetL[(size_t) i] = lookaheadDelay[0].process (L, activeLookahead);
            wetR[(size_t) i] = lookaheadDelay[1].process (R, activeLookahead);
        }
        dryL[(size_t) i] = dryDelay[0].process ((float) L, activeLatency);
        dryR[(size_t) i] = dryDelay[1].process ((float) R, activeLatency);
    }

    // Analyzer feeds (mono sums, as float).
    float pre[tickInterval], scm[tickInterval];
    for (int i = 0; i < n; ++i)
    {
        pre[i] = (float) (0.5 * (inL[(size_t) (off + i)] + inR[(size_t) (off + i)]));
        scm[i] = (float) (0.5 * (scL[(size_t) (off + i)] + scR[(size_t) (off + i)]));
    }
    preFifo.push (pre, n);
    sidechainFifo.push (scm, n);

    if (! lpMode)
    {
        double* wl = wetL.data() + off;
        double* wr = wetR.data() + off;
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& band = bands[(size_t) b];
            if (! band.isActive())
                continue;
            band.process (wl, wr, n, dynamicBand[(size_t) b] ? dynGain[(size_t) b].data() + off : nullptr);
        }
    }
}

void Engine::outputStage (float* const* io, int offset, int off, int n) noexcept
{
    const bool stereo = channels > 1;

    for (int i = off; i < off + n; ++i)
    {
        double wl = wetL[(size_t) i], wr = wetR[(size_t) i];

        const double sm = soloMix.next();
        if (sm > 0.0 && soloBand >= 0)
        {
            const double sl = soloFilter.tick (0, wl);
            const double sr = stereo ? soloFilter.tick (1, wr) : sl;
            wl += (sl - wl) * sm;
            wr += (sr - wr) * sm;
        }
        const double lm = listenMix.next();
        if (lm > 0.0 && listenBand >= 0)
        {
            wl += (listenL[(size_t) i] - wl) * lm;
            wr += (listenR[(size_t) i] - wr) * lm;
        }

        autoGainLin += autoGainCoeff * (autoGainTargetLin - autoGainLin);
        wl *= autoGainLin;
        wr *= autoGainLin;

        const double d = deltaMix.next();
        if (d > 0.0)
        {
            wl -= d * dryL[(size_t) i];
            wr -= d * dryR[(size_t) i];
        }

        if (outputGainDb.isMoving())
            outputGainLin = std::pow (10.0, outputGainDb.next() / 20.0);
        wetL[(size_t) i] = wl * outputGainLin;
        wetR[(size_t) i] = wr * outputGainLin;
    }

    if (activeConfig.warm)
        warm->process (wetL.data() + off, wetR.data() + off, n);

    float post[tickInterval];
    int postCount = 0;

    for (int i = off; i < off + n; ++i)
    {
        const double b = bypassMix.next();
        const double s = swapMix.next();
        if (swapState == SwapOut && ! swapMix.isMoving())
        {
            swapState = SwapHold;
            engineStats.swapHolding.store (true);
        }
        else if (swapState == SwapHold && ++swapHoldSamples > (int) (0.5 * fs))
            endSwap(); // the message thread never answered: carry on
        else if (swapState == SwapIn && ! swapMix.isMoving())
            swapState = SwapNone;

        const double wetAmount = (1.0 - b) * (1.0 - s);
        const double dl = dryL[(size_t) i], dr = dryR[(size_t) i];
        const double lg = latencyGain.next();
        if (latState == LatFadeIn && ! latencyGain.isMoving())
            latState = LatRunning;

        double yl = wetL[(size_t) i], yr = wetR[(size_t) i];
        if (wetAmount < 1.0)
        {
            yl = dl + (yl - dl) * wetAmount;
            yr = dr + (yr - dr) * wetAmount;
        }
        if (lg < 1.0)
        {
            yl *= lg;
            yr *= lg;
        }

        io[0][offset + i] = (float) yl;
        if (stereo)
            io[1][offset + i] = (float) yr;

        outLoudness.push (yl, yr);
        blockOutPeak = std::max (blockOutPeak, (float) std::max (std::abs (yl), stereo ? std::abs (yr) : 0.0));

        post[postCount++] = (float) (stereo ? 0.5 * (yl + yr) : yl);
    }
    postFifo.push (post, postCount);
}

void Engine::publishStats() noexcept
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto& s = engineStats.bands[(size_t) b];
        const auto& d = detectors[(size_t) b];
        if (dynamicBand[(size_t) b])
        {
            s.dynGainDb.store ((float) d.currentGainDb());
            s.activity.store ((float) d.activityPercent());
            s.levelDb.store ((float) d.currentLevelDb());
        }
        else
        {
            s.dynGainDb.store (0.0f);
            s.activity.store (0.0f);
        }
    }
    engineStats.inputLufs.store ((float) inLoudness.lufs());
    engineStats.outputLufs.store ((float) outLoudness.lufs());
    fetchMax (engineStats.inputPeakHold, blockInPeak);
    fetchMax (engineStats.outputPeakHold, blockOutPeak);
    engineStats.autoGainDb.store ((float) (params.global.autoGain ? autoGainDb : 0.0));
}
} // namespace lens::dsp
