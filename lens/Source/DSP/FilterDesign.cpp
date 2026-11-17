#include "FilterDesign.h"

#include <algorithm>
#include <cmath>

namespace lens::dsp
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using cd = std::complex<double>;

double ampFromDb (double db) noexcept { return std::pow (10.0, db / 40.0); }

// Butterworth section Q values for order n (pairs only).
double butterworthQ (int order, int pairIndex) noexcept
{
    if (order % 2 == 0)
        return 1.0 / (2.0 * std::cos (pi * (2.0 * pairIndex + 1.0) / (2.0 * order)));
    return 1.0 / (2.0 * std::cos (pi * (pairIndex + 1.0) / order));
}

// ---------------------------------------------------------------- analog

cd analogSecond (Shape shape, cd s, double A, double q) noexcept
{
    switch (shape)
    {
        case Shape::Bell: return (s * s + (A / q) * s + 1.0) / (s * s + s / (q * A) + 1.0);
        case Shape::LowShelf:
        {
            const cd t = s * std::sqrt (A);
            return (t * t + (A / q) * t + A * A) / (t * t + t / q + 1.0);
        }
        case Shape::HighShelf:
        {
            const cd t = s / std::sqrt (A);
            return (A * A * t * t + (A / q) * t + 1.0) / (t * t + t / q + 1.0);
        }
        case Shape::Notch: return (s * s + 1.0) / (s * s + s / q + 1.0);
        case Shape::BandPass: return (s / q) / (s * s + s / q + 1.0);
        case Shape::AllPass: return (s * s - s / q + 1.0) / (s * s + s / q + 1.0);
        default: return 1.0;
    }
}

// ---------------------------------------------------------------- digital

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

// Impulse-invariant poles of s² + (w0/Q)·s + w0² (w0 in rad/sample).
void matchedPoles (double w0, double q, double& a1, double& a2) noexcept
{
    const double z = 1.0 / (2.0 * q);
    const double e = std::exp (-z * w0);
    if (z <= 1.0)
        a1 = -2.0 * e * std::cos (std::sqrt (1.0 - z * z) * w0);
    else
        a1 = -2.0 * e * std::cosh (std::sqrt (z * z - 1.0) * w0);
    a2 = e * e;
}

double denPower (double a1, double a2, double w) noexcept
{
    const double p1 = std::sin (0.5 * w) * std::sin (0.5 * w);
    const double p0 = 1.0 - p1;
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
    return A0 * p0 + A1 * p1 + A2 * 4 * p0 * p1;
}

// Minimum-phase numerator whose magnitude matches mag0 (DC), magN
// (Nyquist) and magM at wm (after Vicanek, "Matched Second Order Digital
// Filters", 2016).
Biquad matchedNumerator (double a1, double a2, double mag0, double magN, double wm, double magM) noexcept
{
    Biquad f;
    f.a1 = a1;
    f.a2 = a2;
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2);
    const double p1 = std::sin (0.5 * wm) * std::sin (0.5 * wm);
    const double p0 = 1.0 - p1;
    const double p2 = 4.0 * p0 * p1;
    const double B0 = mag0 * mag0 * A0;
    const double B1 = magN * magN * A1;
    const double B2 = (magM * magM * denPower (a1, a2, wm) - B0 * p0 - B1 * p1) / p2;
    const double W = 0.5 * (std::sqrt (B0) + std::sqrt (B1));
    f.b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
    f.b1 = 0.5 * (std::sqrt (B0) - std::sqrt (B1));
    f.b2 = f.b0 > 0.0 ? -B2 / (4.0 * f.b0) : 0.0;
    return f;
}

double radians (double f, double fs) noexcept
{
    return 2.0 * pi * std::min (f, 0.49 * fs) / fs;
}

// Biquad → TPT SVF (g, k, m0, m1, m2). Exact for any stable biquad.
SvfSection toSvf (const Biquad& f) noexcept
{
    SvfSection s;
    const double num = 1.0 + f.a1 + f.a2;
    const double den = 1.0 - f.a1 + f.a2;
    s.g = std::sqrt (std::max (1.0e-30, num / den));
    const double g2 = s.g * s.g;
    s.k = (1.0 + g2) * (1.0 - f.a2) / ((1.0 + f.a2) * s.g);
    const double N = 1.0 + s.g * s.k + g2;
    s.m0 = N * (f.b0 - f.b1 + f.b2) / 4.0;
    s.m1 = N * (f.b0 - f.b2) / (2.0 * s.g) - s.k * s.m0;
    s.m2 = (N * f.b1 - 2.0 * (g2 - 1.0) * s.m0) / (2.0 * g2);
    return s;
}

