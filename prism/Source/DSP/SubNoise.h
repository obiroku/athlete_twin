#pragma once
#include "Common.h"
#include "Random.h"

namespace prism::dsp
{
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

class SubOsc
{
public:
    enum Shape { Sine, Triangle, Square, Saw };
    void start (float ph) noexcept { phase = ph - std::floor (ph); }
    float getPhase() const noexcept { return phase; }

    // inc: cycles per sample
    inline float next (float inc, int shape) noexcept
    {
        inc = clampf (inc, 0.0f, 0.5f);
        const float p = phase;
        float y = 0.0f;
        switch (shape)
        {
            case Sine: y = sinCycle (p); break;
            case Triangle: y = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); break;
            case Square:
            {
                y = p < 0.5f ? 1.0f : -1.0f;
                y += polyBlep (p, inc);
                float p2 = p + 0.5f; p2 -= std::floor (p2);
                y -= polyBlep (p2, inc);
                y *= 0.8f;
                break;
            }
            case Saw:
            {
                float p2 = p + 0.5f; p2 -= std::floor (p2); // start mid-ramp so phase 0 is a zero crossing
                y = 2.0f * p2 - 1.0f;
                y -= polyBlep (p2, inc);
                y *= 0.9f;
                break;
            }
        }
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return y;
    }

private:
    float phase = 0.0f;
};

class NoiseGen
{
public:
    void reseed (uint64_t s) noexcept { rng.reseed (s); b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0; brown = 0; lp = 0; }

    // type: 0 white, 1 pink (Paul Kellet), 2 brown
    inline float next (int type) noexcept
    {
        const float w = rng.bipolar();
        switch (type)
        {
            case 1:
            {
                b0 = 0.99886f * b0 + w * 0.0555179f;
                b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f;
                b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f;
                b5 = -0.7616f * b5 - w * 0.0168980f;
                const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
                b6 = w * 0.115926f;
                return pink * 0.2f;
            }
            case 2:
                brown = (brown + 0.02f * w) * 0.998f;
                return brown * 3.0f;
            default: return w * 0.6f;
        }
    }

    // One-pole tone tilt: tone 0..1, 0.5 neutral
    inline float tone (float x, float t, float coefLow, float coefHigh) noexcept
    {
        if (t < 0.5f)
        {
            lp += (x - lp) * coefLow;
            return lp;
        }
        lp += (x - lp) * coefHigh;
        const float hp = x - lp;
        return x + (hp - x) * ((t - 0.5f) * 2.0f);
    }

private:
    Rng rng;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, brown = 0, lp = 0;
};
} // namespace prism::dsp
