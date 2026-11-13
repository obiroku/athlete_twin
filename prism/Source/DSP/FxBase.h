#pragma once
// Shared building blocks for the FX rack.
#include "Common.h"
#include "Random.h"
#include <vector>

namespace prism::dsp
{
struct FxContext
{
    double sr = 48000.0;
    double bpm = 120.0;
    float modWheel = 0.0f;
};

class FxModule
{
public:
    virtual ~FxModule() = default;
    virtual void prepare (double sr, int maxBlock) = 0;
    virtual void reset() = 0;
    // called once per control tick with real parameter values (indexed by param index)
    virtual void update (const float* real, const FxContext& ctx) = 0;
    virtual void process (float* L, float* R, int n) = 0;
    virtual double tailSeconds() const = 0;
    virtual bool infiniteTail() const { return false; }
};

// Circular delay line with fractional (linear / cubic) reads
class DelayLine
{
public:
    void prepare (int maxSamples) { buf.assign ((size_t) maxSamples + 4, 0.0f); size = (int) buf.size(); w = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) noexcept { buf[(size_t) w] = x; if (++w >= size) w = 0; }
    // delay in samples (>= 1)
    inline float read (float d) const noexcept
    {
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        const float f = rp - (float) i0;
        if (i0 >= size) i0 -= size;
        int i1 = i0 + 1; if (i1 >= size) i1 = 0;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * f;
    }
    inline float readCubic (float d) const noexcept
    {
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) size;
        int i1 = (int) rp;
        const float f = rp - (float) i1;
        if (i1 >= size) i1 -= size;
        const int i0 = i1 == 0 ? size - 1 : i1 - 1;
        const int i2 = i1 + 1 >= size ? 0 : i1 + 1;
        const int i3 = i2 + 1 >= size ? 0 : i2 + 1;
        const float xm1 = buf[(size_t) i0], x0 = buf[(size_t) i1], x1 = buf[(size_t) i2], x2 = buf[(size_t) i3];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    inline float readInt (int d) const noexcept
    {
        int i = w - d;
        while (i < 0) i += size;
        return buf[(size_t) i];
    }
    int capacity() const noexcept { return size - 4; }
private:
    std::vector<float> buf;
    int size = 0, w = 0;
};

struct OnePole
{
    float z = 0, a = 0;
    void setLP (float fc, double sr) noexcept { a = 1.0f - std::exp (-kTwoPiF * clampf (fc, 1.0f, (float) sr * 0.49f) / (float) sr); }
    inline float lp (float x) noexcept { z += (x - z) * a; return z; }
    inline float hp (float x) noexcept { z += (x - z) * a; return x - z; }
    void reset() noexcept { z = 0; }
};

// RBJ biquad (transposed direct form II)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};
    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline float tick (float x, int ch) noexcept
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void set (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void lowpass (double f, double q, double sr) noexcept
    {
        const double w = 2 * kPi * std::min (f, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double f, double q, double sr) noexcept
    {
        const double w = 2 * kPi * std::min (f, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (double f, double q, double db, double sr) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::min (f, sr * 0.49) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double f, double db, double sr) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::min (f, sr * 0.49) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double f, double db, double sr) noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * std::min (f, sr * 0.49) / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// Value that ramps linearly toward a target over a fixed number of samples
struct Ramp
{
    float cur = 0, target = 0, step = 0; int left = 0, len = 480;
    void setLen (int n) noexcept { len = std::max (1, n); }
    void snap (float v) noexcept { cur = target = v; left = 0; }
    void set (float v) noexcept { if (v == target) return; target = v; left = len; step = (target - cur) / (float) len; }
    inline float next() noexcept { if (left > 0) { cur += step; if (--left == 0) cur = target; } return cur; }
};
} // namespace prism::dsp
