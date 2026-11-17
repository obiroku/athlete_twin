#include "FxModules.h"
#include "Halfband.h"
#include "../Params/Params.h"
#include <array>

namespace prism::dsp
{
namespace
{
// ============================================================== Distortion
class Distortion final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& o : os) o.reset(); tone[0].reset(); tone[1].reset(); dc[0].reset(); dc[1].reset(); held[0] = held[1] = 0; crushPh[0] = crushPh[1] = 0; }
    void update (const float* r, const FxContext&) override
    {
        mode = (int) std::lround (r[P::DistBase + P::dist_mode]);
        drive = dbToGain (r[P::DistBase + P::dist_drive]);
        mix = r[P::DistBase + P::dist_mix];
        bits = r[P::DistBase + P::dist_bits];
        crushInc = std::min (1.0f, r[P::DistBase + P::dist_downsample] / (float) sr);
        const float t = r[P::DistBase + P::dist_tone];
        const float fc = 800.0f * std::pow (25.0f, t); // 800 Hz .. 20 kHz
        tone[0].setLP (fc, sr); tone[1].setLP (fc, sr);
        dc[0].setLP (8.0f, sr); dc[1].setLP (8.0f, sr);
        makeup = 1.0f / std::max (1.0f, std::pow (drive, 0.6f));
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        const int m = mode;
        const float g = drive;
        auto shape = [m] (float x) {
            switch (m)
            {
                case 0: return tanhFast (x);
                case 1: return clampf (x, -1.0f, 1.0f);
                case 2: { float y = (x + 1.0f) * 0.25f; y -= std::floor (y); return 1.0f - 4.0f * std::abs (y - 0.5f); }
                case 3: return tanhFast (x + 0.25f) - 0.2449f + (x < 0 ? 0.15f * tanhFast (x) : 0.0f);
                default: return x;
            }
        };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float x = ch[c][i];
                float y;
                if (m == 4)
                {
                    bool latch = false;
                    crushPh[c] += crushInc;
                    if (crushPh[c] >= 1.0f) { crushPh[c] -= 1.0f; latch = true; }
                    if (latch || crushInc >= 1.0f) held[c] = x * g;
                    const float q = std::pow (2.0f, bits - 1.0f);
                    y = clampf (std::round (held[c] * q) / q, -1.0f, 1.0f);
                }
                else y = os[c].process (x * g, shape);
                y = dc[c].hp (y);
                y = tone[c].lp (y) * makeup;
                ch[c][i] = x + (y - x) * mix;
            }
    }
    double tailSeconds() const override { return 0.05; }
private:
    double sr = 48000;
    int mode = 0;
    float drive = 1, mix = 1, bits = 12, crushInc = 1, crushPh[2] {}, makeup = 1;
    float held[2] {};
    Oversampled2x os[2];
    OnePole tone[2], dc[2];
};

// ============================================================== Tape
class Tape final : public FxModule
{
public:
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& d : dl) d.prepare ((int) (0.05 * sr));
        reset();
    }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : hissF) f.reset();
        rng.reseed (0x7A9E);
        wowPh = 0; flPh = 0; drift = 0; driftT = 0;
    }
    void update (const float* r, const FxContext&) override
    {
        wowRate = r[P::TapeBase + P::tape_wow_rate];
        wowDepth = r[P::TapeBase + P::tape_wow_depth];
        flutter = r[P::TapeBase + P::tape_flutter];
        sat = r[P::TapeBase + P::tape_sat];
        hiss = r[P::TapeBase + P::tape_hiss];
        const float age = r[P::TapeBase + P::tape_age];
        mix = r[P::TapeBase + P::tape_mix];
        const float lpf = 18000.0f * std::pow (3500.0f / 18000.0f, age);
        const float hpf = 15.0f * std::pow (10.0f, age);
        for (auto& f : lp) f.setLP (lpf, sr);
        for (auto& f : hp) f.setLP (hpf, sr);
        for (auto& f : hissF) f.setLP (6000.0f, sr);
        satG = 1.0f + 3.0f * sat;
    }
    void process (float* L, float* R, int n) override
    {
        const float base = (float) (0.006 * sr);
        for (int i = 0; i < n; ++i)
        {
            wowPh += wowRate / (float) sr; if (wowPh >= 1.0f) wowPh -= 1.0f;
            flPh += 9.3f / (float) sr; if (flPh >= 1.0f) flPh -= 1.0f;
            // slow random drift of the wow
            driftT += 0.7f / (float) sr;
            if (driftT >= 1.0f) { driftT -= 1.0f; driftTarget = rng.bipolar(); }
            drift += (driftTarget - drift) * 0.00005f;
            const float wow = (sinCycle (wowPh) * 0.8f + drift * 0.4f) * wowDepth * (float) (0.0025 * sr);
            const float fl = sinCycle (flPh) * flutter * (float) (0.00025 * sr);
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                dl[c].push (x);
                float y = dl[c].read (base + wow + (c == 0 ? fl : -fl * 0.8f));
                y = tanhFast (y * satG) / tanhFast (satG) * (1.0f - 0.25f * sat) + y * 0.25f * sat;
                y = lp[c].lp (y);
                y = y - hp[c].lp (y);
                if (hiss > 0.0f) y += hissF[c].lp (rng.bipolar()) * hiss * 0.02f;
                ch[c][i] = x + (y - x) * mix;
            }
        }
    }
    double tailSeconds() const override { return 0.1; }
