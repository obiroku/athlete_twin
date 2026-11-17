#pragma once

#include "FilterDesign.h"

// Runtime TPT filter sections (Simper's "SvfLinearTrapOptimised2").
namespace lens::dsp
{
struct SvfRun
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    static SvfRun from (const SvfSection& s) noexcept
    {
        SvfRun r;
        r.a1 = 1.0 / (1.0 + s.g * (s.g + s.k));
        r.a2 = s.g * r.a1;
        r.a3 = s.g * r.a2;
        r.m0 = s.m0;
        r.m1 = s.m1;
        r.m2 = s.m2;
        return r;
    }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
};

inline double svfTick (const SvfRun& c, SvfState& st, double v0) noexcept
{
    const double v3 = v0 - st.ic2;
    const double v1 = c.a1 * st.ic1 + c.a2 * v3;
    const double v2 = st.ic2 + c.a2 * st.ic1 + c.a3 * v3;
    st.ic1 = 2.0 * v1 - st.ic1;
    st.ic2 = 2.0 * v2 - st.ic2;
    return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
}

struct OnePoleRun
{
    double G = 0.0, mLp = 1.0, mHp = 0.0;

    static OnePoleRun from (const OnePoleSection& p) noexcept
    {
        OnePoleRun r;
        r.G = p.g / (1.0 + p.g);
        r.mLp = p.mLp;
        r.mHp = p.mHp;
        return r;
    }
};

inline double onePoleTick (const OnePoleRun& c, double& s, double x) noexcept
{
    const double v = (x - s) * c.G;
    const double lp = v + s;
    s = lp + v;
    return c.mLp * lp + c.mHp * (x - lp);
}

// A single SVF section with state for each channel: used by solo,
// detectors and the hidden ear-trainer band.
struct SimpleSvf
{
    SvfRun run;
    SvfState st[2];

    void set (const SvfSection& s) noexcept { run = SvfRun::from (s); }
    void reset() noexcept { st[0] = st[1] = {}; }
    double tick (int ch, double x) noexcept { return svfTick (run, st[ch], x); }
};
} // namespace lens::dsp
