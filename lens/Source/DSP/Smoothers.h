#pragma once

#include <cmath>

namespace lens::dsp
{
// Linear ramp to a target over a fixed number of samples. Advancing by n
// samples at once lands exactly where n single steps would (to rounding).
struct Ramp
{
    double current = 0.0;
    double target = 0.0;
    double step = 0.0;
    int left = 0;

    void setTarget (double t, int length) noexcept
    {
        if (t == target) // NOLINT exact compare: unchanged target
            return;
        target = t;
        if (length <= 0)
        {
            snap();
            return;
        }
        step = (target - current) / length;
        left = length;
    }

    void snap (double t) noexcept
    {
        target = t;
        snap();
    }

    void snap() noexcept
    {
        current = target;
        left = 0;
        step = 0.0;
    }

    void skip (int n) noexcept
    {
        if (left <= 0)
            return;
        if (n >= left)
        {
            current = target;
            left = 0;
        }
        else
        {
            current += step * n;
            left -= n;
        }
    }

    double next() noexcept
    {
        skip (1);
        return current;
    }

    bool isMoving() const noexcept { return left > 0; }
};

// One-pole smoother: y += a·(x − y), with a for a time constant.
inline double onePoleCoeff (double timeSeconds, double sampleRate) noexcept
{
    if (timeSeconds <= 0.0)
        return 1.0;
    return 1.0 - std::exp (-1.0 / (timeSeconds * sampleRate));
}
} // namespace lens::dsp