private:
    double sr = 48000;
    DelayLine dl[2];
    OnePole lp[2], hp[2], hissF[2];
    Rng rng;
    float wowRate = 0.6f, wowDepth = 0.2f, flutter = 0.1f, sat = 0.3f, satG = 1, hiss = 0, mix = 1;
    float wowPh = 0, flPh = 0, drift = 0, driftT = 0, driftTarget = 0;
};

// ============================================================== Dimension
class Dimension final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (0.04 * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); }
    void update (const float* r, const FxContext&) override
    {
        const float size = 0.5f + r[P::DimBase + P::dim_size];
        mix = r[P::DimBase + P::dim_mix];
        const float ms[4] = { 7.3f, 13.1f, 9.7f, 11.9f };
        for (int i = 0; i < 4; ++i) tap[i] = ms[i] * size * 0.001f * (float) sr;
    }
    void process (float* L, float* R, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            dl[0].push (l); dl[1].push (r);
            const float a = dl[0].read (tap[0]), b = dl[1].read (tap[1]), c = dl[1].read (tap[2]), d = dl[0].read (tap[3]);
            const float wl = 0.5f * (a - 0.7f * b) + 0.25f * d;
            const float wr = 0.5f * (c - 0.7f * d) + 0.25f * a;
            L[i] = l + wl * mix;
            R[i] = r + wr * mix;
        }
    }
    double tailSeconds() const override { return 0.05; }
private:
    double sr = 48000;
    DelayLine dl[2];
    float tap[4] {}, mix = 0.5f;
};

// ============================================================== Chorus
class Chorus final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (0.06 * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); ph = 0; fbL = fbR = 0; }
    void update (const float* r, const FxContext&) override
    {
        rate = r[P::ChorusBase + P::chorus_rate];
        depth = r[P::ChorusBase + P::chorus_depth];
        fb = r[P::ChorusBase + P::chorus_fb];
        mix = r[P::ChorusBase + P::chorus_mix];
    }
    void process (float* L, float* R, int n) override
    {
        const float base = (float) (0.012 * sr), dep = depth * (float) (0.005 * sr);
        for (int i = 0; i < n; ++i)
        {
            ph += rate / (float) sr; if (ph >= 1.0f) ph -= 1.0f;
            const float l = L[i], r = R[i];
            dl[0].push (l + fbL * fb); dl[1].push (r + fbR * fb);
            float wl = 0, wr = 0;
            for (int v = 0; v < 3; ++v)
            {
                const float o = (float) v / 3.0f;
                wl += dl[0].read (base + dep * (0.5f + 0.5f * sinCycle (ph + o)));
                wr += dl[1].read (base + dep * (0.5f + 0.5f * sinCycle (ph + o + 0.25f)));
            }
            wl *= 0.4f; wr *= 0.4f;
            fbL = wl; fbR = wr;
            L[i] = l * (1.0f - 0.5f * mix) + wl * mix;
            R[i] = r * (1.0f - 0.5f * mix) + wr * mix;
        }
    }
    double tailSeconds() const override { return 0.06 + 0.5 * fb; }
