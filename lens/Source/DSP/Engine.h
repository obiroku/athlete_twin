#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "AutoGain.h"
#include "Detector.h"
#include "EqBand.h"
#include "Fifo.h"
#include "Loudness.h"
#include "Types.h"

namespace lens::dsp
{
class LinearPhaseEngine;
class SpectralProcessor;
class WarmStage;

// Audio → GUI statistics. Written once per block by the audio thread.
struct EngineStats
{
    struct Band
    {
        std::atomic<float> dynGainDb { 0.0f };
        std::atomic<float> activity { 0.0f };
        std::atomic<float> levelDb { -200.0f };
    };
    std::array<Band, kNumBands> bands;

    std::atomic<float> inputLufs { -200.0f }, outputLufs { -200.0f };
    std::atomic<float> inputPeakHold { 0.0f }, outputPeakHold { 0.0f }; // linear, GUI resets
    std::atomic<float> inputLevelDb { -200.0f };  // 100 ms RMS
    std::atomic<float> midLevelDb { -200.0f }, sideLevelDb { -200.0f };
    std::atomic<float> sidechainLevelDb { -200.0f };
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<int> latencySamples { 0 };
    std::atomic<bool> linearPhaseActive { false };
    std::atomic<double> tailSeconds { 1.0 };
    std::atomic<bool> swapHolding { false };
};

// The whole LENS signal path, headless. One instance per plugin.
class Engine
{
public:
    Engine();
    ~Engine();

    static constexpr int tickInterval = EqBand::tickInterval;
    static constexpr int autoGainInterval = 4096;

    // Allocates. Not real-time.
    void prepare (double sampleRate, int maxBlockSize, int numChannels, int numSidechainChannels);

    // Block start: latest parameters. The first call after prepare snaps.
    void setParams (const EngineParams&) noexcept;

    // UI state (not parameters): -1 for none.
    void setSoloBand (int band) noexcept { soloRequest = band; }
    void setListenBand (int band) noexcept { listenRequest = band; }

    // Hidden ear-trainer band (SPEC §8). Never part of the state.
    void setEarBand (bool on, float freq, float gainDb, float q) noexcept;

    // A/B and preset swaps: fade to dry, hold until endSwap, then snap
    // to the new parameters and fade back in (10 + 10 ms).
    void beginSwap() noexcept;
    void endSwap() noexcept;
    bool isSwapHolding() const noexcept { return swapState == SwapHold; }

    void process (float* const* io, int numChannels, const float* const* sidechain, int numSidechainChannels, int numSamples) noexcept;

    // Latency of the current configuration, and of a parameter set.
    int latencySamples() const noexcept { return activeLatency; }
    int latencyFor (const EngineParams&) const noexcept;
    static int linearPhaseTaps (LpResolution, double sampleRate) noexcept;
    int spectralFrameSize() const noexcept;

    EngineStats& stats() noexcept { return engineStats; }
    const EngineStats& stats() const noexcept { return engineStats; }
    Detector& detector (int band) noexcept { return detectors[(size_t) band]; }
    const EqBand& band (int b) const noexcept { return bands[(size_t) b]; }
    double sampleRate() const noexcept { return fs; }
    int numChannels() const noexcept { return channels; }

    // Analyzer feeds (mono sums). Consumer: the analyzer thread.
    SpscQueue<float> preFifo { 1 << 16 }, postFifo { 1 << 16 }, sidechainFifo { 1 << 16 };

    // Background work for Linear Phase kernels. Message/worker thread.
    LinearPhaseEngine& linearPhase() noexcept { return *lp; }
    SpectralProcessor& spectral() noexcept { return *spec; }

private:
    struct LatencyConfig
    {
        bool linearPhase = false;
        LpResolution resolution = LpResolution::Medium;
        int lookahead = 0;
        bool spectral = false;
        bool warm = false;

        bool operator== (const LatencyConfig& o) const noexcept
        {
            return linearPhase == o.linearPhase && resolution == o.resolution && lookahead == o.lookahead
                   && spectral == o.spectral && warm == o.warm;
        }
        bool operator!= (const LatencyConfig& o) const noexcept { return ! (*this == o); }
    };

    LatencyConfig configFor (const EngineParams&) const noexcept;
    int latencyOf (const LatencyConfig&) const noexcept;
    void applyLatencyConfig (const LatencyConfig&) noexcept;

    void onTick() noexcept;
    void updateAutoGain() noexcept;
    void processPiece (float* const* io, const float* const* sc, int numSc, int offset, int len) noexcept;
    void frontChunk (int offset, int len) noexcept;
    void outputStage (float* const* io, int offset, int off, int n) noexcept;
    void closeBuckets() noexcept;
    void publishStats() noexcept;

    double fs = 48000.0;
    int channels = 2;
    int maxBlock = 512;
    bool prepared = false;
    bool snapNext = true;
    long long counter = 0;

    EngineParams params {};
    std::array<EqBand, kNumBands> bands;
    std::array<Detector, kNumBands> detectors;
    std::array<bool, kNumBands> dynamicBand {};
    std::array<bool, kNumBands> externalBand {};
    EqBand earBand;
    BandParams earParams {};

    AutoGainEstimator autoGain;
    std::array<BandDesign, kNumBands> dynDesigns {};
    double autoGainTargetLin = 1.0, autoGainLin = 1.0, autoGainCoeff = 0.0, autoGainDb = 0.0;

    Ramp outputGainDb;
    double outputGainLin = 1.0;
    Ramp deltaMix, bypassMix, soloMix, listenMix;
    int fadeLen10 = 480, fadeLen20 = 960;

    int soloRequest = -1, listenRequest = -1;
    int soloBand = -1, listenBand = -1;
    SimpleSvf soloFilter;

    enum SwapState
    {
        SwapNone,
        SwapOut,
        SwapHold,
        SwapIn
    };
    SwapState swapState = SwapNone;
    Ramp swapMix;
    int swapHoldSamples = 0;
    bool swapEndRequested = false;

    // Latency structure changes: 20 ms fade-out, switch, 20 ms fade-in.
    LatencyConfig activeConfig {}, wantedConfig {};
    int activeLatency = 0;
    enum LatencyState
    {
        LatRunning,
        LatFadeOut,
        LatFadeIn
    };
    LatencyState latState = LatRunning;
    Ramp latencyGain;
    int activeLookahead = 0;

    DelayLineD lookaheadDelay[2];
    DelayLine dryDelay[2];

    std::unique_ptr<LinearPhaseEngine> lp;
    std::unique_ptr<SpectralProcessor> spec;
    std::unique_ptr<WarmStage> warm;

    // Per-piece work buffers (maxBlock long).
    std::vector<double> inL, inR, scL, scR, wetL, wetR, listenL, listenR;
    std::vector<float> dryL, dryR;
    std::array<std::vector<double>, kNumBands> dynGain;

    ShortTermLoudness inLoudness, outLoudness;
    int bucketLen = 4800, bucketPos = 0;
    double inPowerAcc = 0.0, midPowerAcc = 0.0, sidePowerAcc = 0.0, scPowerAcc = 0.0;
    float blockInPeak = 0.0f, blockOutPeak = 0.0f;

    EngineStats engineStats;
};
} // namespace lens::dsp
