#include "FactoryTables.h"
#include "FFT.h"
#include "Random.h"
#include "Wavetable.h"
#include <cmath>
#include <functional>
#include <algorithm>

namespace prism::dsp
{
namespace
{
constexpr int N = Wavetable::N;
constexpr int H = Wavetable::MaxHarmonics;
constexpr double PI = 3.14159265358979323846;

using Amps = std::vector<double>; // index = harmonic, 0..H

// Sine-phase additive synthesis (all partials start at phase 0 so frames
// interpolate without cancellation).
std::vector<float> fromAmps (const Amps& a)
{
    std::vector<cplx> h ((size_t) H + 1, cplx (0, 0));
    for (int k = 1; k <= H && k < (int) a.size(); ++k)
        h[(size_t) k] = cplx (0.0, -a[(size_t) k]); // Re(-j A e^{jθ}) = A sin θ
    std::vector<float> out (N);
    harmonicsToReal (h, H, out.data(), N);
    return out;
}

// Additive synthesis with explicit cosine phase per harmonic
std::vector<float> fromAmpsPhase (const Amps& a, const std::vector<double>& ph)
{
    std::vector<cplx> h ((size_t) H + 1, cplx (0, 0));
    for (int k = 1; k <= H && k < (int) a.size(); ++k)
        h[(size_t) k] = std::polar (a[(size_t) k], ph[(size_t) k]);
    std::vector<float> out (N);
    harmonicsToReal (h, H, out.data(), N);
    return out;
}

// Render a periodic function at os x resolution, FFT, keep harmonics <= 512.
std::vector<float> highRes (int os, const std::function<double (double)>& fn)
{
    const int M = N * os;
    std::vector<double> x ((size_t) M);
    for (int i = 0; i < M; ++i) x[(size_t) i] = fn ((double) i / (double) M);
    auto h = realToHarmonics (x.data(), M);
    h.resize ((size_t) H + 1);
    h[0] = 0;
    std::vector<float> out (N);
    harmonicsToReal (h, H, out.data(), N);
    return out;
}

double frac (double x) { return x - std::floor (x); }

Amps sawAmps (double maxH = H)
{
    Amps a ((size_t) H + 1, 0.0);
    for (int k = 1; k <= H; ++k)
    {
        if (k > maxH + 1) break;
        double w = 1.0;
        if (k > maxH) w = maxH - std::floor (maxH); // partial top harmonic
        a[(size_t) k] = w * (2.0 / PI) / k;
    }
    return a;
}

Amps squareAmps()
{
    Amps a ((size_t) H + 1, 0.0);
    for (int k = 1; k <= H; k += 2) a[(size_t) k] = (4.0 / PI) / k;
    return a;
}

Amps triAmps()
{
    Amps a ((size_t) H + 1, 0.0);
    for (int k = 1; k <= H; k += 2)
        a[(size_t) k] = (8.0 / (PI * PI)) / ((double) k * k) * (((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0);
    return a;
}

Amps sineAmps()
{
    Amps a ((size_t) H + 1, 0.0);
    a[1] = 1.0;
    return a;
}

Amps mixAmps (const Amps& a, const Amps& b, double t)
{
    Amps r (a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] * (1 - t) + b[i] * t;
    return r;
}

// Formant resonance magnitude at frequency f
double resonance (double f, double fc, double bw)
{
    const double x = (f - fc) / (bw * 0.5);
    return 1.0 / std::sqrt (1.0 + x * x);
}

struct Formants { double f[3]; double g[3]; double bw[3]; };

Amps vocalAmps (const Formants& fm, double f0, double breath, Rng* rng, double tiltAbove = 0.0)
{
    Amps a ((size_t) H + 1, 0.0);
    for (int k = 1; k <= H; ++k)
    {
        const double f = k * f0;
        double env = 0.0;
        for (int i = 0; i < 3; ++i) env += fm.g[i] * resonance (f, fm.f[i], fm.bw[i]);
        double src = 1.0 / std::pow ((double) k, 0.9); // glottal tilt
        double v = src * (0.04 + env);
        if (tiltAbove > 0.0 && f > tiltAbove) v *= std::pow (tiltAbove / f, 1.0); // soft top
        if (f > 9000.0) v *= std::exp (-(f - 9000.0) / 3000.0);
        if (rng != nullptr)
        {
            const double r = rng->bipolar();
            if (k > 6) v *= (1.0 + breath * 0.9 * r);
            v += breath * 0.004 * std::abs (rng->bipolar()) / std::sqrt ((double) k) * (k > 10 ? 1.0 : 0.0);
        }
        a[(size_t) k] = std::max (0.0, v);
    }
    return a;
}

Amps pulseAmps (double w, bool oddOnly)
{
    // centered pulse, cosine phase handled by caller via fromAmpsPhase
    Amps a ((size_t) H + 1, 0.0);
    for (int k = 1; k <= H; ++k)
    {
        if (oddOnly && (k % 2) == 0) continue;
        a[(size_t) k] = (2.0 / (PI * k)) * std::sin (PI * k * w);
    }
    return a;
}

std::vector<float> pulseFrame (double w, bool oddOnly)
{
    auto a = pulseAmps (w, oddOnly);
    std::vector<double> ph ((size_t) H + 1, 0.0);
    // cos phase: Re(A e^{jθ}) = A cos θ; handle sign via negative amplitude
    std::vector<cplx> h ((size_t) H + 1, cplx (0, 0));
    for (int k = 1; k <= H; ++k) h[(size_t) k] = cplx (a[(size_t) k], 0.0);
    std::vector<float> out (N);
    harmonicsToReal (h, H, out.data(), N);
    return out;
}

using Gen = std::function<std::vector<float> (double t, int frame)>;

struct Entry { const char* name; Gen gen; };

std::vector<Entry> makeEntries()
{
    std::vector<Entry> e;

    // 1. Basic Shapes
    e.push_back ({ "Basic Shapes", [] (double t, int) {
        const Amps shapes[4] = { sineAmps(), triAmps(), sawAmps(), squareAmps() };
        const double x = t * 3.0;
        const int s = std::min (2, (int) std::floor (x));
        return fromAmps (mixAmps (shapes[s], shapes[s + 1], x - s));
    } });

    // 2. Saw Sweep
    e.push_back ({ "Saw Sweep", [] (double t, int) {
        const double count = std::pow (2.0, 9.0 * t); // 1 .. 512
        return fromAmps (sawAmps (count));
    } });

    // 3. Formant Stack
    e.push_back ({ "Formant Stack", [] (double t, int) {
        const double fc = 300.0 * std::pow (10.0, t);
        const double bw = fc * 0.35 + 60.0;
        Amps a ((size_t) H + 1, 0.0);
        for (int k = 1; k <= H; ++k)
        {
            const double f = 110.0 * k;
            const double w = std::exp (-0.5 * std::pow ((f - fc) / (bw * 0.5), 2.0));
            a[(size_t) k] = (0.06 + w) / std::sqrt ((double) k) * (f > 12000.0 ? std::exp (-(f - 12000.0) / 3000.0) : 1.0);
        }
        return fromAmps (a);
    } });

    // 4. Vocal Ah
    e.push_back ({ "Vocal Ah", [] (double t, int frame) {
        Rng r (mixSeed (4, (uint64_t) frame));
        Formants fm { { 730, 1090, 2440 }, { 1.0, 0.5, 0.25 }, { 90, 110, 160 } };
        return fromAmps (vocalAmps (fm, 110.0, t, &r));
    } });

    // 5. Vocal Ee
    e.push_back ({ "Vocal Ee", [] (double t, int frame) {
        Rng r (mixSeed (5, (uint64_t) frame));
        Formants fm { { 270, 2290, 3010 }, { 1.0, 0.4, 0.3 }, { 70, 130, 170 } };
        return fromAmps (vocalAmps (fm, 110.0, t, &r));
    } });

    // 6. FM Bell: 1:3, index 0 -> 8
    e.push_back ({ "FM Bell", [] (double t, int) {
        const double I = 8.0 * t;
        return highRes (4, [I] (double p) { return std::sin (2 * PI * p + I * std::sin (2 * PI * 3.0 * p)); });
    } });

    // 7. FM Growl: 1:2 with feedback on the modulator, index 0 -> 12
    e.push_back ({ "FM Growl", [] (double t, int) {
        const double I = 12.0 * t;
        const double fb = 0.7;
        const int os = 16, M = N * os;
        std::vector<double> mod ((size_t) M);
        double prev = 0.0;
        for (int pass = 0; pass < 3; ++pass) // settle the feedback loop to a periodic state
            for (int i = 0; i < M; ++i)
            {
                const double p = (double) i / M;
                prev = std::sin (2 * PI * 2.0 * p + fb * prev);
                mod[(size_t) i] = prev;
            }
        return highRes (os, [&mod, I, M] (double p) {
            const int i = std::min (M - 1, (int) (p * M));
            return std::sin (2 * PI * p + I * mod[(size_t) i]);
        });
    } });

    // 8. Square Stack: odd harmonics only, pulse width 50% -> 5%
    e.push_back ({ "Square Stack", [] (double t, int) { return pulseFrame (0.5 - 0.45 * t, true); } });

    // 9. Triangle Stack: triangle -> wavefolded triangle
    e.push_back ({ "Triangle Stack", [] (double t, int) {
        const double g = 1.0 + 4.0 * t;
        return highRes (16, [g] (double p) {
            double tri = 1.0 - 4.0 * std::abs (frac (p + 0.25) - 0.5); // starts at 0 going up
            double x = tri * g;
            // reflect into [-1, 1]
            x = (x + 1.0) * 0.5;
            x = x - 2.0 * std::floor (x * 0.5);
            x = x > 1.0 ? 2.0 - x : x;
            return x * 2.0 - 1.0;
        });
    } });

    // 10. Noise Shaped: seeded random partials, tilt -12 -> 0 dB/oct
    e.push_back ({ "Noise Shaped", [] (double t, int) {
        Rng r (0x10A15E);
        Amps a ((size_t) H + 1, 0.0);
        std::vector<double> ph ((size_t) H + 1, 0.0);
        const double slope = -2.0 * (1.0 - t); // amplitude exponent: -12 dB/oct == h^-2
        for (int k = 1; k <= H; ++k)
        {
            const double amp = 0.3 + 0.7 * r.uniformD();
            ph[(size_t) k] = 2 * PI * r.uniformD();
            a[(size_t) k] = amp * std::pow ((double) k, slope);
        }
        return fromAmpsPhase (a, ph);
    } });

    // 11. Rezo: saw with a resonant peak travelling harmonic 2 -> 64
    e.push_back ({ "Rezo", [] (double t, int) {
        const double c = 2.0 * std::pow (32.0, t);
        Amps a = sawAmps();
        for (int k = 1; k <= H; ++k)
        {
            const double w = c * 0.12 + 0.7;
            a[(size_t) k] *= 1.0 + 7.0 * std::exp (-0.5 * std::pow ((k - c) / w, 2.0));
        }
        return fromAmps (a);
    } });

    // 12. Hollow: even only -> odd only (equal power)
    e.push_back ({ "Hollow", [] (double t, int) {
        Amps a ((size_t) H + 1, 0.0);
        const double ge = std::cos (t * PI * 0.5), go = std::sin (t * PI * 0.5);
        for (int k = 1; k <= H; ++k) a[(size_t) k] = ((k % 2 == 0) ? ge * 2.0 : go) / k;
        return fromAmps (a);
    } });

    // 13. PWM Sweep: pulse 50% -> 2%
    e.push_back ({ "PWM Sweep", [] (double t, int) { return pulseFrame (0.5 - 0.48 * t, false); } });

    // 14. Sync Sweep: naive hard-sync saw rendered at 64x, ratio 1 -> 8
    e.push_back ({ "Sync Sweep", [] (double t, int) {
        const double ratio = 1.0 + 7.0 * t;
        return highRes (64, [ratio] (double p) { return 1.0 - 2.0 * frac (p * ratio); });
    } });

    // 15/16. Additive 8 / 16: harmonics swelling in sequence
    auto additive = [] (int count) {
        return [count] (double t, int) {
            Amps a ((size_t) H + 1, 0.0);
            const double d = 1.0 / (count - 1);
            for (int k = 1; k <= count; ++k)
            {
                const double c = (k - 1) * d;
                const double x = std::clamp ((t - c) / d, -1.0, 1.0);
                const double w = std::cos (x * PI * 0.5);
                a[(size_t) k] = (0.12 + w * w) / std::sqrt ((double) k);
            }
            return fromAmps (a);
        };
    };
    e.push_back ({ "Additive 8", additive (8) });
    e.push_back ({ "Additive 16", additive (16) });

    // 17. Choirish: vowel morph Ah -> Oo -> Ee, soft top
    e.push_back ({ "Choirish", [] (double t, int frame) {
        const Formants ah { { 730, 1090, 2440 }, { 1.0, 0.5, 0.2 }, { 110, 130, 180 } };
        const Formants oo { { 300, 870, 2240 }, { 1.0, 0.45, 0.15 }, { 90, 120, 170 } };
        const Formants ee { { 270, 2290, 3010 }, { 1.0, 0.35, 0.25 }, { 90, 150, 190 } };
        const double x = t * 2.0;
        const Formants& A = x < 1.0 ? ah : oo;
        const Formants& B = x < 1.0 ? oo : ee;
        const double u = x < 1.0 ? x : x - 1.0;
        Formants m;
        for (int i = 0; i < 3; ++i)
        {
            m.f[i] = A.f[i] * std::pow (B.f[i] / A.f[i], u);
            m.g[i] = A.g[i] + (B.g[i] - A.g[i]) * u;
            m.bw[i] = A.bw[i] + (B.bw[i] - A.bw[i]) * u;
        }
        Rng r (mixSeed (17, (uint64_t) frame));
        return fromAmps (vocalAmps (m, 110.0, 0.15, &r, 3000.0));
    } });

    // 18. Organ-ish: drawbars 16',8',5 1/3',4',2 2/3',2',1 3/5',1 1/3',1' ->
    //     harmonics 1,2,3,4,6,8,10,12,16 of the 16' fundamental; 8 registrations.
    e.push_back ({ "Organ-ish", [] (double t, int) {
        static const int harm[9] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
        static const int reg[8][9] = {
            { 0, 8, 4, 0, 0, 0, 0, 0, 0 }, { 6, 8, 6, 3, 0, 0, 0, 0, 0 },
            { 4, 8, 8, 6, 2, 0, 0, 0, 0 }, { 8, 8, 6, 5, 3, 2, 0, 0, 0 },
            { 6, 7, 8, 7, 5, 4, 2, 0, 0 }, { 4, 6, 7, 8, 7, 6, 4, 3, 0 },
            { 8, 6, 5, 6, 7, 7, 6, 5, 4 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
        const double x = t * 7.0;
        const int r0 = std::min (6, (int) std::floor (x));
        const double u = x - r0;
        Amps a ((size_t) H + 1, 0.0);
        for (int i = 0; i < 9; ++i)
        {
            auto lvl = [] (int v) { return v == 0 ? 0.0 : std::pow (10.0, -3.0 * (8 - v) / 20.0); };
            a[(size_t) harm[i]] = lvl (reg[r0][i]) * (1 - u) + lvl (reg[r0 + 1][i]) * u;
        }
        return fromAmps (a);
    } });

    // 19. Tape Saturate: sine -> saw through tanh with rising drive
    e.push_back ({ "Tape Saturate", [] (double t, int) {
        const int os = 16;
        const int M = N * os;
        // bandlimited input at high resolution (512 harmonics)
        Amps in = mixAmps (sineAmps(), sawAmps(), t);
        std::vector<cplx> h ((size_t) M / 2 + 1, cplx (0, 0));
        for (int k = 1; k <= H; ++k) h[(size_t) k] = cplx (0.0, -in[(size_t) k]);
        std::vector<double> x ((size_t) M);
        harmonicsToReal (h, H, x.data(), M);
        const double g = 1.0 + 6.0 * t;
        return highRes (os, [&x, g, M] (double p) {
            const int i = std::min (M - 1, (int) std::lround (p * M) % M);
            return std::tanh (g * x[(size_t) i]) / std::tanh (g);
        });
    } });

    // 20. Glass: square-number harmonics, brightness rising
    e.push_back ({ "Glass", [] (double t, int) {
        Amps a ((size_t) H + 1, 0.0);
        const double p = 2.0 - 1.5 * t;
        for (int k = 1; k * k <= 484; ++k) a[(size_t) (k * k)] = std::pow ((double) k, -p);
        return fromAmps (a);
    } });

    // 21. Metallic: prime harmonics, weighting moving upward
    e.push_back ({ "Metallic", [] (double t, int) {
        Amps a ((size_t) H + 1, 0.0);
        a[1] = 0.5;
        const double centre = 1.0 + 7.0 * t; // octaves
        for (int k = 2; k <= H; ++k)
        {
            bool prime = true;
            for (int d = 2; d * d <= k; ++d) if (k % d == 0) { prime = false; break; }
            if (! prime) continue;
            const double o = std::log2 ((double) k);
            a[(size_t) k] = std::exp (-0.5 * std::pow ((o - centre) / 1.4, 2.0)) / std::pow ((double) k, 0.3);
        }
        return fromAmps (a);
    } });

    // 22. Wobble: comb-shaped spectral ripple whose phase moves
    e.push_back ({ "Wobble", [] (double t, int) {
        Amps a ((size_t) H + 1, 0.0);
        for (int k = 1; k <= H; ++k)
            a[(size_t) k] = (0.55 + 0.45 * std::cos (2 * PI * ((k - 1) / 6.0 - t))) / std::pow ((double) k, 0.85);
        return fromAmps (a);
    } });

    // 23. Chord Stack: 4:5:6 -> 10:12:15
    e.push_back ({ "Chord Stack", [] (double t, int) {
        Amps maj ((size_t) H + 1, 0.0), mnr ((size_t) H + 1, 0.0);
        auto addNote = [] (Amps& a, int r) {
            for (int k = 1; k * r <= H; ++k) a[(size_t) (k * r)] += 1.0 / k;
        };
        addNote (maj, 4); addNote (maj, 5); addNote (maj, 6);
        addNote (mnr, 10); addNote (mnr, 12); addNote (mnr, 15);
        Amps a ((size_t) H + 1, 0.0);
        const double ga = std::cos (t * PI * 0.5), gb = std::sin (t * PI * 0.5);
        for (int k = 1; k <= H; ++k) a[(size_t) k] = maj[(size_t) k] * ga + mnr[(size_t) k] * gb;
        a[1] = 0.25; // anchor the root so the table keeps its pitch
        return fromAmps (a);
    } });

    // 24. Sub Stack: fundamental heavy -> sub heavy square
    e.push_back ({ "Sub Stack", [] (double t, int) {
        Amps a ((size_t) H + 1, 0.0);
        a[1] = 1.0; a[2] = 0.5; a[3] = 0.15;
        Amps b = squareAmps();
        b[1] *= 1.6;
        for (auto& v : b) v *= 0.8;
        return fromAmps (mixAmps (a, b, t));
    } });

    return e;
}

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e = makeEntries();
    return e;
}
} // namespace

const std::vector<std::string>& factoryTableNames()
{
    static const std::vector<std::string> n = [] {
        std::vector<std::string> v;
        for (auto& e : entries()) v.push_back (e.name);
        return v;
    }();
    return n;
}

int factoryTableIndex (const std::string& name)
{
    const auto& n = factoryTableNames();
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == name) return (int) i;
    return -1;
}

std::vector<std::vector<float>> generateFactoryFrames (int index, int numFrames)
{
    const auto& e = entries();
    if (index < 0 || index >= (int) e.size()) index = 0;
    std::vector<std::vector<float>> frames ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f)
    {
        const double t = numFrames > 1 ? (double) f / (double) (numFrames - 1) : 0.0;
        frames[(size_t) f] = e[(size_t) index].gen (t, f);
    }
    return frames;
}
} // namespace prism::dsp