private:
    double sr = 48000;
    DelayLine dl[2];
    float ph = 0, rate = 0.5f, depth = 0.5f, fb = 0.1f, mix = 0.5f, fbL = 0, fbR = 0;
};

// ============================================================== Phaser
class Phaser final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& a : z) a.fill (0.0f); ph = 0; last[0] = last[1] = 0; }
    void update (const float* r, const FxContext&) override
    {
        stages = std::max (2, std::min (12, (int) std::lround (r[P::PhaserBase + P::phaser_stages])));
        rate = r[P::PhaserBase + P::phaser_rate];
        depth = r[P::PhaserBase + P::phaser_depth];
        fb = r[P::PhaserBase + P::phaser_fb];
        mix = r[P::PhaserBase + P::phaser_mix];
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            ph += rate / (float) sr; if (ph >= 1.0f) ph -= 1.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * sinCycle (ph + 0.25f * (float) c);
                const float fc = 200.0f * std::pow (20.0f, depth * lfo);
                const float t = fastTan (kPiF * std::min (fc, (float) sr * 0.45f) / (float) sr);
                const float a = (t - 1.0f) / (t + 1.0f);
                const float x = ch[c][i];
                float y = x + fb * last[c];
                for (int s = 0; s < stages; ++s)
                {
                    const float o = a * y + z[c][(size_t) s];
                    z[c][(size_t) s] = y - a * o;
                    y = o;
                }
                last[c] = tanhFast (y);
                ch[c][i] = x * (1.0f - 0.5f * mix) + y * 0.5f * mix;
            }
        }
    }
    double tailSeconds() const override { return 0.05 + 0.3 * std::abs (fb); }
private:
    double sr = 48000;
    std::array<std::array<float, 12>, 2> z {};
    int stages = 4;
    float ph = 0, rate = 0.3f, depth = 0.6f, fb = 0.3f, mix = 0.5f, last[2] {};
};

// ============================================================== Flanger
class Flanger final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (0.02 * sr)); reset(); }
    void reset() override { for (auto& d : dl) d.reset(); ph = 0; last[0] = last[1] = 0; }
    void update (const float* r, const FxContext&) override
    {
        rate = r[P::FlangerBase + P::flanger_rate];
        depth = r[P::FlangerBase + P::flanger_depth];
        fb = r[P::FlangerBase + P::flanger_fb];
        mix = r[P::FlangerBase + P::flanger_mix];
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        const float mn = (float) (0.0003 * sr), mx = (float) (0.0003 * sr + depth * 0.007 * sr);
        for (int i = 0; i < n; ++i)
        {
            ph += rate / (float) sr; if (ph >= 1.0f) ph -= 1.0f;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * sinCycle (ph + 0.25f * (float) c);
                const float x = ch[c][i];
                dl[c].push (x + fb * last[c]);
                const float d = dl[c].readCubic (mn + (mx - mn) * lfo);
                last[c] = tanhFast (d);
                ch[c][i] = x * (1.0f - 0.5f * mix) + d * 0.5f * mix;
            }
        }
    }
    double tailSeconds() const override { return 0.05 + 0.3 * std::abs (fb); }
private:
    double sr = 48000;
    DelayLine dl[2];
    float ph = 0, rate = 0.2f, depth = 0.5f, fb = 0.5f, mix = 0.5f, last[2] {};
};

