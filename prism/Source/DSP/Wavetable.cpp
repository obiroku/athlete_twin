#include "Wavetable.h"
#include "FFT.h"
#include <cmath>
#include <algorithm>

namespace prism::dsp
{
namespace
{
constexpr double kTarget = 0.89125093813; // -1 dBFS

struct Spectra
{
    std::vector<std::vector<cplx>> h; // per frame, harmonics 0..512
};

Spectra analyse (const std::vector<std::vector<float>>& frames)
{
    Spectra s;
    s.h.resize (frames.size());
    std::vector<float> tmp (Wavetable::N);
    for (size_t f = 0; f < frames.size(); ++f)
    {
        std::fill (tmp.begin(), tmp.end(), 0.0f);
        const auto& fr = frames[f];
        std::copy (fr.begin(), fr.begin() + std::min<size_t> (fr.size(), Wavetable::N), tmp.begin());
        auto h = realToHarmonics (tmp.data(), Wavetable::N);
        h.resize (Wavetable::MaxHarmonics + 1);
        h[0] = 0.0; // DC removed
        s.h[f] = std::move (h);
    }
    return s;
}

// Find a (fractional) rotation offset so frame `f` starts at an upward zero crossing.
double findRotation (const std::vector<cplx>& h)
{
    std::vector<double> x (Wavetable::N);
    harmonicsToReal (h, Wavetable::MaxHarmonics, x.data(), Wavetable::N);
    // already starting at an upward zero crossing (e.g. a table that went through hygiene before):
    // keep it, so hygiene is idempotent and exported tables re-import identically
    double pk = 0.0;
    for (double v : x) pk = std::max (pk, std::abs (v));
    if (std::abs (x[0]) <= 1e-6 * pk && x[1] > x[(size_t) Wavetable::N - 1] && x[1] > 0.0) return 0.0;
    double best = -1.0, bestPos = 0.0;
    for (int i = 0; i < Wavetable::N; ++i)
    {
        const double a = x[(size_t) ((i + Wavetable::N - 1) % Wavetable::N)];
        const double b = x[(size_t) i];
        if (a < 0.0 && b >= 0.0)
        {
            const double slope = b - a;
            if (slope > best)
            {
                best = slope;
                const double t = (b - a) != 0.0 ? (-a / (b - a)) : 0.0; // fraction from i-1
                bestPos = (double) (i - 1) + t;
            }
        }
    }
    if (best < 0.0) return 0.0;
    if (bestPos < 0) bestPos += Wavetable::N;
    return bestPos;
}

void rotate (std::vector<cplx>& h, double offset)
{
    // advance by offset samples: x'[n] = x[n + offset]
    for (size_t k = 1; k < h.size(); ++k)
    {
        const double ang = 2.0 * 3.14159265358979323846 * (double) k * offset / (double) Wavetable::N;
        h[k] *= cplx (std::cos (ang), std::sin (ang));
    }
}

void hygieneSpectra (Spectra& s)
{
    if (s.h.empty()) return;
    // one rotation offset for the whole table, from the first non-silent frame
    double offset = 0.0;
    for (auto& h : s.h)
    {
        double e = 0;
        for (auto& c : h) e += std::norm (c);
        if (e > 1e-12) { offset = findRotation (h); break; }
    }
    for (auto& h : s.h) rotate (h, offset);

    // one gain for all frames
    double peak = 0.0;
    std::vector<double> x (Wavetable::N);
    for (auto& h : s.h)
    {
        harmonicsToReal (h, Wavetable::MaxHarmonics, x.data(), Wavetable::N);
        for (double v : x) peak = std::max (peak, std::abs (v));
    }
    const double g = peak > 1e-12 ? kTarget / peak : 0.0;
    for (auto& h : s.h) for (auto& c : h) c *= g;
}
} // namespace

void applyTableHygiene (std::vector<std::vector<float>>& frames)
{
    auto s = analyse (frames);
    hygieneSpectra (s);
    for (size_t f = 0; f < frames.size(); ++f)
    {
        frames[f].assign (Wavetable::N, 0.0f);
        harmonicsToReal (s.h[f], Wavetable::MaxHarmonics, frames[f].data(), Wavetable::N);
    }
}

std::unique_ptr<Wavetable> Wavetable::fromFrames (const std::vector<std::vector<float>>& inFrames, const std::string& nm)
{
    std::unique_ptr<Wavetable> t (new Wavetable());
    t->name = nm;
    std::vector<std::vector<float>> frames (inFrames.begin(), inFrames.begin() + std::min<size_t> (inFrames.size(), MaxFrames));
    if (frames.empty()) frames.push_back (std::vector<float> (N, 0.0f));
    t->frames = (int) frames.size();

    auto s = analyse (frames);
    hygieneSpectra (s);

    t->data.assign ((size_t) t->frames * NumLevels * Stride, 0.0f);
    std::vector<float> x (N);
    for (int f = 0; f < t->frames; ++f)
        for (int l = 0; l < NumLevels; ++l)
        {
            harmonicsToReal (s.h[(size_t) f], harmonicsInLevel (l), x.data(), N);
            float* d = t->data.data() + ((size_t) f * NumLevels + (size_t) l) * Stride;
            d[0] = x[N - 1];
            std::copy (x.begin(), x.end(), d + 1);
            d[N + 1] = x[0];
            d[N + 2] = x[1];
            d[N + 3] = x[2];
        }
    return t;
}

std::vector<std::vector<float>> Wavetable::levelZeroFrames() const
{
    std::vector<std::vector<float>> out ((size_t) frames);
    for (int f = 0; f < frames; ++f)
        out[(size_t) f].assign (frame0 (f), frame0 (f) + N);
    return out;
}
} // namespace prism::dsp
