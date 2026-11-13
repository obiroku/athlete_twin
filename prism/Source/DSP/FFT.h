#pragma once
// Plain radix-2 complex FFT in double precision. Used off the audio thread
// for wavetable construction and in tests. Deterministic on every platform.
#include <complex>
#include <vector>

namespace prism::dsp
{
using cplx = std::complex<double>;

// In-place FFT. n must be a power of two. inverse=true computes the
// unnormalized inverse (caller divides by n).
void fft (std::vector<cplx>& a, bool inverse);

// Real-signal helpers for a cycle of length n (power of two).
// spectrum[h] for h = 0..n/2 (complex amplitude, scaled so a unit sine at
// harmonic h has |spectrum[h]| == 1).
std::vector<cplx> realToHarmonics (const float* x, int n);
std::vector<cplx> realToHarmonics (const double* x, int n);
// Synthesize n samples from harmonics[0..H] (H <= n/2) with the same scaling.
void harmonicsToReal (const std::vector<cplx>& harm, int maxHarm, float* out, int n);
void harmonicsToReal (const std::vector<cplx>& harm, int maxHarm, double* out, int n);

// Window + magnitude spectrum for analysis (Blackman-Harris 4-term)
std::vector<double> blackmanHarris (int n);
} // namespace prism::dsp