// ============================================================== Rotary
class Rotary final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (0.01 * sr)); reset(); }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        xo[0].reset(); xo[1].reset();
        hornPh = 0; drumPh = 0.3f;
        hornSpeed = slowH; drumSpeed = slowD;
    }
    void update (const float* r, const FxContext& c) override
    {
        bool fast = r[P::RotaryBase + P::rotary_fast] >= 0.5f;
        if (r[P::RotaryBase + P::rotary_mw] >= 0.5f && c.modWheel > 0.5f) fast = ! fast;
        targetH = fast ? fastH : slowH;
        targetD = fast ? fastD : slowD;
        const float accel = r[P::RotaryBase + P::rotary_accel];
        // seconds to reach the new speed: horn is quicker than the drum
        accH = 1.0f / ((0.4f + 1.6f * (1.0f - accel)) * (float) sr);
        accD = 1.0f / ((1.5f + 4.0f * (1.0f - accel)) * (float) sr);
        balance = r[P::RotaryBase + P::rotary_balance];
        drive = r[P::RotaryBase + P::rotary_drive];
        width = r[P::RotaryBase + P::rotary_width];
        mix = r[P::RotaryBase + P::rotary_mix];
        xo[0].setLP (800.0f, sr); xo[1].setLP (800.0f, sr);
    }
    void process (float* L, float* R, int n) override
    {
        const float dg = 1.0f + 6.0f * drive;
        const float hornDepth = (float) (0.00035 * sr), drumDepth = (float) (0.0002 * sr), base = (float) (0.0015 * sr);
        for (int i = 0; i < n; ++i)
        {
            // speed ramps (exponential approach -> natural acceleration)
            hornSpeed += (targetH - hornSpeed) * accH * 3.0f;
            drumSpeed += (targetD - drumSpeed) * accD * 3.0f;
            hornPh += hornSpeed / (float) sr; if (hornPh >= 1.0f) hornPh -= 1.0f;
            drumPh += drumSpeed / (float) sr; if (drumPh >= 1.0f) drumPh -= 1.0f;
            const float inL = L[i], inR = R[i];
            float x = 0.5f * (inL + inR);
            if (drive > 0.0f) x = tanhFast (x * dg) / tanhFast (dg);
            const float lo = xo[0].lp (x);
            const float hi = x - lo;
            dl[0].push (hi); dl[1].push (lo);
            const float off = 0.25f * width; // mic angle in cycles
            float out[2];
            for (int m = 0; m < 2; ++m)
            {
                const float a = m == 0 ? -off : off;
                const float hs = sinCycle (hornPh + a), hc = sinCycle (hornPh + a + 0.25f);
                const float ds = sinCycle (drumPh + a), dc = sinCycle (drumPh + a + 0.25f);
                const float h = dl[0].read (base + hornDepth * hs) * (1.0f + 0.45f * hc);
                const float d = dl[1].read (base + drumDepth * ds) * (1.0f + 0.2f * dc);
                out[m] = h * (0.3f + 1.4f * balance) * 0.7f + d * (0.3f + 1.4f * (1.0f - balance)) * 0.7f;
            }
            L[i] = inL + (out[0] - inL) * mix;
            R[i] = inR + (out[1] - inR) * mix;
        }
    }
    double tailSeconds() const override { return 0.02; }
private:
    double sr = 48000;
    DelayLine dl[2];
    OnePole xo[2];
    static constexpr float slowH = 0.8f, fastH = 6.8f, slowD = 0.67f, fastD = 5.9f;
    float hornPh = 0, drumPh = 0, hornSpeed = slowH, drumSpeed = slowD, targetH = slowH, targetD = slowD;
    float accH = 0, accD = 0, balance = 0.5f, drive = 0.2f, width = 0.7f, mix = 1.0f;
};

