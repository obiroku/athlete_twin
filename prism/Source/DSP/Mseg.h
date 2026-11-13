#pragma once
#include "Shape.h"
#include "../Params/Params.h"

namespace prism::dsp
{
class MsegPlayer
{
public:
    void trigger() noexcept { pos = 0.0; held = true; seg = 0; done = false; }
    void release() noexcept { held = false; }
    double position() const noexcept { return pos; }

    inline float next (const Shape& s, MsegMode mode, double lengthSamples) noexcept
    {
        const double inc = 1.0 / std::max (1.0, lengthSamples);
        if (! done)
        {
            pos += inc;
            const bool looping = mode == MsegMode::Loop || (mode == MsegMode::SustainLoop && held);
            if (looping)
            {
                const double a = s.pts[(size_t) s.loopStart].x, b = s.pts[(size_t) s.loopEnd].x;
                if (b - a > 1e-6 && pos >= b) pos = a + std::fmod (pos - b, b - a);
                else if (b - a <= 1e-6 && pos >= b) pos = b;
            }
            if (pos >= 1.0) { pos = 1.0; done = true; }
        }
        value = s.eval ((float) pos, seg);
        return value;
    }
    float current() const noexcept { return value; }

private:
    double pos = 0.0;
    bool held = false, done = false;
    int seg = 0;
    float value = 0.0f;
};
} // namespace prism::dsp
