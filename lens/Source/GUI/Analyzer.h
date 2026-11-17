#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <mutex>
#include <vector>

#include "DSP/Fifo.h"
#include "Match/EqMatch.h"

// Spectrum analyzer and peak finder (SPEC §6). The math is headless; the
// AnalyzerThread drains the engine's FIFOs on its own thread.
namespace lens::gui
{
struct PeakMarker
{
    double freq = 0.0;
    double prominenceDb = 0.0;
    double q = 4.0;
    double levelDb = -100.0;
};

class SpectrumAnalyzer
{
public:
    enum Trace
    {
        Pre = 0,
        Post,
        Sidechain,
        NumTraces
    };

    static constexpr int numDisplayPoints = 640;
    static constexpr double minDisplayDb = -90.0, maxDisplayDb = 30.0;

    void prepare (double sampleRate);

    // 0 raw, 1 = 1/12, 2 = 1/6, 3 = 1/3 octave.
    void setSmoothing (int index);
    // 0 slow, 1 medium, 2 fast, 3 very fast.
    void setSpeed (int index) noexcept { speed = juce::jlimit (0, 3, index); }
    void setTilt (double dbPerOct) noexcept { tilt = dbPerOct; }
    void setFrozen (bool f) noexcept { frozen = f; }
    bool isFrozen() const noexcept { return frozen; }

    // Analyzer thread.
    void feed (Trace, const float* samples, int n);

    struct Snapshot
    {
        std::vector<float> freqs;
        std::array<std::vector<float>, NumTraces> db;
        std::array<double, NumTraces> lastFrameTime {};
        std::vector<PeakMarker> peaks;
        int version = 0;
    };
    // Any thread. Copies the display data.
    void getSnapshot (Snapshot&) const;
    int version() const noexcept { return snapshotVersion.load(); }

    // Display level (dB, tilted, smoothed) at a frequency.
    double displayLevelAt (Trace, double freq) const;

    // Headless helpers: averaged level per FFT bin (window-corrected sine
    // peak level) and an interpolated peak search.
    double binLevelDb (Trace, int bin) const;
    double findPeak (Trace, double loHz, double hiHz, double& peakHz) const;
    std::vector<PeakMarker> peaks() const;

    // EQ Match capture of one trace's long-term spectrum (any thread).
    void startCapture (Trace);
    void stopCapture();
    bool isCapturing() const noexcept { return captureTrace.load() >= 0; }
    double captureSeconds() const;
    match::Spectrum captureResult (const juce::String& name) const;

    int fftSize() const noexcept { return windowSize; }
    double binHz() const noexcept { return fs / (2.0 * windowSize); }
    double secondsFed (Trace t) const noexcept { return (double) fedSamples[(size_t) t] / fs; }

private:
    void computeFrame (Trace);
    void rebuildDisplay (Trace);
    void updatePeaks();
    void buildBinRanges();
    double powerAt (const std::vector<double>& p, double bin) const;

    double fs = 48000.0;
    int windowSize = 8192, fftLen = 16384, hop = 2048;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftBuffer;

    struct TraceState
    {
        std::vector<float> ring;
        int writePos = 0;
        int sinceHop = 0;
        long long frames = 0;
        std::vector<double> avg;   // speed-averaged power per bin
        std::vector<double> slow;  // 0.5 s average for the peak finder
    };
    std::array<TraceState, NumTraces> traces;
    std::array<long long, NumTraces> fedSamples {};

    int smoothing = 2, speed = 1;
    double tilt = 4.5;
    bool frozen = false;

    std::vector<float> displayFreqs;
    std::vector<std::pair<double, double>> binRanges; // per display point (fractional bins)

    // Peak finder.
    struct Track
    {
        double freq, firstSeen, lastSeen, prominence, q, level;
    };
    std::vector<Track> tracks;
    std::vector<double> grid, gridDb, gridMed;

    mutable std::mutex lock; // analyzer thread ↔ GUI only, never audio
    mutable std::mutex captureLock;
    std::atomic<int> captureTrace { -1 };
    match::LtasAccumulator capture;
    Snapshot published;
    std::atomic<int> snapshotVersion { 0 };
};

// Drains the engine FIFOs into a SpectrumAnalyzer on its own thread.
class AnalyzerThread : private juce::Thread
{
public:
    AnalyzerThread (dsp::SpscQueue<float>& pre, dsp::SpscQueue<float>& post, dsp::SpscQueue<float>& sidechain);
    ~AnalyzerThread() override;

    void start (double sampleRate);
    void stop();
    SpectrumAnalyzer& analyzer() noexcept { return spectrum; }

    std::atomic<int> smoothing { 2 }, speed { 1 };
    std::atomic<float> tilt { 4.5f };
    std::atomic<bool> frozen { false };

private:
    void run() override;

    std::array<dsp::SpscQueue<float>*, SpectrumAnalyzer::NumTraces> fifos;
    SpectrumAnalyzer spectrum;
    std::vector<float> scratch;
};
} // namespace lens::gui