// ============================================================== Delay
class Delay final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; for (auto& d : dl) d.prepare ((int) (4.1 * sr)); reset(); }
    void reset() override
    {
        for (auto& d : dl) d.reset();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        env = 0; wowPh = 0;
        timeS.snap (timeTarget);
    }
    void update (const float* r, const FxContext& c) override
    {
        const bool sync = r[P::DelayBase + P::delay_sync] >= 0.5f;
        const double secs = sync ? syncDivBeats ((int) std::lround (r[P::DelayBase + P::delay_div])) * 60.0 / c.bpm
                                 : r[P::DelayBase + P::delay_time] * 0.001;
        timeTarget = (float) std::min (4.0, std::max (0.001, secs)) * (float) sr;
        timeS.setLen ((int) (0.08 * sr));
        if (first) { timeS.snap (timeTarget); first = false; }
        timeS.set (timeTarget);
        pingpong = r[P::DelayBase + P::delay_pingpong] >= 0.5f;
        fb = r[P::DelayBase + P::delay_fb];
        for (auto& f : hp) f.setLP (r[P::DelayBase + P::delay_hp], sr);
        for (auto& f : lp) f.setLP (r[P::DelayBase + P::delay_lp], sr);
        wow = r[P::DelayBase + P::delay_wow];
        duck = r[P::DelayBase + P::delay_duck];
        mix = r[P::DelayBase + P::delay_mix];
    }
    void process (float* L, float* R, int n) override
    {
        const float envA = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
        const float envR = 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const float t = timeS.next();
            wowPh += 0.5f / (float) sr; if (wowPh >= 1.0f) wowPh -= 1.0f;
            const float d = std::max (1.0f, t + wow * sinCycle (wowPh) * (float) (0.003 * sr));
            const float inL = L[i], inR = R[i];
            const float a = std::max (std::abs (inL), std::abs (inR));
            env += (a - env) * (a > env ? envA : envR);
            float wl = dl[0].read (d), wr = dl[1].read (d);
            float fl = wl - hp[0].lp (wl); fl = lp[0].lp (fl);
            float fr = wr - hp[1].lp (wr); fr = lp[1].lp (fr);
            if (pingpong)
            {
                dl[0].push (0.5f * (inL + inR) + fr * fb);
                dl[1].push (fl * fb);
            }
            else
            {
                dl[0].push (inL + fl * fb);
                dl[1].push (inR + fr * fb);
            }
            const float dg = 1.0f - duck * std::min (1.0f, env * 2.0f);
            L[i] = inL + wl * mix * dg;
            R[i] = inR + wr * mix * dg;
        }
    }
    double tailSeconds() const override
    {
        const double secs = timeTarget / sr;
        if (fb < 1e-3f) return secs + 0.05;
        return secs * std::min (200.0, std::log (1e-4) / std::log ((double) fb)) + 0.05;
    }
private:
    double sr = 48000;
    DelayLine dl[2];
    OnePole hp[2], lp[2];
    Ramp timeS;
    bool first = true, pingpong = false;
    float timeTarget = 1000, fb = 0.35f, wow = 0, duck = 0, mix = 0.3f, env = 0, wowPh = 0;
};

