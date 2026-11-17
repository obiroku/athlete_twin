#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "Fifo.h"
#include "Types.h"

// Linear Phase mode (SPEC §3).
//
// Static bands: the combined zero-phase response of every band, per FFT
// bin, as a 2×2 matrix (L/R in → L/R out). It becomes up to four
// symmetric FIR kernels: LL and RR always, LR and RL only when Mid/Side
// bands make the channels mix.
//
// Dynamic bands: band d with static gain Gs and dynamic gain Gd(t) is
// modelled as 1 + (Gs·Gd − 1)·S_d, so the output is
//     static(x) + Gs·(Gd(t) − 1)·P_d(x),  P_d = A_after · S_d · A_before,
// with P_d as its own kernel and Gd delayed to line up with the kernel.
//
// Convolution: uniformly partitioned overlap-save with one shared
// frequency-domain input history, so every kernel (static and dynamic)
// reuses one forward FFT per channel, and a new kernel takes over with
// full history and a short crossfade. Latency = taps/2 + partition size.
//
// Kernels are built on a background thread (debounced 30 ms) and handed
// to the audio thread through a lock-free pool.
namespace lens::dsp
{
class LinearPhaseEngine
{
public:
    static constexpr int maxDynamicSlots = 8;
    static constexpr int numPaths = 1 + maxDynamicSlots;

    LinearPhaseEngine();
    ~LinearPhaseEngine();

    static int partitionSize (int taps) noexcept;
    static int latencyFor (int taps) noexcept { return taps / 2 + partitionSize (taps); }

    // Allocates.
    void prepare (double sampleRate, int maxBlock, int numChannels);
    // Builds and activates kernels synchronously (not on the audio thread).
    void prepareKernels (const EngineParams&);
    void reset() noexcept;
    void setResolution (LpResolution) noexcept;
    int taps() const noexcept { return currentTaps; }
    int latency() const noexcept { return latencyFor (currentTaps); }

    // Audio thread.
    // offline = the host renders faster than real time: build now, on
    // this thread, so the result does not depend on timing.
    void pushParams (const EngineParams&, bool offline) noexcept;
    void convolve (const double* L, const double* R, int n) noexcept;
    void combine (double* wl, double* wr, int off, int n, const std::vector<double>* dynGain, const EngineParams&) noexcept;
    void setLookahead (int samples) noexcept { lookahead = samples; }
    bool ready() const noexcept;

    // Kernel design, exposed for tests.
    struct Matrix
    {
        double ll = 1, lr = 0, rl = 0, rr = 1;
    };
    static std::vector<float> kernelFromMagnitude (const std::vector<double>& mag, int taps);

private:
    struct KernelSet;
    class Builder;

    void buildInto (KernelSet&, const EngineParams&);
    void computeBlock() noexcept;
    void convolvePath (const KernelSet&, int path, int ch, float* out) noexcept;
    static std::array<int, maxDynamicSlots> slotBands (const EngineParams&, int& count);

    double fs = 48000.0;
    int maxBlock = 512;
    int channels = 2;
    int currentTaps = 16384;
    int lookahead = 0;

    // Partitioned convolution state (sized for the largest kernel).
    int B = 512, P = 32, maxB = 512, maxP = 32;
    // One FFT per resolution, created in prepare(): switching never
    // allocates on the audio thread.
    std::array<std::unique_ptr<juce::dsp::FFT>, 3> ffts;
    juce::dsp::FFT* fft = nullptr;
    std::array<std::vector<float>, 2> inBlock, prevBlock;
    std::array<std::vector<float>, 2> fdlRe, fdlIm; // [P][B+1] per channel
    int head = 0, fill = 0;
    std::vector<float> work, accRe, accIm;
    // Output blocks per path and channel, for the active and fading sets.
    std::array<std::array<std::vector<float>, 2>, numPaths> outCur, outOld;
    int fadeBlocksLeft = 0, fadeBlocks = 1;
    double fadePos = 1.0, fadeStep = 1.0;

    // Per-piece outputs.
    juce::AudioBuffer<float> staticOut;
    std::array<juce::AudioBuffer<float>, maxDynamicSlots> slotOut;

    // Untouched channels run through an exact delay (bit-identical).
    std::array<double, 2> identityMix {};
    double identityStep = 0.002;
    std::array<std::vector<float>, 2> delayLine;
    int delayLen = 1, delayPos = 0;

    std::array<std::vector<float>, maxDynamicSlots> gainDelay;
    std::array<int, maxDynamicSlots> gainPos {};

    // Kernel hand-over pool.
    static constexpr int poolSize = 4;
    std::array<std::unique_ptr<KernelSet>, poolSize> pool;
    std::array<std::atomic<int>, poolSize> poolState {};
    std::atomic<int> pending { -1 };
    int active = -1, fading = -1;

    SpscQueue<EngineParams> paramQueue { 8 };
    juce::uint64 lastHash = 0;
    std::unique_ptr<Builder> builder;
};
} // namespace lens::dsp
