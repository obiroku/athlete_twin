#pragma once
// Breakpoint shape (MSEG, custom LFO, remap). Up to 32 points with a power
// curve per segment and an optional loop region.
#include <array>
#include <cmath>
#include <algorithm>

namespace prism::dsp
{
struct ShapePoint { float x = 0, y = 0, curve = 0; }; // curve applies to the segment ending at this point

struct Shape
{
    static constexpr int MaxPoints = 32;
    std::array<ShapePoint, MaxPoints> pts {};
    int count = 2;
    int loopStart = 0, loopEnd = 1; // point indices

    Shape()
    {
        pts[0] = { 0.0f, 0.0f, 0.0f };
        pts[1] = { 1.0f, 1.0f, 0.0f };
    }

    static Shape triangle()
    {
        Shape s;
        s.count = 3;
        s.pts[0] = { 0.0f, 0.0f, 0.0f };
        s.pts[1] = { 0.5f, 1.0f, 0.0f };
        s.pts[2] = { 1.0f, 0.0f, 0.0f };
        s.loopStart = 0; s.loopEnd = 2;
        return s;
    }

    static Shape envelope()
    {
        Shape s;
        s.count = 4;
        s.pts[0] = { 0.0f, 0.0f, 0.0f };
        s.pts[1] = { 0.1f, 1.0f, -0.4f };
        s.pts[2] = { 0.5f, 0.5f, 0.4f };
        s.pts[3] = { 1.0f, 0.0f, 0.4f };
        s.loopStart = 1; s.loopEnd = 2;
        return s;
    }

    void sanitize() noexcept
    {
        count = std::max (2, std::min (MaxPoints, count));
        for (int i = 0; i < count; ++i)
        {
            auto& p = pts[(size_t) i];
            p.x = std::min (1.0f, std::max (0.0f, std::isfinite (p.x) ? p.x : 0.0f));
            p.y = std::min (1.0f, std::max (0.0f, std::isfinite (p.y) ? p.y : 0.0f));
            p.curve = std::min (1.0f, std::max (-1.0f, std::isfinite (p.curve) ? p.curve : 0.0f));
            if (i > 0 && p.x < pts[(size_t) i - 1].x) p.x = pts[(size_t) i - 1].x;
        }
        pts[0].x = 0.0f;
        pts[(size_t) count - 1].x = 1.0f;
        loopStart = std::max (0, std::min (count - 2, loopStart));
        loopEnd = std::max (loopStart + 1, std::min (count - 1, loopEnd));
    }

    static inline float curveFn (float t, float c) noexcept
    {
        if (c == 0.0f) return t;
        const float e = std::pow (2.0f, c * 3.0f); // c>0: slow start, c<0: fast start
        return std::pow (t, e);
    }

    // Evaluate at x in [0,1]; seg is a cached segment index hint.
    inline float eval (float x, int& seg) const noexcept
    {
        if (seg < 0 || seg >= count - 1) seg = 0;
        while (seg > 0 && x < pts[(size_t) seg].x) --seg;
        while (seg < count - 2 && x >= pts[(size_t) seg + 1].x) ++seg;
        const auto& a = pts[(size_t) seg];
        const auto& b = pts[(size_t) seg + 1];
        const float w = b.x - a.x;
        const float t = w > 1e-6f ? std::min (1.0f, std::max (0.0f, (x - a.x) / w)) : 1.0f;
        return a.y + (b.y - a.y) * curveFn (t, b.curve);
    }
    float eval (float x) const noexcept { int s = 0; return eval (x, s); }
};
} // namespace prism::dsp