// ============================================================== Reverb (diffused FDN + shimmer)
class Reverb final : public FxModule
{
public:
    static constexpr int MaxLines = 16;
    void prepare (double s, int) override
    {
        sr = s;
        for (auto& d : lines) d.prepare ((int) (0.27 * sr) + 64);
        for (auto& d : pre) d.prepare ((int) (0.26 * sr) + 8);
        for (auto& c : diff) for (auto& d : c) d.prepare ((int) (0.02 * sr) + 8);
        shim.prepare ((int) (0.2 * sr));
        grain = (int) (0.07 * sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : lines) d.reset();
        for (auto& d : pre) d.reset();
        for (auto& c : diff) for (auto& d : c) d.reset();
        shim.reset();
        for (auto& f : damp) f.reset();
        lc[0].reset(); lc[1].reset();
        shPh = 0;
        for (int k = 0; k < MaxLines; ++k) modPh[k] = (float) k / MaxLines;
    }
    void update (const float* r, const FxContext&) override
    {
        n = std::lround (r[P::ReverbBase + P::reverb_lines]) >= 1 ? 16 : 8;
        size = 0.4f + 1.2f * r[P::ReverbBase + P::reverb_size];
        decay = r[P::ReverbBase + P::reverb_decay];
        const float dmp = r[P::ReverbBase + P::reverb_damp];
        preDelay = std::max (1.0f, r[P::ReverbBase + P::reverb_predelay] * (float) sr);
        modAmt = r[P::ReverbBase + P::reverb_mod];
        width = r[P::ReverbBase + P::reverb_width];
        lc[0].setLP (r[P::ReverbBase + P::reverb_lowcut], sr); lc[1].setLP (r[P::ReverbBase + P::reverb_lowcut], sr);
        freeze = r[P::ReverbBase + P::reverb_freeze] >= 0.5f;
        shimmer = r[P::ReverbBase + P::reverb_shimmer];
        mix = r[P::ReverbBase + P::reverb_mix];
        static const float ms[MaxLines] = { 43.1f, 97.3f, 53.3f, 119.3f, 64.9f, 83.7f, 47.9f, 137.1f,
                                            71.3f, 103.7f, 59.7f, 127.9f, 77.9f, 90.1f, 111.1f, 149.3f };
        const float dampFc = 16000.0f * std::pow (1200.0f / 16000.0f, dmp);
        for (int i = 0; i < MaxLines; ++i)
        {
            len[i] = ms[i] * 0.001f * size * (float) sr;
            const double rt = std::max (0.05, (double) decay);
            gain[i] = freeze ? 1.0f : (float) std::pow (10.0, -3.0 * (double) len[i] / (rt * sr));
            damp[i].setLP (freeze ? 22000.0f : dampFc, sr);
            modRate[i] = (0.17f + 0.09f * (float) i) / (float) sr;
        }
        static const float dms[2][4] = { { 4.77f, 3.59f, 12.73f, 9.30f }, { 4.92f, 3.71f, 12.51f, 9.68f } };
        const float ds = 0.6f + 0.4f * std::min (1.0f, size);
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) diffLen[c][k] = dms[c][k] * 0.001f * ds * (float) sr;
    }
    void process (float* L, float* R, int nn) override
    {
        const float norm = 1.0f / std::sqrt ((float) n);
        const float modDepth = freeze ? 0.0f : modAmt * (float) (0.0012 * sr);
        const float shimAmt = freeze ? 0.0f : shimmer;
        static const float dg[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int i = 0; i < nn; ++i)
        {
            const float inL = L[i], inR = R[i];
            float x[2] = { inL, inR };
            for (int c = 0; c < 2; ++c)
            {
                pre[c].push (x[c]);
                float v = pre[c].read (preDelay);
                v = v - lc[c].lp (v);
                // input diffusion: four series allpasses per channel
                for (int k = 0; k < 4; ++k)
                {
                    const float d = diff[c][k].read (diffLen[c][k]);
                    const float w = v + dg[k] * d;
                    diff[c][k].push (w);
                    v = d - dg[k] * w;
                }
                x[c] = freeze ? 0.0f : v;
            }

            float y[MaxLines];
            for (int k = 0; k < n; ++k)
            {
                float d = len[k];
                if (modDepth > 0.0f)
                {
                    modPh[k] += modRate[k]; if (modPh[k] >= 1.0f) modPh[k] -= 1.0f;
                    d += modDepth * sinCycle (modPh[k]);
                }
                // frozen: whole-sample reads keep the tank lossless
                float v = (freeze ? lines[k].readInt ((int) std::lround (d)) : lines[k].readCubic (d)) * gain[k];
                if (! freeze) v = damp[k].lp (v);
                y[k] = v;
            }
            // decorrelated stereo taps
            float oL = 0, oR = 0;
            for (int k = 0; k < n; ++k)
            {
                oL += ((k & 1) ? -y[k] : y[k]);
                oR += ((k & 2) ? -y[k] : y[k]);
            }
            oL *= norm * 1.2f; oR *= norm * 1.2f;

            float sh = 0.0f;
            if (shimAmt > 0.0f)
            {
                shim.push (0.5f * (oL + oR));
                shPh += 1.0f; if (shPh >= (float) grain) shPh -= (float) grain;
                const float g = (float) grain;
                const float p1 = shPh, p2 = std::fmod (shPh + g * 0.5f, g);
                const float d1 = 2.0f + g - p1, d2 = 2.0f + g - p2;
                const float w1 = 0.5f - 0.5f * sinCycle (p1 / g + 0.25f);
                const float w2 = 0.5f - 0.5f * sinCycle (p2 / g + 0.25f);
                sh = tanhFast (shim.read (d1) * w1 + shim.read (d2) * w2) * shimAmt * 0.5f;
            }

            // Hadamard feedback matrix (fast Walsh-Hadamard transform)
            for (int h = 1; h < n; h <<= 1)
                for (int a = 0; a < n; a += 2 * h)
                    for (int b = a; b < a + h; ++b)
                    {
                        const float u = y[b], v = y[b + h];
                        y[b] = u + v; y[b + h] = u - v;
                    }
            for (int k = 0; k < n; ++k)
            {
                const float in = ((k & 1) ? x[1] : x[0]) * ((k & 2) ? -0.5f : 0.5f) + sh * ((k & 4) ? -0.35f : 0.35f);
                lines[k].push (y[k] * norm + in);
            }
            const float m = 0.5f * (oL + oR), sd = 0.5f * (oL - oR) * width;
            L[i] = inL * (1.0f - 0.3f * mix) + (m + sd) * mix;
            R[i] = inR * (1.0f - 0.3f * mix) + (m - sd) * mix;
        }
    }
    double tailSeconds() const override { return freeze ? 30.0 : std::min (30.0, (double) decay * 1.3 + preDelay / sr + 0.15); }
    bool infiniteTail() const override { return freeze; }
