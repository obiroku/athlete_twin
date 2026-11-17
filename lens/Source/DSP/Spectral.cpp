#include "Spectral.h"

namespace lens::dsp
{
void SpectralProcessor::prepare (double sampleRate, int, int numChannels)
{
    fs = sampleRate;
    channels = numChannels > 1 ? 2 : 1;
    const int scale = fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
    N = 2048 * scale;
    hop = N / 4;
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (N)));
    window.resize ((size_t) N);
    for (int i = 0; i < N; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / N));
    for (auto& r : inRing)
        r.assign ((size_t) (2 * N), 0.0f);
    for (auto& r : olaRing)
        r.assign ((size_t) (2 * N), 0.0f);
    for (auto& s : spec)
        s.assign ((size_t) (2 * N), 0.0f);
    work.assign ((size_t) (2 * N), 0.0f);
    power.assign ((size_t) (N / 2 + 1), 0.0f);
    gainDb.assign ((size_t) (N / 2 + 1), 0.0f);
    cutDb.assign ((size_t) (N / 2 + 1), 0.0f);
    for (auto& s : smoothCut)
        s.assign ((size_t) (N / 2 + 1), 0.0f);
    published = std::make_unique<std::atomic<float>[]> ((size_t) (N / 2 + 1));
    numPublished = N / 2 + 1;
    reset();
}

float SpectralProcessor::publishedCutDb (double freq) const noexcept
{
    if (published == nullptr)
        return 0.0f;
    const int b = juce::roundToInt (freq / binHz());
    return b >= 0 && b < numPublished ? published[(size_t) b].load (std::memory_order_relaxed) : 0.0f;
}

void SpectralProcessor::reset() noexcept
{
    for (auto& r : inRing)
        std::fill (r.begin(), r.end(), 0.0f);
    for (auto& r : olaRing)
        std::fill (r.begin(), r.end(), 0.0f);
    for (auto& s : smoothCut)
        std::fill (s.begin(), s.end(), 0.0f);
    for (int b = 0; b < numPublished; ++b)
        published[(size_t) b].store (0.0f);
    pos = 0;
    sinceHop = 0;
}

void SpectralProcessor::process (double* L, double* R, int n, const EngineParams& p) noexcept
{
    const int ring = 2 * N;
    for (int i = 0; i < n; ++i)
    {
        double* io[2] = { L + i, R + i };
        for (int c = 0; c < channels; ++c)
            inRing[(size_t) c][(size_t) pos] = (float) *io[c];

        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            frame (p); // covers input [pos − N + 1, pos]
        }

        // Output: input delayed by N plus the finished change at that sample.
        int rd = pos - N;
        if (rd < 0)
            rd += ring;
        for (int c = 0; c < channels; ++c)
        {
            auto& ola = olaRing[(size_t) c];
            *io[c] = (double) inRing[(size_t) c][(size_t) rd] + (double) ola[(size_t) rd];
            ola[(size_t) rd] = 0.0f;
        }
        if (channels < 2)
            *io[1] = *io[0];
        pos = pos + 1 == ring ? 0 : pos + 1;
    }
}