// One-pole (b0 + b1·z⁻¹)/(1 − p·z⁻¹) → TPT one-pole.
OnePoleSection toOnePole (double b0, double b1, double p) noexcept
{
    OnePoleSection o;
    o.g = (1.0 - p) / (1.0 + p);
    o.mLp = (b0 + b1) * (1.0 + o.g) / (2.0 * o.g);
    o.mHp = (b0 - b1) * (1.0 + o.g) / 2.0;
    return o;
}

double lowPassMag (double ratio, double q) noexcept
{
    const cd s (0.0, ratio);
    return std::abs (1.0 / (s * s + s / q + 1.0));
}

Biquad matchedLowPass (double fc, double q, double fs) noexcept
{
    double a1, a2;
    const double w0 = radians (fc, fs);
    matchedPoles (w0, q, a1, a2);
    const double magN = lowPassMag ((fs / 2) / fc, q);
    const double magM = lowPassMag (w0 * fs / (2 * pi) / fc, q);
    return matchedNumerator (a1, a2, 1.0, magN, w0, magM);
}

Biquad matchedHighPass (double fc, double q, double fs) noexcept
{
    Biquad f;
    const double w0 = radians (fc, fs);
    matchedPoles (w0, q, f.a1, f.a2);
    const double p1 = std::sin (0.5 * w0) * std::sin (0.5 * w0);
    const cd s (0.0, w0 * fs / (2 * pi) / fc);
    const double mag = std::abs (s * s / (s * s + s / q + 1.0));
    f.b0 = mag * std::sqrt (denPower (f.a1, f.a2, w0)) / (4.0 * p1);
    f.b1 = -2.0 * f.b0;
    f.b2 = f.b0;
    return f;
}

// Band-pass: one zero fixed at DC, numerator (1 − z⁻¹)(c0 + c1·z⁻¹)
// matched to the low-frequency asymptote and at the centre.
Biquad matchedBandPass (double fc, double q, double fs) noexcept
{
    Biquad f;
    const double w0 = radians (fc, fs);
    matchedPoles (w0, q, f.a1, f.a2);
    const double magN = std::abs (analogSecond (Shape::BandPass, cd (0.0, fs / 2 / fc), 1.0, q));
    const double magM = std::abs (analogSecond (Shape::BandPass, cd (0.0, w0 * fs / (2 * pi) / fc), 1.0, q));
    (void) magN;
    const double p1 = std::sin (0.5 * w0) * std::sin (0.5 * w0);
    const double p0 = 1.0 - p1;
    // Low-frequency asymptote ω/(ω0·Q) and the centre magnitude.
    const double asym = (1.0 + f.a1 + f.a2) / (2.0 * pi * fc / fs * q);
    const double u = asym * asym;
    const double v = std::max (0.0, (magM * magM * denPower (f.a1, f.a2, w0) / (4.0 * p1) - u * p0) / p1);
    const double c0 = 0.5 * (std::sqrt (u) + std::sqrt (v));
    const double c1 = 0.5 * (std::sqrt (u) - std::sqrt (v));
    f.b0 = c0;
    f.b1 = c1 - c0;
    f.b2 = -c1;
    return f;
}

Biquad invert (const Biquad& f) noexcept
{
    Biquad r;
    r.b0 = 1.0 / f.b0;
    r.b1 = f.a1 / f.b0;
    r.b2 = f.a2 / f.b0;
    r.a1 = f.b1 / f.b0;
    r.a2 = f.b2 / f.b0;
    return r;
}

Biquad matchedBellBoost (double fc, double A, double q, double fs) noexcept
{
    // A ≥ 1. Poles have Q·A; numerator matched at DC, Nyquist and fc.
    double a1, a2;
    const double w0 = radians (fc, fs);
    matchedPoles (w0, q * A, a1, a2);
    const double magN = std::abs (analogSecond (Shape::Bell, cd (0.0, fs / 2 / fc), A, q));
    const double magM = std::abs (analogSecond (Shape::Bell, cd (0.0, w0 * fs / (2 * pi) / fc), A, q));
    return matchedNumerator (a1, a2, 1.0, magN, w0, magM);
}