private:
    double sr = 48000;
    int n = 16, grain = 3360;
    DelayLine lines[MaxLines], pre[2], diff[2][4], shim;
    OnePole damp[MaxLines], lc[2];
    float len[MaxLines] {}, gain[MaxLines] {}, modPh[MaxLines] {}, modRate[MaxLines] {}, diffLen[2][4] {};
    float size = 1, decay = 3, preDelay = 1, modAmt = 0.3f, width = 1, shimmer = 0, mix = 0.3f, shPh = 0;
    bool freeze = false;
};

// ============================================================== Compressor
class Compressor final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { envDb = -120.0f; }
    void update (const float* r, const FxContext&) override
    {
        thr = r[P::CompBase + P::comp_thresh];
        ratio = r[P::CompBase + P::comp_ratio];
        att = std::exp (-1.0f / (std::max (0.05f, r[P::CompBase + P::comp_attack]) * 0.001f * (float) sr));
        rel = std::exp (-1.0f / (std::max (1.0f, r[P::CompBase + P::comp_release]) * 0.001f * (float) sr));
        knee = r[P::CompBase + P::comp_knee];
        makeup = dbToGain (r[P::CompBase + P::comp_makeup]);
        mix = r[P::CompBase + P::comp_mix];
    }
    void process (float* L, float* R, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::abs (L[i]), std::abs (R[i]));
            const float db = gainToDb (a);
            envDb = db > envDb ? db + (envDb - db) * att : db + (envDb - db) * rel;
            const float over = envDb - thr;
            float gr = 0.0f;
            if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
                gr = (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
            else if (over > 0.0f)
                gr = (1.0f / ratio - 1.0f) * over;
            const float g = dbToGain (gr) * makeup;
            L[i] = L[i] + (L[i] * g - L[i]) * mix;
            R[i] = R[i] + (R[i] * g - R[i]) * mix;
        }
    }
    double tailSeconds() const override { return 0.05; }
private:
    double sr = 48000;
    float thr = -18, ratio = 4, att = 0.9f, rel = 0.99f, knee = 6, makeup = 1, mix = 1, envDb = -120;
};

// ============================================================== Multiband (3-band up/down)
class Multiband final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override
    {
        for (auto& b : lo) b.reset();
        for (auto& b : hi) b.reset();
        for (auto& b : ap) b.reset();
        for (auto& e : env) e = -120.0f;
    }
    void update (const float* r, const FxContext&) override
    {
        depth = r[P::MbBase + P::mb_depth];
        const float time = r[P::MbBase + P::mb_time];
        up = r[P::MbBase + P::mb_up];
        down = r[P::MbBase + P::mb_down];
        mix = r[P::MbBase + P::mb_mix];
        const float lx = r[P::MbBase + P::mb_low_x], hx = r[P::MbBase + P::mb_high_x];
        if (lx != lastLx || hx != lastHx)
        {
            lastLx = lx; lastHx = hx;
            // LR4 = two cascaded Butterworth (Q 0.7071) sections
            lo[0].lowpass (lx, 0.70710678, sr); lo[1].lowpass (lx, 0.70710678, sr);
            lo[2].highpass (lx, 0.70710678, sr); lo[3].highpass (lx, 0.70710678, sr);
            hi[0].lowpass (hx, 0.70710678, sr); hi[1].lowpass (hx, 0.70710678, sr);
            hi[2].highpass (hx, 0.70710678, sr); hi[3].highpass (hx, 0.70710678, sr);
            ap[0].lowpass (hx, 0.70710678, sr); ap[1].lowpass (hx, 0.70710678, sr);
            ap[2].highpass (hx, 0.70710678, sr); ap[3].highpass (hx, 0.70710678, sr);
        }
        const float t = 0.2f + 4.0f * time; // time scale
        att = std::exp (-1.0f / (0.003f * t * (float) sr));
        rel = std::exp (-1.0f / (0.08f * t * (float) sr));
    }
    void process (float* L, float* R, int n) override
    {
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int c = 0; c < 2; ++c)
            {
                const float x = ch[c][i];
                const float l = lo[1].tick (lo[0].tick (x, c), c);
                const float rest = lo[3].tick (lo[2].tick (x, c), c);
                const float m = hi[1].tick (hi[0].tick (rest, c), c);
                const float h = hi[3].tick (hi[2].tick (rest, c), c);
                // allpass-compensate the low band at the high crossover
                const float la = ap[1].tick (ap[0].tick (l, c), c) + ap[3].tick (ap[2].tick (l, c), c);
                band[0][c] = la; band[1][c] = m; band[2][c] = h;
            }
            float outL = 0, outR = 0;
            for (int b = 0; b < 3; ++b)
            {
                const float a = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                const float db = gainToDb (a);
                env[b] = db > env[b] ? db + (env[b] - db) * att : db + (env[b] - db) * rel;
                float g = 0.0f;
                const float tDown = -22.0f, tUp = -42.0f;
                if (env[b] > tDown) g -= (env[b] - tDown) * 0.75f * down * depth;
                if (env[b] < tUp && env[b] > -90.0f) g += std::min (18.0f, (tUp - env[b]) * 0.6f * up * depth);
                const float gl = dbToGain (g);
                outL += band[b][0] * gl; outR += band[b][1] * gl;
            }
            ch[0][i] = ch[0][i] + (outL - ch[0][i]) * mix;
            ch[1][i] = ch[1][i] + (outR - ch[1][i]) * mix;
        }
    }
    double tailSeconds() const override { return 0.1; }
