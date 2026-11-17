#include "Analyzer.h"

#include <algorithm>
#include <cmath>

namespace lens::gui
{
namespace
{
int rateScale (double fs) noexcept { return fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4); }

double speedSeconds (int s) noexcept
{
    constexpr double t[] = { 1.5, 0.6, 0.25, 0.08 };
    return t[juce::jlimit (0, 3, s)];
}

double smoothingOctaves (int s) noexcept
{
    constexpr double o[] = { 0.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0 };
    return o[juce::jlimit (0, 3, s)];
}

double toDb (double power) noexcept { return power > 1.0e-30 ? 10.0 * std::log10 (power) : -300.0; }
} // namespace

void SpectrumAnalyzer::prepare (double sampleRate)
{
    fs = sampleRate;
    windowSize = 8192 * rateScale (fs);
    fftLen = 2 * windowSize; // 2x zero padding
    hop = windowSize / 4;    // 75% overlap
    fft = std::make_unique<juce::dsp::FFT> ((int) std::lround (std::log2 (fftLen)));
    window.resize ((size_t) windowSize);
    for (int i = 0; i < windowSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / windowSize));
    fftBuffer.assign ((size_t) (2 * fftLen), 0.0f);

    for (auto& t : traces)
    {
        t.ring.assign ((size_t) windowSize, 0.0f);
        t.writePos = t.sinceHop = 0;
        t.frames = 0;
        t.avg.assign ((size_t) (fftLen / 2 + 1), 0.0);
        t.slow.assign ((size_t) (fftLen / 2 + 1), 0.0);
    }
    fedSamples.fill (0);

    const double top = std::min (30000.0, 0.5 * fs);
    displayFreqs.resize (numDisplayPoints);
    for (int i = 0; i < numDisplayPoints; ++i)
        displayFreqs[(size_t) i] = (float) (10.0 * std::pow (top / 10.0, (double) i / (numDisplayPoints - 1)));

    // Peak finder grid: 48 points per octave, 25 Hz to 16 kHz.
    grid.clear();
    for (double f = 25.0; f <= std::min (16000.0, 0.45 * fs); f *= std::pow (2.0, 1.0 / 48.0))
        grid.push_back (f);
    gridDb.assign (grid.size(), -300.0);
    gridMed.assign (grid.size(), -300.0);
    tracks.clear();

    buildBinRanges();

    const std::lock_guard<std::mutex> g (lock);
    published = {};
    published.freqs = displayFreqs;
    for (auto& d : published.db)
        d.assign (numDisplayPoints, (float) minDisplayDb - 10.0f);
    snapshotVersion++;
}

void SpectrumAnalyzer::setSmoothing (int index)
{
    index = juce::jlimit (0, 3, index);
    if (index == smoothing)
        return;
    smoothing = index;
    buildBinRanges();
}

void SpectrumAnalyzer::buildBinRanges()
{
    binRanges.resize ((size_t) numDisplayPoints);
    const double spacing = std::log2 (displayFreqs.back() / displayFreqs.front()) / (numDisplayPoints - 1);
    const double oct = std::max (smoothingOctaves (smoothing), spacing);
    const double bw = binHz();
    for (int i = 0; i < numDisplayPoints; ++i)
    {
        const double f = displayFreqs[(size_t) i];
        binRanges[(size_t) i] = { f * std::pow (2.0, -0.5 * oct) / bw, f * std::pow (2.0, 0.5 * oct) / bw };
    }
}

void SpectrumAnalyzer::feed (Trace t, const float* x, int n)
{
    auto& ts = traces[(size_t) t];
    if (ts.ring.empty())
        return;
    for (int i = 0; i < n; ++i)
    {
        ts.ring[(size_t) ts.writePos] = x[i];
        ts.writePos = (ts.writePos + 1) % windowSize;
        if (++ts.sinceHop >= hop)
        {
            ts.sinceHop = 0;
            computeFrame (t);
        }
    }
    fedSamples[(size_t) t] += n;
}

