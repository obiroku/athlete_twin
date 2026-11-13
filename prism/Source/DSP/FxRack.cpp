#include "FxRack.h"

namespace prism::dsp
{
FxRack::FxRack()
{
    for (int i = 0; i < NumFx; ++i)
    {
        mods[(size_t) i] = createFxModule (i);
        order[(size_t) i] = (int8_t) i;
    }
}

void FxRack::prepare (double s, int maxBlock)
{
    sr = s;
    for (auto& m : mods) m->prepare (sr, maxBlock);
    for (auto* v : { &dryL, &dryR, &tmpL, &tmpR, &rackDryL, &rackDryR }) v->assign ((size_t) maxBlock, 0.0f);
    for (auto& w : wet) { w.setLen ((int) (0.01 * sr)); w.snap (0.0f); }
    rackMix.setLen ((int) (0.005 * sr));
    rackMix.snap (1.0f);
    for (auto& e : enabled) e = false;
    reset();
}

void FxRack::reset()
{
    for (auto& m : mods) m->reset();
    for (auto& s : silentFor) s = 0;
    for (auto& s : sleeping) s = false;
    for (int i = 0; i < NumFx; ++i) wet[(size_t) i].snap (enabled[(size_t) i] ? 1.0f : 0.0f);
}

void FxRack::setOrder (const std::array<int8_t, NumFx>& o)
{
    if (o == order && ! orderPending) return;
    // validate permutation
    std::array<bool, NumFx> seen {};
    for (auto v : o) { if (v < 0 || v >= NumFx || seen[(size_t) v]) return; seen[(size_t) v] = true; }
    pendingOrder = o;
    orderPending = true;
    rackMix.set (0.0f);
}

void FxRack::update (const float* real, const FxContext& ctx, const int* enableParams)
{
    for (int i = 0; i < NumFx; ++i)
    {
        const bool en = real[enableParams[i]] >= 0.5f;
        if (en != enabled[(size_t) i])
        {
            enabled[(size_t) i] = en;
            wet[(size_t) i].set (en ? 1.0f : 0.0f);
            if (en) { sleeping[(size_t) i] = false; silentFor[(size_t) i] = 0; }
        }
        if (en || wet[(size_t) i].cur > 0.0f) mods[(size_t) i]->update (real, ctx);
    }
}

void FxRack::process (float* L, float* R, int n, bool endsOnTick)
{
    const bool anyActive = [&] { for (int i = 0; i < NumFx; ++i) if (enabled[(size_t) i] || wet[(size_t) i].cur > 0.0f) return true; return false; }();
    if (! anyActive && ! orderPending) return;

    const bool crossfading = orderPending || rackMix.left > 0 || rackMix.cur < 1.0f;
    if (crossfading)
    {
        std::copy (L, L + n, rackDryL.begin());
        std::copy (R, R + n, rackDryR.begin());
    }

    for (int slot = 0; slot < NumFx; ++slot)
    {
        const int id = order[(size_t) slot];
        auto& w = wet[(size_t) id];
        const bool en = enabled[(size_t) id];
        if (! en && w.cur <= 0.0f && w.left == 0) continue;

        // input silence tracking for sleep (sample-accurate, so independent of block splits)
        int firstLoud = -1, lastLoud = -1;
        for (int i = 0; i < n; ++i)
            if (std::max (std::abs (L[i]), std::abs (R[i])) >= 1e-6f) { if (firstLoud < 0) firstLoud = i; lastLoud = i; }
        auto& sf = silentFor[(size_t) id];
        sf = lastLoud < 0 ? sf + n : (int64_t) (n - 1 - lastLoud);
        int start = 0;
        if (sleeping[(size_t) id])
        {
            if (firstLoud < 0) { for (int i = 0; i < n; ++i) w.next(); continue; } // still asleep
            sleeping[(size_t) id] = false;                                         // wake on the first loud sample
            for (int i = 0; i < firstLoud; ++i) w.next();
            start = firstLoud;
        }
        float* l = L + start;
        float* r = R + start;
        const int len = n - start;

        const bool ramping = w.left > 0 || w.cur < 1.0f;
        if (ramping)
        {
            std::copy (l, l + len, dryL.begin());
            std::copy (r, r + len, dryR.begin());
        }
        mods[(size_t) id]->process (l, r, len);
        if (ramping)
            for (int i = 0; i < len; ++i)
            {
                const float g = w.next();
                l[i] = dryL[(size_t) i] + (l[i] - dryL[(size_t) i]) * g;
                r[i] = dryR[(size_t) i] + (r[i] - dryR[(size_t) i]) * g;
            }
        // go to sleep only on absolute tick boundaries, once the input has been silent past the tail
        if (endsOnTick && ! mods[(size_t) id]->infiniteTail() && (double) sf > (mods[(size_t) id]->tailSeconds() + 0.1) * sr)
        {
            mods[(size_t) id]->reset();
            sleeping[(size_t) id] = true;
        }
    }

    if (crossfading)
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = rackMix.next();
            L[i] = rackDryL[(size_t) i] + (L[i] - rackDryL[(size_t) i]) * g;
            R[i] = rackDryR[(size_t) i] + (R[i] - rackDryR[(size_t) i]) * g;
            if (orderPending && rackMix.cur <= 0.0f && rackMix.left == 0)
            {
                order = pendingOrder;
                orderPending = false;
                rackMix.set (1.0f);
            }
        }
    }
}

