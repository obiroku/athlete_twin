#include "WavetableOsc.h"
#include "Common.h"
#include <juce_dsp/juce_dsp.h>

namespace prism::dsp
{
using V = juce::dsp::SIMDRegister<float>;
static_assert (sizeof (V) == 16, "PRISM expects 4-wide float SIMD (SSE)");

// Crossfade zone: the top half-octave below the point where the brighter
// level would alias. The darker level reaches 100% exactly at that point.
static constexpr float kXfadeOct = 0.5f;
static constexpr float kSyncFade = 6.0f; // samples (at the oscillator rate)

MipChoice chooseMip (float inc) noexcept
{
    MipChoice m;
    if (inc <= 0.0f) return m;
    // highest harmonic allowed = 0.5 / inc ; L (in half-octave steps) = 2 * log2(512 / maxHarm)
    // fastLog2 is within 5e-3 of log2; the +0.012 margin keeps the choice on the safe (darker) side
    const float L = 2.0f * fastLog2 (1024.0f * inc) + 0.012f;
    if (L <= -1.0f + 1e-6f) return m;
    int k = (int) std::ceil (L);
    if (k < 0) k = 0;
    const float u = L - (float) (k - 1); // (0, 1]
    m.wDark = clampf ((u - (1.0f - kXfadeOct)) / kXfadeOct, 0.0f, 1.0f);
    if (k >= Wavetable::NumLevels) { m.level = Wavetable::NumLevels - 1; m.silent = true; return m; }
    m.level = k;
    return m;
}

RemapCurve::RemapCurve()
{
    // default: gentle S-curve so the warp knob is audible before any drawing
    for (int i = 0; i <= 32; ++i)
    {
        const float x = (float) i / 32.0f;
        y[(size_t) i] = x - 0.12f * std::sin (kTwoPiF * x);
    }
    computeSlope();
}

void RemapCurve::computeSlope() noexcept
{
    float m = 1.0f;
    for (int i = 0; i < 32; ++i) m = std::max (m, std::abs (y[(size_t) i + 1] - y[(size_t) i]) * 32.0f);
    maxSlope = m;
}

void WavetableOsc::start (Rng& rng, float ph, float randPhase) noexcept
{
    for (int i = 0; i < MaxUnison; ++i)
    {
        const float r = rng.uniform();
        float p = ph + randPhase * r;
        p -= std::floor (p);
        phase[i] = p;
    }
    for (int i = 0; i < MaxUnison; ++i) randLane[i] = rng.bipolar(); // "Random" unison spacing, new per note
    if (uni.mode == (int) UniMode::Random) rebuild();
}

void WavetableOsc::setUnison (const Unison& u) noexcept
{
    if (u.count == uni.count && u.detune == uni.detune && u.blend == uni.blend && u.spread == uni.spread
        && u.wtSpread == uni.wtSpread && u.mode == uni.mode && u.stack == uni.stack && u.range == uni.range && lanes == u.count)
        return;
    uni = u;
    rebuild();
}

void WavetableOsc::setLaneMod (float p, float w, float pan) noexcept
{
    if (p == lanePitch && w == laneWt && pan == lanePan) return;
    lanePitch = p; laneWt = w; lanePan = pan;
    rebuild();
}

void WavetableOsc::rebuild() noexcept
{
    lanes = std::max (1, std::min (MaxUnison, uni.count));
    const int N = lanes;
    // outer voices at +-range * 100 * d^1.25 cents (range 1 st: 0.15 -> 9 ct, 0.25 -> 18 ct, 1.0 -> 100 ct)
    const float outerCents = uni.range * 100.0f * std::pow (uni.detune, 1.25f);
    const float gC = std::min (1.0f, 2.0f * (1.0f - uni.blend));
    const float gS = std::min (1.0f, 2.0f * uni.blend);
    const float norm = 1.0f / std::sqrt ((float) N);
    // irregular "super" spacing (normalised offsets of a classic 7-voice supersaw)
    static const float superCurve[7] = { -1.0f, -0.5716f, -0.1775f, 0.0f, 0.1810f, 0.5650f, 0.9767f };
    maxRatio = 1.0f;
    for (int i = 0; i < MaxUnison; ++i)
    {
        if (i >= N)
        {
            ratio[i] = 0; gainL[i] = gainR[i] = gainM[i] = 0; wtOff[i] = 0; laneIndex[i] = 0;
            continue;
        }
        const float x = N > 1 ? 2.0f * (float) i / (float) (N - 1) - 1.0f : 0.0f;
        laneIndex[i] = x;
        float dx = x;
        switch ((UniMode) uni.mode)
        {
            case UniMode::Super:
            {
                const float t = (x + 1.0f) * 3.0f;
                const int k = std::min (5, (int) t);
                dx = superCurve[k] + (superCurve[k + 1] - superCurve[k]) * (t - (float) k);
                break;
            }
            case UniMode::Exp: dx = x * std::abs (x); break;
            case UniMode::Inv: dx = (x < 0 ? -1.0f : 1.0f) * std::sqrt (std::abs (x)); break;
            case UniMode::Random: dx = N > 1 ? randLane[i] : 0.0f; break;
            default: break;
        }
        float stack = 0.0f;
        const bool centre = N == 1 || std::abs (x) < 1e-6f || (N % 2 == 0 && std::abs (std::abs (x) - 1.0f / (float) (N - 1)) < 1e-6f);
        switch ((UniStack) uni.stack)
        {
            case UniStack::Oct: stack = (i & 1) ? 12.0f : 0.0f; break;
            case UniStack::OctFifth: stack = (i % 3 == 1) ? 12.0f : ((i % 3 == 2) ? 7.0f : 0.0f); break;
            case UniStack::CenterMinus12: stack = centre ? -12.0f : 0.0f; break;
            case UniStack::CenterMinus24: stack = centre ? -24.0f : 0.0f; break;
            default: break;
        }
        const float semis = outerCents * dx / 100.0f + lanePitch * x + stack;
        ratio[i] = std::pow (2.0f, semis / 12.0f);
        maxRatio = std::max (maxRatio, ratio[i]);
        float l, r;
        panGains (clampf (uni.spread * x + lanePan * x, -1.0f, 1.0f), l, r);
        const float g = N == 1 ? 1.0f : (centre ? gC : gS);
        gainM[i] = g * norm;
        // pan law normalised so a centred lane has unity gain per channel
        gainL[i] = gainM[i] * l * 1.41421356f;
        gainR[i] = gainM[i] * r * 1.41421356f;
        wtOff[i] = (uni.wtSpread * 0.5f + laneWt) * x;
    }
}

namespace
{
inline float bendMap (float p, float b) noexcept { return p * (1.0f + b) / (1.0f + b * p); }

struct WarpState
{
    WarpMode mode;
    float amt;
    float slope = 1.0f;
    float syncRatio = 1, b = 0, w = 1, c = 0.5f, q = 0, fmIdx = 0;
};

inline float warpPhase (float p, const WarpState& s, float mod, const RemapCurve& remap) noexcept
{
    switch (s.mode)
    {
        case WarpMode::Sync: { float x = p * s.syncRatio; return x - std::floor (x); }
        case WarpMode::BendPlus: return bendMap (p, s.b);
        case WarpMode::BendMinus: return 1.0f - bendMap (1.0f - p, s.b);
        case WarpMode::BendBoth:
            return p < 0.5f ? 0.5f * bendMap (2.0f * p, s.b) : 1.0f - 0.5f * bendMap (2.0f - 2.0f * p, s.b);
        case WarpMode::PWM: return p < s.w ? p / s.w : 0.0f;
        case WarpMode::Mirror: { const float m = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p; return lerpf (p, m * 0.999999f, s.amt); }
        case WarpMode::AsymPlus:
        case WarpMode::AsymMinus:
            return p < s.c ? 0.5f * p / s.c : 0.5f + 0.5f * (p - s.c) / (1.0f - s.c);
        case WarpMode::Remap: return clampf (lerpf (p, remap.eval (p), s.amt), 0.0f, 0.999999f);
        case WarpMode::FMOther:
        case WarpMode::FMSub: { float x = p + s.fmIdx * mod; return x - std::floor (x); }
        case WarpMode::Quantize: return s.amt > 0.0f && s.q > 0.0f ? std::floor (p * s.q) / s.q : p;
        default: return p;
    }
}

inline bool isPhaseWarp (WarpMode m) noexcept
{
    return m != WarpMode::None && m != WarpMode::AM && m != WarpMode::RM && m != WarpMode::Drive
        && m != WarpMode::Fold && m != WarpMode::Flip;
}

inline void setupWarp (WarpState& ws, WarpMode mode, float a, const RemapCurve& remap) noexcept
{
    ws.mode = mode;
    ws.amt = a;
    switch (mode)
    {
        case WarpMode::Sync: ws.syncRatio = 1.0f + 7.0f * a; ws.slope = ws.syncRatio; break;
        case WarpMode::BendPlus: case WarpMode::BendMinus: case WarpMode::BendBoth:
            ws.b = 7.0f * a; ws.slope = 1.0f + ws.b; break;
        case WarpMode::PWM: ws.w = 1.0f - 0.98f * a; ws.slope = 1.0f / ws.w; break;
        case WarpMode::Mirror: ws.slope = 1.0f + a; break;
        case WarpMode::AsymPlus: ws.c = 0.5f - 0.49f * a; ws.slope = 0.5f / ws.c; break;
        case WarpMode::AsymMinus: ws.c = 0.5f + 0.49f * a; ws.slope = 0.5f / (1.0f - ws.c); break;
        case WarpMode::Quantize: ws.q = std::round (std::pow (2.0f, 1.0f + 7.0f * (1.0f - a))); ws.slope = a > 0.0f ? 1.6f : 1.0f; break; // whole steps per cycle
        case WarpMode::Remap: ws.slope = 1.0f + a * (remap.maxSlope - 1.0f); break;
        case WarpMode::FMOther: case WarpMode::FMSub: ws.fmIdx = 2.0f * a; ws.slope = 1.0f + 6.0f * a; break;
        case WarpMode::Drive: ws.b = 1.0f + 9.0f * a; ws.w = 1.0f / tanhFast (ws.b); ws.slope = 1.0f + 2.0f * a; break;
        case WarpMode::Fold: ws.b = 1.0f + 4.0f * a; ws.slope = 1.0f + 3.0f * a; break;
        case WarpMode::Flip: ws.c = 1.0f - 0.98f * a; break;
        default: break;
    }
}

// output-domain warps (applied to the interpolated sample)
inline float warpOutput (float y, const WarpState& s, float mod, float flipGain) noexcept
{
    switch (s.mode)
    {
        case WarpMode::AM: return y * (1.0f - s.amt * 0.5f + s.amt * 0.5f * mod);
        case WarpMode::RM: return y * lerpf (1.0f, mod, s.amt);
        case WarpMode::Drive: return s.amt > 0.0f ? tanhFast (y * s.b) * s.w : y;
        case WarpMode::Fold: return s.amt > 0.0f ? sinCycle (y * s.b * 0.25f) : y;
        case WarpMode::Flip: return y * flipGain;
        default: return y;
    }
}
} // namespace

void WavetableOsc::render (const Wavetable* table, int n, const float* inc, const float* pos, const float* warp,
                           WarpMode mode, const float* warp2, WarpMode mode2, const float* mod, const RemapCurve& remap,
                           float* outL, float* outR, float* mono, int osFactor) noexcept
{
    // Mip levels are chosen against the host-rate Nyquist even when oversampled:
    // the oversampling headroom is for harmonics the warp creates, and table
    // harmonics in the halfband transition band would otherwise fold back.
    const float osMul = (float) std::max (1, osFactor);
    const bool hasTable = table != nullptr && table->numFrames() > 0;
    const int F = hasTable ? table->numFrames() : 1;
    if (lanes == 1 && hasTable && mode == WarpMode::None && mode2 == WarpMode::None)
    {
        renderSingle (*table, n, inc, pos, outL, outR, mono);
        return;
    }
    const int groups = (lanes + 3) / 4;
    const V one = V::expand (1.0f);

    alignas (16) float P0[4][4], P1[4][4], fr[4], ys[4], ys2[4], corr[4], win[4], flipG[4];
    const bool anySync = mode == WarpMode::Sync || mode2 == WarpMode::Sync;
    const bool anyFlip = mode == WarpMode::Flip || mode2 == WarpMode::Flip;
    // polyBLEP the quantize staircase when its steps sit on the master phase grid
    const bool quantBlep = mode == WarpMode::Quantize || (mode2 == WarpMode::Quantize && ! isPhaseWarp (mode));
    const bool outWarp = ! isPhaseWarp (mode) && mode != WarpMode::None;
    const bool outWarp2 = ! isPhaseWarp (mode2) && mode2 != WarpMode::None;
    auto polyBlep = [] (float t, float dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    };

    for (int i = 0; i < n; ++i)
    {
        WarpState ws, ws2;
        setupWarp (ws, mode, warp != nullptr ? clampf (warp[i], 0.0f, 1.0f) : 0.0f, remap);
        setupWarp (ws2, mode2, warp2 != nullptr ? clampf (warp2[i], 0.0f, 1.0f) : 0.0f, remap);
        const WarpState& wq = mode == WarpMode::Quantize ? ws : ws2;
        const float syncA = mode == WarpMode::Sync ? ws.amt : (mode2 == WarpMode::Sync ? ws2.amt : 0.0f);
        const WarpState& wf = mode == WarpMode::Flip ? ws : ws2;
        const float m = mod != nullptr ? mod[i] : 0.0f;
        const MipChoice mc = hasTable ? chooseMip (inc[i] * maxRatio * ws.slope * ws2.slope * osMul) : MipChoice {};
        const int lvB = mc.level;
        const int lvD = std::min (Wavetable::NumLevels - 1, mc.level + 1);
        const bool darkSilent = mc.level + 1 >= Wavetable::NumLevels;
        const float wD = mc.wDark;
        const bool needDark = wD > 0.0f;
        const bool hermB = Wavetable::useHermite (lvB), hermD = Wavetable::useHermite (lvD);
        const float basePos = clampf (pos != nullptr ? pos[i] : 0.0f, 0.0f, 1.0f);
        lastPosition = basePos;
        const V vinc = V::expand (inc[i]);

        V accL = V::expand (0.0f), accR = V::expand (0.0f), accM = V::expand (0.0f);

        for (int g = 0; g < groups; ++g)
        {
            float* ph = phase + 4 * g;
            V p = V::fromRawArray (ph) + V::fromRawArray (ratio + 4 * g) * vinc;
            p = p - (one & V::greaterThanOrEqual (p, one));
            p.copyToRawArray (ph);

            for (int j = 0; j < 4; ++j)
            {
                const int lane = 4 * g + j;
                corr[j] = 0.0f;
                win[j] = 1.0f;
                flipG[j] = 1.0f;
                if (lane >= lanes || ! hasTable || mc.silent)
                {
                    for (int k = 0; k < 4; ++k) P0[k][j] = P1[k][j] = 0.0f;
                    fr[j] = 0.0f;
                    if (lane < lanes && ! hasTable)
                    {
                        // sine fallback while the table is generating
                        P0[1][j] = sinCycle (warpPhase (warpPhase (ph[j], ws, m, remap), ws2, m, remap));
                    }
                    continue;
                }
                WarpState wsl = ws, wsl2 = ws2;
                const float laneInc = inc[i] * ratio[lane];
                // quantize steps finer than a sample: no staircase
                if (mode == WarpMode::Quantize && ws.q * laneInc > 0.5f) wsl.amt = 0.0f;
                if (mode2 == WarpMode::Quantize && ws2.q * laneInc > 0.5f) wsl2.amt = 0.0f;
                const float wp = warpPhase (warpPhase (ph[j], wsl, m, remap), wsl2, m, remap);
                const bool quantOn = quantBlep && (mode == WarpMode::Quantize ? wsl.amt : wsl2.amt) > 0.0f;
                if (anyFlip && wf.amt > 0.001f)
                {
                    // polarity flip from the flip point to the end of the cycle, with short smooth transitions
                    const float pm = ph[j];
                    const float li = std::max (1e-9f, laneInc);
                    const float e = (pm < 0.5f ? pm : pm - 1.0f) / li;   // samples from the wrap
                    const float d1 = (pm - wf.c) / li;                    // samples from the flip point
                    if (std::abs (e) < kSyncFade) flipG[j] = sinCycle (0.25f * clampf (e / kSyncFade, -1.0f, 1.0f));
                    else if (std::abs (d1) < kSyncFade) flipG[j] = -sinCycle (0.25f * clampf (d1 / kSyncFade, -1.0f, 1.0f));
                    else flipG[j] = pm >= wf.c ? -1.0f : 1.0f;
                }
                float idx = wp * (float) Wavetable::N;
                int i0 = (int) idx;
                float f = idx - (float) i0;
                if (i0 >= Wavetable::N) { i0 -= Wavetable::N; }
                if (i0 < 0) { i0 = 0; f = 0.0f; }
                fr[j] = f;

                const float fpos = clampf (basePos + wtOff[lane], 0.0f, 1.0f) * (float) (F - 1);
                int f0 = (int) fpos;
                if (f0 > F - 1) f0 = F - 1;
                const float ff = fpos - (float) f0;
                const int f1 = std::min (F - 1, f0 + 1);

                if ((anySync && syncA > 0.0f) || quantOn)
                {
                    // value of the bright level at an arbitrary phase (linear, frame-interpolated)
                    auto T = [&] (float phs) {
                        phs -= std::floor (phs);
                        const float x = phs * (float) Wavetable::N;
                        int k = (int) x; const float u = x - (float) k;
                        if (k >= Wavetable::N) k -= Wavetable::N;
                        const float* d0 = table->levelData (f0, lvB) + 1 + k;
                        float v = d0[0] + (d0[1] - d0[0]) * u;
                        if (ff > 0.0f) { const float* d1 = table->levelData (f1, lvB) + 1 + k; v += (d1[0] + (d1[1] - d1[0]) * u - v) * ff; }
                        return v;
                    };
                    const float pm = ph[j];
                    if (anySync && syncA > 0.0f)
                    {
                        // a short raised-cosine dip (kSyncFade samples each side) around the
                        // master reset removes the value and slope discontinuities of hard sync
                        const float d = std::min (pm, 1.0f - pm) / std::max (1e-9f, laneInc);
                        if (d < kSyncFade)
                            win[j] = 0.5f - 0.5f * std::cos (kPiF * d / kSyncFade);
                    }
                    if (quantOn)
                    {
                        const float dt = laneInc * wq.q;
                        const float u = pm * wq.q - std::floor (pm * wq.q);
                        if (u < dt) corr[j] = 0.5f * (T (wp) - T (wp - 1.0f / wq.q)) * polyBlep (u, dt);
                        else if (u > 1.0f - dt) corr[j] = 0.5f * (T (wp + 1.0f / wq.q) - T (wp)) * polyBlep (u, dt);
                    }
                }

                const float* b0 = table->levelData (f0, lvB) + i0;
                if (ff > 0.0f)
                {
                    const float* b1 = table->levelData (f1, lvB) + i0;
                    for (int k = 0; k < 4; ++k) P0[k][j] = b0[k] + (b1[k] - b0[k]) * ff;
                }
                else
                    for (int k = 0; k < 4; ++k) P0[k][j] = b0[k];

                if (needDark)
                {
                    if (darkSilent)
                        for (int k = 0; k < 4; ++k) P1[k][j] = 0.0f;
                    else
                    {
                        const float* d0 = table->levelData (f0, lvD) + i0;
                        if (ff > 0.0f)
                        {
                            const float* d1 = table->levelData (f1, lvD) + i0;
                            for (int k = 0; k < 4; ++k) P1[k][j] = d0[k] + (d1[k] - d0[k]) * ff;
                        }
                        else
                            for (int k = 0; k < 4; ++k) P1[k][j] = d0[k];
                    }
                }
            }

            // SIMD interpolation across 4 lanes
            const V x0 = V::fromRawArray (P0[0]), x1 = V::fromRawArray (P0[1]),
                    x2 = V::fromRawArray (P0[2]), x3 = V::fromRawArray (P0[3]);
            const V vf = V::fromRawArray (fr);
            V y;
            if (hermB)
            {
                const V c1 = V::expand (0.5f) * (x2 - x0);
                const V c2 = x0 - V::expand (2.5f) * x1 + V::expand (2.0f) * x2 - V::expand (0.5f) * x3;
                const V c3 = V::expand (0.5f) * (x3 - x0) + V::expand (1.5f) * (x1 - x2);
                y = ((c3 * vf + c2) * vf + c1) * vf + x1;
            }
            else
                y = x1 + (x2 - x1) * vf;

            if (needDark)
            {
                const V d0 = V::fromRawArray (P1[0]), d1 = V::fromRawArray (P1[1]),
                        d2 = V::fromRawArray (P1[2]), d3 = V::fromRawArray (P1[3]);
                V yd;
                if (hermD)
                {
                    const V c1 = V::expand (0.5f) * (d2 - d0);
                    const V c2 = d0 - V::expand (2.5f) * d1 + V::expand (2.0f) * d2 - V::expand (0.5f) * d3;
                    const V c3 = V::expand (0.5f) * (d3 - d0) + V::expand (1.5f) * (d1 - d2);
                    yd = ((c3 * vf + c2) * vf + c1) * vf + d1;
                }
                else
                    yd = d1 + (d2 - d1) * vf;
                y = y + (yd - y) * V::expand (wD);
            }

            if (quantBlep) y = y + V::fromRawArray (corr);
            if (anySync) y = y * V::fromRawArray (win);
            if (outWarp || outWarp2)
            {
                y.copyToRawArray (ys);
                for (int j = 0; j < 4; ++j)
                {
                    float v = ys[j];
                    if (outWarp) v = warpOutput (v, ws, m, flipG[j]);
                    if (outWarp2) v = warpOutput (v, ws2, m, flipG[j]);
                    ys2[j] = v;
                }
                y = V::fromRawArray (ys2);
            }

            accL = accL + y * V::fromRawArray (gainL + 4 * g);
            accR = accR + y * V::fromRawArray (gainR + 4 * g);
            accM = accM + y * V::fromRawArray (gainM + 4 * g);
        }
        outL[i] = accL.sum();
        outR[i] = accR.sum();
        if (mono != nullptr) mono[i] = accM.sum();
    }
}
void WavetableOsc::renderSingle (const Wavetable& table, int n, const float* inc, const float* pos,
                                 float* outL, float* outR, float* mono) noexcept
{
    const int F = table.numFrames();
    float ph = phase[0];
    const float r = ratio[0], gl = gainL[0], gr = gainR[0], gm = gainM[0];
    const float off = wtOff[0];
    for (int i = 0; i < n; ++i)
    {
        ph += inc[i] * r;
        if (ph >= 1.0f) ph -= 1.0f;
        const float mi = inc[i] * maxRatio;
        if (mi != mipInc) { mipInc = mi; mipCache = chooseMip (mi); }
        const MipChoice mc = mipCache;
        float y = 0.0f;
        if (! mc.silent || mc.wDark < 1.0f)
        {
            float idx = ph * (float) Wavetable::N;
            int i0 = (int) idx;
            const float f = idx - (float) i0;
            if (i0 >= Wavetable::N) i0 -= Wavetable::N;
            const float basePos = clampf ((pos != nullptr ? pos[i] : 0.0f) + off, 0.0f, 1.0f);
            const float fpos = basePos * (float) (F - 1);
            int f0 = std::min (F - 1, (int) fpos);
            const float ff = fpos - (float) f0;
            const int f1 = std::min (F - 1, f0 + 1);
            auto readLevel = [&] (int lv) {
                const float* a = table.levelData (f0, lv) + i0;
                float x0 = a[0], x1 = a[1], x2 = a[2], x3 = a[3];
                if (ff > 0.0f)
                {
                    const float* b = table.levelData (f1, lv) + i0;
                    x0 += (b[0] - x0) * ff; x1 += (b[1] - x1) * ff; x2 += (b[2] - x2) * ff; x3 += (b[3] - x3) * ff;
                }
                if (Wavetable::useHermite (lv))
                {
                    const float c1 = 0.5f * (x2 - x0);
                    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
                    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
                    return ((c3 * f + c2) * f + c1) * f + x1;
                }
                return x1 + (x2 - x1) * f;
            };
            if (! mc.silent) y = readLevel (mc.level);
            if (mc.wDark > 0.0f)
            {
                const float yd = mc.level + 1 < Wavetable::NumLevels ? readLevel (mc.level + 1) : 0.0f;
                y += (yd - y) * mc.wDark;
            }
            lastPosition = basePos - off;
        }
        outL[i] = y * gl;
        outR[i] = y * gr;
        if (mono != nullptr) mono[i] = y * gm;
    }
    phase[0] = ph;
}
} // namespace prism::dsp