void SpectrumAnalyzer::computeFrame (Trace t)
{
    auto& ts = traces[(size_t) t];
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
    for (int i = 0; i < windowSize; ++i)
        fftBuffer[(size_t) i] = ts.ring[(size_t) ((ts.writePos + i) % windowSize)] * window[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    // Window-corrected so a sine of amplitude a reads a (Hann coherent
    // gain 0.5).
    const double scale = 2.0 / (windowSize * 0.5);
    const double frameSeconds = (double) hop / fs;
    ++ts.frames;
    const double a = std::max (1.0 - std::exp (-frameSeconds / speedSeconds (speed)), 1.0 / (double) ts.frames);
    const double aSlow = std::max (1.0 - std::exp (-frameSeconds / 0.5), 1.0 / (double) ts.frames);

    for (size_t k = 0; k < ts.avg.size(); ++k)
    {
        const double m = fftBuffer[k] * scale;
        const double p = m * m;
        ts.avg[k] += a * (p - ts.avg[k]);
        ts.slow[k] += aSlow * (p - ts.slow[k]);
    }

    if (t == Pre)
        updatePeaks();
    if (! frozen)
        rebuildDisplay (t);
}

double SpectrumAnalyzer::powerAt (const std::vector<double>& p, double bin) const
{
    bin = juce::jlimit (0.0, (double) p.size() - 1.0, bin);
    const int i = (int) bin;
    const double frac = bin - i;
    if (i + 1 >= (int) p.size())
        return p[(size_t) i];
    return p[(size_t) i] * (1.0 - frac) + p[(size_t) i + 1] * frac;
}

void SpectrumAnalyzer::rebuildDisplay (Trace t)
{
    const auto& p = traces[(size_t) t].avg;
    std::vector<float> out ((size_t) numDisplayPoints);
    for (int i = 0; i < numDisplayPoints; ++i)
    {
        const auto [lo, hi] = binRanges[(size_t) i];
        double power;
        if (hi - lo < 1.0)
        {
            power = powerAt (p, 0.5 * (lo + hi));
        }
        else if (smoothing == 0)
        {
            power = 0.0; // raw: the strongest bin in this pixel, so peaks show
            for (int k = (int) std::ceil (lo); k <= (int) std::floor (hi) && k < (int) p.size(); ++k)
                power = std::max (power, p[(size_t) k]);
        }
        else
        {
            double sum = 0.0;
            int count = 0;
            for (int k = (int) std::ceil (lo); k <= (int) std::floor (hi) && k < (int) p.size(); ++k)
            {
                sum += p[(size_t) k];
                ++count;
            }
            power = count > 0 ? sum / count : powerAt (p, 0.5 * (lo + hi));
        }
        const double f = displayFreqs[(size_t) i];
        out[(size_t) i] = (float) (toDb (power) + tilt * std::log2 (f / 1000.0));
    }

    const std::lock_guard<std::mutex> g (lock);
    published.db[(size_t) t] = std::move (out);
    published.lastFrameTime[(size_t) t] = secondsFed (t);
    snapshotVersion++;
    published.version = snapshotVersion.load();
}

void SpectrumAnalyzer::updatePeaks()
{
    const auto& p = traces[Pre].slow;
    const double bw = binHz();
    const double now = secondsFed (Pre);
    const size_t n = grid.size();

    // 1/12-octave smoothed level on the grid.
    for (size_t i = 0; i < n; ++i)
    {
        const double lo = grid[i] * std::pow (2.0, -1.0 / 24.0) / bw;
        const double hi = grid[i] * std::pow (2.0, 1.0 / 24.0) / bw;
        double sum = 0.0;
        int count = 0;
        for (int k = (int) std::ceil (lo); k <= (int) std::floor (hi); ++k)
        {
            sum += p[(size_t) k];
            ++count;
        }
        gridDb[i] = toDb (count > 0 ? sum / count : powerAt (p, 0.5 * (lo + hi)));
    }
    // 1-octave running median (robust against the peak itself).
    std::vector<double> win;
    win.reserve (64);
    for (size_t i = 0; i < n; ++i)
    {
        win.clear();
        for (size_t j = (i >= 24 ? i - 24 : 0); j <= std::min (n - 1, i + 24); ++j)
            win.push_back (gridDb[j]);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        gridMed[i] = win[win.size() / 2];
    }

    // Candidates: local maxima at least 6 dB above the smoothed spectrum.
    std::vector<PeakMarker> found;
    for (size_t i = 2; i + 2 < n; ++i)
    {
        const double v = gridDb[i];
        const double prom = v - gridMed[i];
        if (prom < 6.0 || v < -110.0)
            continue;
        if (! (v >= gridDb[i - 1] && v >= gridDb[i + 1] && v > gridDb[i - 2] && v > gridDb[i + 2]))
            continue;
        // Parabolic refinement of the frequency on the log grid.
        const double ym = gridDb[i - 1], y0 = v, yp = gridDb[i + 1];
        const double den = ym - 2.0 * y0 + yp;
        const double off = std::abs (den) > 1.0e-12 ? 0.5 * (ym - yp) / den : 0.0;
        const double f = grid[i] * std::pow (2.0, juce::jlimit (-0.5, 0.5, off) / 48.0);
        // Width at 3 dB below the peak → Q.
        size_t l = i, r = i;
        while (l > 0 && gridDb[l] > v - 3.0)
            --l;
        while (r + 1 < n && gridDb[r] > v - 3.0)
            ++r;
        const double width = grid[r] - grid[l];
        const double q = juce::jlimit (2.0, 10.0, width > 0.0 ? f / width : 10.0);
        found.push_back ({ f, prom, q, v });
    }

    for (const auto& c : found)
    {
        bool matched = false;
        for (auto& t : tracks)
            if (std::abs (std::log2 (c.freq / t.freq)) < 1.0 / 12.0)
            {
                t.freq = c.freq;
                t.lastSeen = now;
                t.prominence = c.prominenceDb;
                t.q = c.q;
                t.level = c.levelDb;
                matched = true;
                break;
            }
        if (! matched)
            tracks.push_back ({ c.freq, now, now, c.prominenceDb, c.q, c.levelDb });
    }
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [now] (const Track& t) { return now - t.lastSeen > 0.5; }), tracks.end());

    std::vector<PeakMarker> markers;
    for (const auto& t : tracks)
        if (t.lastSeen >= now - 1.0e-9 && t.lastSeen - t.firstSeen >= 2.0)
            markers.push_back ({ t.freq, t.prominence, t.q, t.level });
    std::sort (markers.begin(), markers.end(), [] (const PeakMarker& a, const PeakMarker& b) { return a.prominenceDb > b.prominenceDb; });
    if (markers.size() > 5)
        markers.resize (5);

    const std::lock_guard<std::mutex> g (lock);
    published.peaks = std::move (markers);
}