private:
    double sr = 48000;
    Biquad lo[4], hi[4], ap[4];
    float env[3] { -120, -120, -120 };
    float depth = 0.5f, up = 0.5f, down = 0.5f, mix = 1, att = 0.9f, rel = 0.99f, lastLx = -1, lastHx = -1;
};

// ============================================================== EQ
class Eq final : public FxModule
{
public:
    void prepare (double s, int) override { sr = s; reset(); }
    void reset() override { for (auto& b : bq) b.reset(); }
    void update (const float* r, const FxContext&) override
    {
        float v[9];
        for (int i = 0; i < 9; ++i) v[i] = r[P::EqBase + 1 + i];
        bool same = true;
        for (int i = 0; i < 9; ++i) if (v[i] != last[i]) same = false;
        if (same) return;
        std::copy (v, v + 9, last);
        bq[0].lowShelf (v[0], v[1], sr); on[0] = std::abs (v[1]) > 0.01f;
        bq[1].peak (v[2], v[4], v[3], sr); on[1] = std::abs (v[3]) > 0.01f;
        bq[2].highShelf (v[5], v[6], sr); on[2] = std::abs (v[6]) > 0.01f;
        bq[3].highpass (v[7], 0.70710678, sr); on[3] = v[7] > 20.5f;
        bq[4].lowpass (v[8], 0.70710678, sr); on[4] = v[8] < 19900.0f;
    }
    void process (float* L, float* R, int n) override
    {
        for (int b = 0; b < 5; ++b)
        {
            if (! on[b]) continue;
            for (int i = 0; i < n; ++i) { L[i] = bq[b].tick (L[i], 0); R[i] = bq[b].tick (R[i], 1); }
        }
    }
    double tailSeconds() const override { return 0.05; }
private:
    double sr = 48000;
    Biquad bq[5];
    bool on[5] {};
    float last[9] { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
};
} // namespace

std::unique_ptr<FxModule> createFxModule (int id)
{
    switch (id)
    {
        case 0: return std::make_unique<Distortion>();
        case 1: return std::make_unique<Tape>();
        case 2: return std::make_unique<Dimension>();
        case 3: return std::make_unique<Chorus>();
        case 4: return std::make_unique<Phaser>();
        case 5: return std::make_unique<Flanger>();
        case 6: return std::make_unique<Rotary>();
        case 7: return std::make_unique<Delay>();
        case 8: return std::make_unique<Reverb>();
        case 9: return std::make_unique<Compressor>();
        case 10: return std::make_unique<Multiband>();
        case 11: return std::make_unique<Eq>();
        default: return nullptr;
    }
}
} // namespace prism::dsp