Biquad matchedLowShelf (double fc, double A, double q, double fs) noexcept
{
    // Matched-z: poles at fc/√A, zeros at fc·√A (both Q), DC gain A².
    const double sA = std::sqrt (A);
    double pa1, pa2, za1, za2;
    matchedPoles (radians (fc / sA, fs), q, pa1, pa2);
    matchedPoles (radians (fc * sA, fs), q, za1, za2);
    const double K = A * A * (1.0 + pa1 + pa2) / (1.0 + za1 + za2);
    Biquad f;
    f.b0 = K;
    f.b1 = K * za1;
    f.b2 = K * za2;
    f.a1 = pa1;
    f.a2 = pa2;
    return f;
}

SvfSection bilinear (double tanW, double k, double m0, double m1, double m2) noexcept
{
    SvfSection s;
    s.g = tanW;
    s.k = k;
    s.m0 = m0;
    s.m1 = m1;
    s.m2 = m2;
    return s;
}

OnePoleSection matchedOnePole (double poleHz, double dcMag, double nyqMag, double fs) noexcept
{
    const double p = std::exp (-2.0 * pi * poleHz / fs);
    const double sum = dcMag * (1.0 - p);   // b0 + b1
    const double diff = nyqMag * (1.0 + p); // b0 − b1
    return toOnePole (0.5 * (sum + diff), 0.5 * (sum - diff), p);
}
} // namespace

double clampFrequency (double freq, double sampleRate) noexcept
{
    return std::clamp (freq, 10.0, 0.45 * sampleRate);
}

SvfSection gainSection (Shape shape, double fc, double q, double A, double fs) noexcept
{
    q = std::clamp (q, 0.025, 40.0);
    if (std::abs (A - 1.0) < 1.0e-12)
        return {}; // identity: m0 = 1, m1 = m2 = 0

    switch (shape)
    {
        case Shape::LowShelf: return toSvf (matchedLowShelf (fc, A, q, fs));
        case Shape::HighShelf:
        {
            // High shelf(A) = A² · low shelf(1/A).
            auto f = matchedLowShelf (fc, 1.0 / A, q, fs);
            f.b0 *= A * A;
            f.b1 *= A * A;
            f.b2 *= A * A;
            return toSvf (f);
        }
        case Shape::Bell:
        default:
            if (A >= 1.0)
                return toSvf (matchedBellBoost (fc, A, q, fs));
            // Cuts are the exact inverse of the matching boost.
            return toSvf (invert (matchedBellBoost (fc, 1.0 / A, q, fs)));
    }
}

SvfSection bandPassSection (double tanW, double q) noexcept
{
    const double k = 1.0 / q;
    return bilinear (tanW, k, 0.0, k, 0.0);
}

SvfSection lowPassSection (double tanW, double q) noexcept
{
    return bilinear (tanW, 1.0 / q, 0.0, 0.0, 1.0);
}

SvfSection highPassSection (double tanW, double q) noexcept
{
    const double k = 1.0 / q;
    return bilinear (tanW, k, 1.0, -k, -1.0);
}

BandDesign designAnalog (Shape shape, double freq, double gainDb, double q, int slopeIndex) noexcept
{
    BandDesign d;
    d.shape = shape;
    d.freq = std::max (freq, 1.0e-3);
    d.gainDb = gainDb;
    d.q = std::clamp (q, 0.025, 40.0);
    d.slope = slopeIndex;
    d.fc = d.freq;
    return d;
}