void SpectralProcessor::frame (const EngineParams& p) noexcept
{
    const int ring = 2 * N;
    const int bins = N / 2 + 1;
    const int start = pos - N + 1;

    // Analysis.
    std::fill (power.begin(), power.end(), 0.0f);
    for (int c = 0; c < channels; ++c)
    {
        auto& s = spec[(size_t) c];
        std::fill (s.begin(), s.end(), 0.0f);
        for (int k = 0; k < N; ++k)
        {
            int idx = start + k;
            if (idx < 0)
                idx += ring;
            s[(size_t) k] = inRing[(size_t) c][(size_t) idx] * window[(size_t) k];
        }
        fft->performRealOnlyForwardTransform (s.data(), true);
        for (int b = 0; b < bins; ++b)
            power[(size_t) b] += (s[(size_t) (2 * b)] * s[(size_t) (2 * b)] + s[(size_t) (2 * b + 1)] * s[(size_t) (2 * b + 1)]) / (float) channels;
    }

    // Gains: every spectral band works inside its −3 dB span.
    std::fill (gainDb.begin(), gainDb.end(), 0.0f);
    const double hopSec = (double) hop / fs;
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto& bp = p.bands[(size_t) band];
        auto& sm = smoothCut[(size_t) band];
        if (! bp.isSpectral())
        {
            std::fill (sm.begin(), sm.end(), 0.0f);
            continue;
        }
        const double q = std::max (0.1, (double) bp.q);
        const double half = 1.0 / (2.0 * q);
        const double root = std::sqrt (1.0 + half * half);
        const int lo = std::max (1, (int) std::ceil (bp.freq * (root - half) / binHz()));
        const int hi = std::min (bins - 1, (int) std::floor (bp.freq * (root + half) / binHz()));
        if (hi < lo)
            continue;

        // The band's smoothed level: mean of the bins' levels in dB.
        double mean = 0.0;
        for (int b = lo; b <= hi; ++b)
            mean += 10.0 * std::log10 (power[(size_t) b] + 1.0e-20);
        mean /= (hi - lo + 1);

        const double att = 1.0 - std::exp (-hopSec / std::max (0.0005, bp.dynAttack * 0.001));
        const double rel = 1.0 - std::exp (-hopSec / std::max (0.005, bp.dynRelease * 0.001));
        for (int b = lo; b <= hi; ++b)
        {
            const double excess = 10.0 * std::log10 (power[(size_t) b] + 1.0e-20) - mean;
            const double target = excess > bp.specThresh ? std::min ((double) bp.specAmount, excess - bp.specThresh) : 0.0;
            auto& v = sm[(size_t) b];
            v += (float) ((target > v ? att : rel) * (target - v));
            gainDb[(size_t) b] -= v;
        }
        // Bins that left the span relax to 0.
        for (int b = 1; b < lo; ++b)
            sm[(size_t) b] = 0.0f;
        for (int b = hi + 1; b < bins; ++b)
            sm[(size_t) b] = 0.0f;
    }
    for (int b = 0; b < bins; ++b)
    {
        cutDb[(size_t) b] = -gainDb[(size_t) b];
        published[(size_t) b].store (cutDb[(size_t) b], std::memory_order_relaxed);
    }

    // Synthesis of the change only: (G − 1)·X, Hann again, overlap-add.
    const float ola = 2.0f / 3.0f; // Hann² at 75% overlap sums to 1.5
    for (int c = 0; c < channels; ++c)
    {
        auto& s = spec[(size_t) c];
        bool any = false;
        for (int b = 0; b < bins; ++b)
        {
            const float g = gainDb[(size_t) b] < -1.0e-6f ? (float) (std::pow (10.0, gainDb[(size_t) b] / 20.0) - 1.0) : 0.0f;
            any = any || g != 0.0f;
            work[(size_t) (2 * b)] = s[(size_t) (2 * b)] * g;
            work[(size_t) (2 * b + 1)] = s[(size_t) (2 * b + 1)] * g;
        }
        if (! any)
            continue;
        for (int b = 1; b < N / 2; ++b)
        {
            work[(size_t) (2 * (N - b))] = work[(size_t) (2 * b)];
            work[(size_t) (2 * (N - b) + 1)] = -work[(size_t) (2 * b + 1)];
        }
        fft->performRealOnlyInverseTransform (work.data());
        auto& o = olaRing[(size_t) c];
        for (int k = 0; k < N; ++k)
        {
            int idx = start + k;
            if (idx < 0)
                idx += ring;
            o[(size_t) idx] += work[(size_t) k] * window[(size_t) k] * ola;
        }
    }
}
} // namespace lens::dsp
