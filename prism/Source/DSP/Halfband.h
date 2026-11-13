#pragma once
// Polyphase IIR halfband filters (two parallel chains of first-order allpass
// sections), used by every oversampler in PRISM (SPEC §2). Coefficients are
// computed from the closed-form elliptic halfband design.
#include <array>
#include <cmath>
#include <vector>

namespace prism::dsp
{
std::vector<double> designHalfband (int numCoefs, double transition);

template <int NC>
struct HalfbandCoefs
{
    std::array<float, NC> c {};
    HalfbandCoefs (double transition)
    {
        auto d = designHalfband (NC, transition);
        for (int i = 0; i < NC; ++i) c[(size_t) i] = (float) d[(size_t) i];
    }
};

// Steep: 8 coefficients, transition 0.04 (~70 dB stopband)
const HalfbandCoefs<8>& steepCoefs();
// Relaxed: 4 coefficients, transition 0.2 (outer stage of 4x)
const HalfbandCoefs<4>& relaxedCoefs();

template <int NC>
class HalfbandDecimator
{
public:
    void reset() noexcept { xa.fill (0); ya.fill (0); xb.fill (0); yb.fill (0); delayed = 0; }
    // Consumes two input samples, returns one.
    inline float process (float in0, float in1, const HalfbandCoefs<NC>& k) noexcept
    {
        // path A: even coefs on in1, path B: odd coefs on in0 (delayed by one)
        float a = in1, b = delayed;
        delayed = in0;
        for (int i = 0; i < NC / 2; ++i)
        {
            const float ca = k.c[(size_t) (2 * i)];
            const float oa = ca * (a - ya[(size_t) i]) + xa[(size_t) i];
            xa[(size_t) i] = a; ya[(size_t) i] = oa; a = oa;
            const float cb = k.c[(size_t) (2 * i + 1)];
            const float ob = cb * (b - yb[(size_t) i]) + xb[(size_t) i];
            xb[(size_t) i] = b; yb[(size_t) i] = ob; b = ob;
        }
        return 0.5f * (a + b);
    }
private:
    std::array<float, NC / 2> xa {}, ya {}, xb {}, yb {};
    float delayed = 0;
};

template <int NC>
class HalfbandInterpolator
{
public:
    void reset() noexcept { xa.fill (0); ya.fill (0); xb.fill (0); yb.fill (0); }
    inline void process (float in, float& out0, float& out1, const HalfbandCoefs<NC>& k) noexcept
    {
        float a = in, b = in;
        for (int i = 0; i < NC / 2; ++i)
        {
            const float ca = k.c[(size_t) (2 * i)];
            const float oa = ca * (a - ya[(size_t) i]) + xa[(size_t) i];
            xa[(size_t) i] = a; ya[(size_t) i] = oa; a = oa;
            const float cb = k.c[(size_t) (2 * i + 1)];
            const float ob = cb * (b - yb[(size_t) i]) + xb[(size_t) i];
            xb[(size_t) i] = b; yb[(size_t) i] = ob; b = ob;
        }
        out0 = b;
        out1 = a;
    }
private:
    std::array<float, NC / 2> xa {}, ya {}, xb {}, yb {};
};

// 2x oversampled nonlinearity helper (interpolate -> fn -> decimate)
class Oversampled2x
{
public:
    void reset() noexcept { up.reset(); down.reset(); }
    template <typename Fn>
    inline float process (float x, Fn&& fn) noexcept
    {
        float a, b;
        up.process (x, a, b, steepCoefs());
        return down.process (fn (a), fn (b), steepCoefs());
    }
private:
    HalfbandInterpolator<8> up;
    HalfbandDecimator<8> down;
};
} // namespace prism::dsp