void SpectrumAnalyzer::getSnapshot (Snapshot& s) const
{
    const std::lock_guard<std::mutex> g (lock);
    s = published;
    s.version = snapshotVersion.load();
}

double SpectrumAnalyzer::displayLevelAt (Trace t, double freq) const
{
    const std::lock_guard<std::mutex> g (lock);
    const auto& f = published.freqs;
    const auto& d = published.db[(size_t) t];
    if (f.empty() || d.size() != f.size())
        return minDisplayDb;
    const auto it = std::lower_bound (f.begin(), f.end(), (float) freq);
    if (it == f.begin())
        return d.front();
    if (it == f.end())
        return d.back();
    const size_t i = (size_t) (it - f.begin());
    const double x = std::log (freq / f[i - 1]) / std::log ((double) f[i] / f[i - 1]);
    return d[i - 1] + (d[i] - d[i - 1]) * x;
}

double SpectrumAnalyzer::binLevelDb (Trace t, int bin) const
{
    return toDb (traces[(size_t) t].avg[(size_t) juce::jlimit (0, (int) traces[(size_t) t].avg.size() - 1, bin)]);
}

double SpectrumAnalyzer::findPeak (Trace t, double loHz, double hiHz, double& peakHz) const
{
    const auto& p = traces[(size_t) t].avg;
    const double bw = binHz();
    int best = std::max (1, (int) (loHz / bw));
    for (int k = best; k <= (int) (hiHz / bw) && k + 1 < (int) p.size(); ++k)
        if (p[(size_t) k] > p[(size_t) best])
            best = k;
    // Parabolic interpolation on log magnitude (Hann: near-Gaussian peak).
    const double ym = toDb (p[(size_t) best - 1]), y0 = toDb (p[(size_t) best]), yp = toDb (p[(size_t) best + 1]);
    const double den = ym - 2.0 * y0 + yp;
    const double off = std::abs (den) > 1.0e-12 ? 0.5 * (ym - yp) / den : 0.0;
    peakHz = (best + off) * bw;
    return y0 - 0.25 * (ym - yp) * off;
}

std::vector<PeakMarker> SpectrumAnalyzer::peaks() const
{
    const std::lock_guard<std::mutex> g (lock);
    return published.peaks;
}

// ---------------------------------------------------------------- thread

AnalyzerThread::AnalyzerThread (dsp::SpscQueue<float>& pre, dsp::SpscQueue<float>& post, dsp::SpscQueue<float>& sidechain)
    : juce::Thread ("LENS analyzer"), fifos { &pre, &post, &sidechain }
{
    scratch.resize (8192);
}

AnalyzerThread::~AnalyzerThread()
{
    stop();
}

void AnalyzerThread::start (double sampleRate)
{
    stop();
    spectrum.prepare (sampleRate);
    // Drop whatever piled up while nobody was listening.
    for (auto* f : fifos)
        while (f->pop (scratch.data(), (int) scratch.size()) > 0)
        {
        }
    startThread (juce::Thread::Priority::low);
}

void AnalyzerThread::stop()
{
    stopThread (2000);
}

void AnalyzerThread::run()
{
    while (! threadShouldExit())
    {
        spectrum.setSmoothing (smoothing.load());
        spectrum.setSpeed (speed.load());
        spectrum.setTilt (tilt.load());
        spectrum.setFrozen (frozen.load());

        bool any = false;
        for (int t = 0; t < SpectrumAnalyzer::NumTraces; ++t)
        {
            int got;
            while ((got = fifos[(size_t) t]->pop (scratch.data(), (int) scratch.size())) > 0)
            {
                spectrum.feed ((SpectrumAnalyzer::Trace) t, scratch.data(), got);
                any = true;
            }
        }
        wait (any ? 5 : 20);
    }
}
} // namespace lens::gui