double FxRack::tailSeconds() const
{
    double t = 0.0;
    for (int i = 0; i < NumFx; ++i)
        if (enabled[(size_t) i]) t = std::max (t, mods[(size_t) i]->tailSeconds());
    return std::min (30.0, t);
}

// ============================================================== Master

void Master::prepare (double s, int)
{
    sr = s;
    vol.setLen ((int) (0.02 * sr));
    relCoef = std::exp (-1.0f / (0.12f * (float) sr));
    reset();
}

void Master::reset()
{
    delayL.fill (0); delayR.fill (0); gq.fill (1.0f); holdBuf.fill (1.0f);
    dPos = 0; relEnv = 1.0f;
    avgSum = (double) (LimiterLookahead + 1);
    bassLp.reset();
    vol.snap (st.volDb <= -59.9f ? 0.0f : dbToGain (st.volDb));
}

void Master::set (const Settings& s)
{
    st = s;
    vol.set (s.volDb <= -59.9f ? 0.0f : dbToGain (s.volDb));
    bassOn = s.bassMonoHz >= 60.0f;
    if (bassOn && s.bassMonoHz != lastBassHz) { bassLp.lowpass (s.bassMonoHz, 0.70710678, sr); lastBassHz = s.bassMonoHz; }
}

void Master::process (float* L, float* R, int n)
{
    constexpr int W = LimiterLookahead + 1; // 65-sample window, 64-sample delay
    constexpr int M = 128;
    float pre = 0.0f, pl = 0.0f, pr = 0.0f;
    const float T = 0.7f;
    for (int i = 0; i < n; ++i)
    {
        const float g = vol.next();
        float l = L[i] * g, r = R[i] * g;
        if (bassOn)
        {
            const float lo = bassLp.tick (l, 0), ro = bassLp.tick (r, 1);
            const float m = 0.5f * (lo + ro);
            l = l - lo + m;
            r = r - ro + m;
        }
        if (st.width != 1.0f)
        {
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * st.width;
            l = m + sd; r = m - sd;
        }
        if (st.softclip)
        {
            auto sc = [T] (float x) {
                const float a = std::abs (x);
                if (a <= T) return x;
                const float y = T + (1.0f - T) * tanhFast ((a - T) / (1.0f - T));
                return x < 0 ? -y : y;
            };
            l = sc (l); r = sc (r);
        }
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        pre = std::max (pre, std::max (std::abs (l), std::abs (r)));

        // ---- lookahead limiter
        const float a = std::max (std::abs (l), std::abs (r));
        const float req = a > ceiling ? ceiling / a : 1.0f;
        gq[(size_t) dPos] = req;
        float h = 1.0f;
        for (int k = 0; k < W; ++k) h = std::min (h, gq[(size_t) ((dPos - k + M) & (M - 1))]);
        relEnv = std::min (h, relEnv + (1.0f - relEnv) * (1.0f - relCoef));
        avgSum += (double) relEnv - (double) holdBuf[(size_t) ((dPos - W + M) & (M - 1))];
        holdBuf[(size_t) dPos] = relEnv;
        const float gain = std::min (1.0f, (float) (avgSum / (double) W));
        delayL[(size_t) dPos] = l; delayR[(size_t) dPos] = r;
        const int rd = (dPos - LimiterLookahead + M) & (M - 1);
        float ol = delayL[(size_t) rd] * gain, orr = delayR[(size_t) rd] * gain;
        ol = clampf (ol, -ceiling, ceiling);
        orr = clampf (orr, -ceiling, ceiling);
        L[i] = ol; R[i] = orr;
        pl = std::max (pl, std::abs (ol)); pr = std::max (pr, std::abs (orr));
        dPos = (dPos + 1) & (M - 1);
    }
    lastPreLimiterPeak = pre;
    peakL = pl; peakR = pr;
}
} // namespace prism::dsp
