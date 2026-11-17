#pragma once

#include <array>
#include <cmath>
#include <complex>

#include "Types.h"

// Filter designs for Zero Latency mode (SPEC §3).
//
// Every section runs as a TPT state-variable filter (Simper / Zavalishin),
// so parameters can move every sample without zipper noise. The section
// coefficients come from magnitude-matched designs: poles placed by
// impulse invariance and numerators fitted to the analog prototype's
// magnitude at DC, Nyquist and the band frequency. Any stable biquad maps
// exactly onto an SVF (g, k, m0, m1, m2), so the topology is unchanged,
// but there is almost no cramping near Nyquist. All Pass, whose magnitude
// is exactly 1 either way, keeps the plain pre-warped bilinear design.
//
// A section with (g, k, m) has the digital response
//     H = m0 + m1·s/D(s) + m2/D(s),  D = s² + k·s + 1,  s = j·tan(πf/fs)/g.
namespace lens::dsp
{
struct SvfSection
{
    double g = 1.0;
    double k = 1.4142135623730951;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

struct OnePoleSection
{
    double g = 1.0;
    double mLp = 0.0, mHp = 0.0;
};

constexpr int kMaxSvfSections = 8;

struct BandDesign
{
    // What was asked for (the analog prototype).
    Shape shape = Shape::Bell;
    double freq = 1000.0; // unclamped
    double gainDb = 0.0;
    double q = 1.0;
    int slope = 1;

    // The digital realisation.
    double fc = 1000.0; // clamped frequency the filter uses
    int numSvf = 0;
    std::array<SvfSection, kMaxSvfSections> svf {};
    bool hasOnePole = false;
    OnePoleSection onePole {};

    bool isIdentity() const noexcept { return numSvf == 0 && ! hasOnePole; }
};

// Zero Latency clamps band frequencies to [10 Hz, 0.45·fs].
double clampFrequency (double freq, double sampleRate) noexcept;

// Full design. slopeIndex is the bNN_slope choice index (cuts only).
BandDesign designBand (Shape, double freq, double gainDb, double q, int slopeIndex, double sampleRate) noexcept;

// Analog prototype only (Linear Phase kernels, tests): no digital part.
BandDesign designAnalog (Shape, double freq, double gainDb, double q, int slopeIndex) noexcept;

// The single section of a Bell or shelf at linear amplitude
// A = 10^(dB/40). Used every sample for dynamic bands.
SvfSection gainSection (Shape, double fc, double q, double A, double sampleRate) noexcept;

// Plain bilinear sections (solo, detectors, ear trainer): tanW = tan(π·fc/fs).
SvfSection bandPassSection (double tanW, double q) noexcept; // peak gain 1
SvfSection lowPassSection (double tanW, double q) noexcept;
SvfSection highPassSection (double tanW, double q) noexcept;

std::complex<double> responseDigital (const BandDesign&, double freq, double sampleRate) noexcept;
// From a precomputed w = tan(π·f/fs) (curve drawing, auto-gain).
std::complex<double> responseAtW (const BandDesign&, double w) noexcept;
// The analog prototype (no cramping).
std::complex<double> responseAnalog (const BandDesign&, double freq) noexcept;
std::complex<double> analogPrototype (Shape, double freq, double gainDb, double q, int slopeIndex, double atFreq) noexcept;

inline double toDb (double magnitude) noexcept
{
    return 20.0 * std::log10 (magnitude > 1.0e-30 ? magnitude : 1.0e-30);
}
} // namespace lens::dsp
