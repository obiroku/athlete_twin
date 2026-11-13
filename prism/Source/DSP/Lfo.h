#pragma once
#include "Common.h"
#include "Random.h"
#include "Shape.h"
#include "../Params/Params.h"

namespace prism::dsp
{
class Lfo
{
public:
    struct Settings
    {
        float rateHz = 1.0f;
        LfoShape shape = LfoShape::Sine;
        bool bipolar = true;
        float phaseOffset = 0.0f;
        float delaySec = 0.0f, fadeSec = 0.0f;
        float smooth = 0.0f;
        bool oneShot = false;
    };

    void prepare (double sampleRate) noexcept { sr = sampleRate; }

    void reseed (uint64_t seed) noexcept
    {
        rng.reseed (seed);
        prevRand = rng.bipolar();
        nextRand = rng.bipolar();
        held = prevRand;
    }

    void set (const Settings& s) noexcept
    {
        st = s;
        inc = (double) s.rateHz / sr;
        delaySamples = (int64_t) std::llround ((double) s.delaySec * sr);
        fadeSamples = (int64_t) std::llround ((double) s.fadeSec * sr);
        smoothCoef = s.smooth > 0.0f ? onePoleCoef (0.0005 + 0.25 * (double) s.smooth * s.smooth, sr) : 0.0f;
    }

    // Restart (Retrig / Env modes, or transport start for free LFOs)
    void trigger() noexcept
    {
        phase = 0.0;
        age = 0;
        finished = false;
        smoothed = rawValue (phase);
    }

    // Host sync: lock to an absolute phase in cycles
    void syncPhase (double cycles) noexcept
    {
        const double p = cycles - std::floor (cycles);
        if (p < phase - 0.5) newCycle();
        phase = p;
    }

    double getPhase() const noexcept { return phase; }

    inline float next (const Shape* custom) noexcept
    {
        ++age;
        if (age > delaySamples && ! finished)
        {
            phase += inc;
            if (phase >= 1.0)
            {
                if (st.oneShot) { phase = 0.999999; finished = true; }
                else { phase -= std::floor (phase); newCycle(); }
            }
        }
        float v = rawValue (phase, custom);
        if (smoothCoef > 0.0f) { smoothed = v + (smoothed - v) * smoothCoef; v = smoothed; }
        else smoothed = v;
        float g = 1.0f;
        if (age <= delaySamples) g = 0.0f;
        else if (fadeSamples > 0 && age - delaySamples < fadeSamples)
            g = (float) (age - delaySamples) / (float) fadeSamples;
        current = st.bipolar ? v * g : (v * 0.5f + 0.5f) * g;
        return current;
    }

    float value() const noexcept { return current; }

    // Jump ahead n samples (no delay/fade/smooth); used for slow control-rate LFOs
    inline float advance (int n) noexcept
    {
        phase += inc * n;
        while (phase >= 1.0) { phase -= 1.0; newCycle(); }
        current = rawValue (phase);
        return current;
    }

private:
    double sr = 48000.0;
    Settings st;
    double phase = 0.0, inc = 0.0;
    int64_t age = 0, delaySamples = 0, fadeSamples = 0;
    bool finished = false;
    Rng rng;
    float prevRand = 0, nextRand = 0, held = 0, smoothed = 0, smoothCoef = 0, current = 0;

    void newCycle() noexcept
    {
        prevRand = nextRand;
        nextRand = rng.bipolar();
        held = rng.bipolar();
    }

    inline float rawValue (double ph, const Shape* custom = nullptr) noexcept
    {
        double p = ph + st.phaseOffset;
        p -= std::floor (p);
        const float x = (float) p;
        switch (st.shape)
        {
            case LfoShape::Sine: return sinCycle (x);
            case LfoShape::Triangle: return x < 0.25f ? 4.0f * x : (x < 0.75f ? 2.0f - 4.0f * x : 4.0f * x - 4.0f);
            case LfoShape::SawUp: return 2.0f * x - 1.0f;
            case LfoShape::SawDown: return 1.0f - 2.0f * x;
            case LfoShape::Square: return x < 0.5f ? 1.0f : -1.0f;
            case LfoShape::SampleHold: return held;
            case LfoShape::SmoothRandom:
            {
                const float t = 0.5f - 0.5f * sinCycle ((float) ph * 0.5f + 0.25f); // cosine ease 0..1
                return prevRand + (nextRand - prevRand) * t;
            }
            case LfoShape::Noise: return rng.bipolar();
            case LfoShape::Custom: return custom != nullptr ? custom->eval (x) * 2.0f - 1.0f : sinCycle (x);
        }
        return 0.0f;
    }
};
} // namespace prism::dsp