BandDesign designBand (Shape shape, double freq, double gainDb, double q, int slopeIndex, double fs) noexcept
{
    BandDesign d = designAnalog (shape, freq, gainDb, q, slopeIndex);
    const double fc = clampFrequency (freq, fs);
    d.fc = fc;
    q = d.q;
    const double A = ampFromDb (gainDb);

    switch (shape)
    {
        case Shape::Bell:
        case Shape::LowShelf:
        case Shape::HighShelf:
            // Always one section, even at 0 dB (where it is an exact
            // identity): a dynamic band modulates this section.
            d.numSvf = 1;
            d.svf[0] = gainSection (shape, fc, q, A, fs);
            break;

        case Shape::Notch:
        {
            Biquad f;
            const double w0 = radians (fc, fs);
            matchedPoles (w0, q, f.a1, f.a2);
            const double c = std::cos (w0);
            const double scale = (1.0 + f.a1 + f.a2) / (2.0 - 2.0 * c);
            f.b0 = scale;
            f.b1 = -2.0 * c * scale;
            f.b2 = scale;
            d.numSvf = 1;
            d.svf[0] = toSvf (f);
            break;
        }

        case Shape::BandPass:
            d.numSvf = 1;
            d.svf[0] = toSvf (matchedBandPass (fc, q, fs));
            break;

        case Shape::AllPass:
        {
            const double k = 1.0 / q;
            d.numSvf = 1;
            d.svf[0] = bilinear (std::tan (pi * fc / fs), k, 1.0, -2.0 * k, 0.0);
            break;
        }

        case Shape::LowCut:
        case Shape::HighCut:
        {
            const bool low = shape == Shape::LowCut;
            const int order = slopeOrder (slopeIndex);
            const int pairs = order / 2;
            d.numSvf = pairs;
            for (int i = 0; i < pairs; ++i)
            {
                const double sq = butterworthQ (order, i);
                d.svf[(size_t) i] = toSvf (low ? matchedHighPass (fc, sq, fs) : matchedLowPass (fc, sq, fs));
            }
            if (order % 2 == 1)
            {
                d.hasOnePole = true;
                const double x = (fs / 2.0) / fc;
                const double lpN = 1.0 / std::sqrt (1.0 + x * x);
                const double hpN = x / std::sqrt (1.0 + x * x);
                d.onePole = low ? matchedOnePole (fc, 0.0, hpN, fs) : matchedOnePole (fc, 1.0, lpN, fs);
            }
            break;
        }

        case Shape::Tilt:
        {
            if (std::abs (gainDb) < 1.0e-9)
                break;
            // H(s) = (B·s + 1)/(s + B), s normalised to fc: lows −gain/2,
            // highs +gain/2, unity at fc.
            // Matched-z: pole at fc·B, zero at fc/B, DC gain 1/B.
            const double B = A;
            const double p = std::exp (-2.0 * pi * fc * B / fs);
            const double zz = std::exp (-2.0 * pi * fc / (B * fs));
            const double K = (1.0 / B) * (1.0 - p) / (1.0 - zz);
            d.hasOnePole = true;
            d.onePole = toOnePole (K, -K * zz, p);
            break;
        }
    }

    return d;
}

namespace
{
cd svfAt (const SvfSection& s, cd sNorm) noexcept
{
    const auto den = sNorm * sNorm + s.k * sNorm + 1.0;
    return s.m0 + (s.m1 * sNorm + s.m2) / den;
}

cd onePoleAt (const OnePoleSection& p, cd sNorm) noexcept
{
    return (p.mLp + p.mHp * sNorm) / (sNorm + 1.0);
}
} // namespace

std::complex<double> responseAtW (const BandDesign& d, double w) noexcept
{
    cd h (1.0, 0.0);
    for (int i = 0; i < d.numSvf; ++i)
        h *= svfAt (d.svf[(size_t) i], { 0.0, w / d.svf[(size_t) i].g });
    if (d.hasOnePole)
        h *= onePoleAt (d.onePole, { 0.0, w / d.onePole.g });
    return h;
}

std::complex<double> responseDigital (const BandDesign& d, double freq, double fs) noexcept
{
    return responseAtW (d, std::tan (pi * std::min (freq, 0.4999999 * fs) / fs));
}

std::complex<double> analogPrototype (Shape shape, double freq, double gainDb, double q, int slopeIndex, double at) noexcept
{
    const cd s (0.0, at / std::max (freq, 1.0e-3));
    const double A = ampFromDb (gainDb);
    q = std::clamp (q, 0.025, 40.0);

    switch (shape)
    {
        case Shape::LowCut:
        case Shape::HighCut:
        {
            const bool low = shape == Shape::LowCut;
            const int order = slopeOrder (slopeIndex);
            cd h (1.0, 0.0);
            for (int i = 0; i < order / 2; ++i)
            {
                const double sq = butterworthQ (order, i);
                const cd den = s * s + s / sq + 1.0;
                h *= low ? s * s / den : 1.0 / den;
            }
            if (order % 2 == 1)
                h *= low ? s / (s + 1.0) : 1.0 / (s + 1.0);
            return h;
        }
        case Shape::Tilt: return (A * s + 1.0) / (s + A);
        default: return analogSecond (shape, s, A, q);
    }
}

std::complex<double> responseAnalog (const BandDesign& d, double freq) noexcept
{
    return analogPrototype (d.shape, d.freq, d.gainDb, d.q, d.slope, freq);
}
} // namespace lens::dsp
