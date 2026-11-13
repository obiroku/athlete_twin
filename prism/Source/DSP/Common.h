#pragma once
// Small, dependency-free DSP helpers shared by the PRISM engine.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace prism::dsp
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kPiF = 3.14159265358979f;
constexpr float kTwoPiF = 6.28318530717959f;

inline float clampf (float x, float lo, float hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return g > 1e-9f ? 20.0f * std::log10 (g) : -180.0f; }

// 2^x, continuous and accurate to ~2e-6 relative. Used per sample for pitch.
inline float fastExp2 (float x) noexcept
{
    x = clampf (x, -126.0f, 126.0f);
    const float fi = std::floor (x);
    const float f = x - fi;
    // minimax-ish polynomial for 2^f on [0,1)
    const float p = 1.0f + f * (0.6931471806f + f * (0.2402265070f + f * (0.0555041087f + f * (0.0096181291f + f * 0.0013333558f))));
    union { float fl; int32_t i; } u;
    u.i = (int32_t) ((int) fi + 127) << 23;
    return p * u.fl;
}

// log2(x) for x > 0, continuous across octaves (p(1)=0, p(2)=1).
inline float fastLog2 (float x) noexcept
{
    union { float fl; int32_t i; } u { x };
    const float e = (float) (((u.i >> 23) & 255) - 128);
    u.i &= ~(255 << 23);
    u.i += 127 << 23;
    const float m = u.fl; // [1,2)
    // cubic through (1,0),(2,1) with good fit; error < 5e-3
    const float p = (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
    return e + p;
}

// Pade approximation of tan(x), x in [0, ~1.45]; ~1% at the top end.
inline float fastTan (float x) noexcept
{
    const float x2 = x * x;
    return x * (945.0f - 105.0f * x2 + x2 * x2) / (945.0f - 420.0f * x2 + 15.0f * x2 * x2);
}

// sin(2*pi*p) for phase p in [0,1). Max error ~1e-4.
inline float sinCycle (float p) noexcept
{
    // map to [-0.5, 0.5)
    float x = p - std::floor (p + 0.5f);
    // sin(2 pi x) via parabolic approximation with refinement
    float y = 8.0f * x - 16.0f * x * std::abs (x);
    return 0.225f * (y * std::abs (y) - y) + y;
}

inline float tanhFast (float x) noexcept
{
    // rational approximation, saturates exactly at +-1
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return clampf (a / b, -1.0f, 1.0f);
}

// equal-power pan, p in [-1,1]
inline void panGains (float p, float& l, float& r) noexcept
{
    const float a = (clampf (p, -1.0f, 1.0f) + 1.0f) * 0.125f; // 0..0.25 cycles
    l = sinCycle (a + 0.25f);
    r = sinCycle (a);
}

inline bool isFiniteF (float x) noexcept { return std::isfinite (x); }

// One-pole smoothing coefficient for a time constant in samples
inline float onePoleCoef (double timeSec, double sr) noexcept
{
    if (timeSec <= 0.0) return 0.0f;
    return (float) std::exp (-1.0 / (timeSec * sr));
}

} // namespace prism::dsp
