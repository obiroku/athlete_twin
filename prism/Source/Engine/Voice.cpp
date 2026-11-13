#include "Voice.h"
#include "../DSP/Common.h"

namespace prism
{
using namespace dsp;

int fastIndexForParam (int p)
{
    if (p == P::voice_pitch) return FD_VoicePitch;
    if (p == P::amp_level) return FD_Amp;
    for (int o = 0; o < 2; ++o)
    {
        const int b = o == 0 ? FD_APos : FD_BPos;
        if (p == P::osc (o, P::wt_pos)) return b;
        if (p == P::osc (o, P::pitch)) return b + 1;
        if (p == P::osc (o, P::level)) return b + 2;
        if (p == P::osc (o, P::pan)) return b + 3;
        if (p == P::osc (o, P::warp_amt)) return b + 4;
    }
    if (p == P::sub (P::sub_level)) return FD_SubLevel;
    if (p == P::sub (P::sub_pan)) return FD_SubPan;
    if (p == P::noise (P::noise_level)) return FD_NoiseLevel;
    if (p == P::noise (P::noise_pan)) return FD_NoisePan;
    if (p == P::filt (0, P::f_cutoff)) return FD_Cut1;
    if (p == P::filt (1, P::f_cutoff)) return FD_Cut2;
    return -1;
}

int paramForFastIndex (int fd)
{
    static const int map[NumFast] = {
        P::voice_pitch, P::amp_level,
        P::osc (0, P::wt_pos), P::osc (0, P::pitch), P::osc (0, P::level), P::osc (0, P::pan), P::osc (0, P::warp_amt),
        P::osc (1, P::wt_pos), P::osc (1, P::pitch), P::osc (1, P::level), P::osc (1, P::pan), P::osc (1, P::warp_amt),
        P::sub (P::sub_level), P::sub (P::sub_pan), P::noise (P::noise_level), P::noise (P::noise_pan),
        P::filt (0, P::f_cutoff), P::filt (1, P::f_cutoff) };
    return map[fd];
}

void RoutingTable::build (const PatchExtras& x)
{
    nDepth = nSlow = nFast = nUnison = nGlobal = 0;
    usedSrc = 0;
    nActive = 0;
    const auto& defs = paramDefs();
    for (int s = 0; s < P::NumModSlots; ++s)
    {
        const auto& r = x.routings[(size_t) s];
        if (! r.active() || r.src >= NumSrc || r.dest >= P::NumParams) continue;
        if (! defs[(size_t) r.dest].isModDest()) continue;
        RoutingRef ref;
        ref.slot = s; ref.src = r.src; ref.aux = (r.aux >= 0 && r.aux < NumSrc) ? r.aux : -1;
        ref.dest = r.dest; ref.polarity = r.bipolar; ref.curve = r.curve;
        ref.fast = fastIndexForParam (r.dest);
        usedSrc |= 1u << r.src;
        if (ref.aux >= 0) usedSrc |= 1u << ref.aux;
        activeSlots[nActive++] = s;
        if (r.src == (int) Src::UnisonIndex)
        {
            const bool ok = r.dest == P::voice_pitch
                         || r.dest == P::osc (0, P::wt_pos) || r.dest == P::osc (1, P::wt_pos)
                         || r.dest == P::osc (0, P::pitch) || r.dest == P::osc (1, P::pitch)
                         || r.dest == P::osc (0, P::pan) || r.dest == P::osc (1, P::pan);
            if (ok) unison[nUnison++] = ref;
            continue;
        }
        if (r.dest >= P::ModDepthBase && r.dest < P::ModDepthBase + P::NumModSlots) depth[nDepth++] = ref;
        else if (defs[(size_t) r.dest].scope == Scope::Global) global[nGlobal++] = ref;
        else if (ref.fast >= 0) fast[nFast++] = ref;
        else slow[nSlow++] = ref;
    }
}

// ------------------------------------------------------------------

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& e : env) e.prepare (sr);
    for (auto& l : lfo) l.prepare (sr);
    wander.prepare (sr);
    for (auto& f : filter) f.prepare (sr);
    fadeLen = std::max (1, (int) std::lround (0.003 * sr));
    reset();
}

void Voice::reset() noexcept
{
    state = State::Free;
    for (auto& c : cacheNorm) c = -1.0f;
    for (auto& e : env) e.reset();
    for (auto& f : filter) f.reset();
    for (auto& a : decS) for (auto& d : a) d.reset();
    for (auto& a : decR) for (auto& d : a) d.reset();
    ticksLeft = 0;
}

static inline int iround (float x) noexcept { return (int) (x + (x >= 0.0f ? 0.5f : -0.5f)); }

static inline float convertSrc (float v, bool nativeBi, int8_t pol, float curve) noexcept
{
    if (pol == 1 && ! nativeBi) v = 2.0f * v - 1.0f;
    else if (pol == 0 && nativeBi) v = 0.5f * (v + 1.0f);
    if (curve != 0.0f)
    {
        const float e = std::pow (4.0f, curve);
        v = v < 0.0f ? -std::pow (-v, e) : std::pow (v, e);
    }
    return v;
}

void Voice::start (const RenderContext& ctx, int pos, int n, float vel, int ch, uint64_t ord, Rng& rng,
                   float alt, float glideFrom, bool glide) noexcept
{
    state = State::Active;
    released = false;
    note = n; channel = ch; order = ord;
    velocity = vel;
    relVelocity = 0.5f;
    alternate = alt;
    polyAT = 0; slide = 0; noteBend = 0;

    // fixed number of draws per note -> deterministic regardless of patch
    randomNote = rng.bipolar();
    driftPitchU = rng.bipolar();
    driftCutU = rng.bipolar();
    const uint64_t seed = rng.next();

    const float* row = ctx.normRowAt (pos);
    const auto& defs = paramDefs();
    for (int o = 0; o < 2; ++o)
    {
        const float ph = defs[(size_t) P::osc (o, P::phase)].fromNorm (row[P::osc (o, P::phase)]);
        const float rp = defs[(size_t) P::osc (o, P::rand_phase)].fromNorm (row[P::osc (o, P::rand_phase)]);
        Rng r (mixSeed (seed, 10 + (uint64_t) o));
        osc[o].start (r, ph, rp);
    }
    sub.start (0.0f);
    subFmPhase = 0.0f;
    noise[0].reseed (mixSeed (seed, 20));
    noise[1].reseed (mixSeed (seed, 21));
    for (int l = 0; l < 4; ++l) { lfo[l].reseed (mixSeed (seed, 30 + (uint64_t) l)); }
    wander.reseed (mixSeed (seed, 40));
    {
        Lfo::Settings ws; ws.rateHz = 0.3f; ws.shape = LfoShape::SmoothRandom; ws.bipolar = true;
        wander.set (ws);
        wander.trigger();
        wanderNow = wander.advance (0);
    }

    for (auto& e : env) { e.reset(); }
    timesValid = false;
    for (int f = 0; f < 2; ++f)
        filter[f].forceType ((FilterType) (int) std::lround (defs[(size_t) P::filt (f, P::f_type)].fromNorm (row[P::filt (f, P::f_type)])));
    for (auto& a : decS) for (auto& d : a) d.reset();
    for (auto& a : decR) for (auto& d : a) d.reset();

    pitchTarget = (float) n;
    if (glide) { pitchNow = glideFrom; gliding = std::abs (glideFrom - pitchTarget) > 1e-4f; glideRate = 0.0f; }
    else { pitchNow = pitchTarget; gliding = false; }

    ticksLeft = 0;
    fadeRemain = 0;
    startDelay = 0;
    mseg.trigger();
    for (auto& s : snap) s = 0.0f;
    snap[(int) Src::Velocity] = velocity;
    controlTick (ctx, pos); // settle tick values (env times etc.) before the first sample
    for (auto& e : env) e.noteOn();
    for (auto& l : lfo) l.trigger();
    ticksLeft = 0;
}

void Voice::retrigger (float vel, uint64_t ord) noexcept
{
    velocity = vel;
    order = ord;
    released = false;
    if (state == State::Fading) state = State::Active;
    for (auto& e : env) e.noteOn();
    for (int l = 0; l < 4; ++l) if (tv.lfoActive[l]) lfo[l].trigger();
    mseg.trigger();
}

void Voice::legatoTo (int n, float glideFrom, bool glide) noexcept
{
    note = n;
    pitchTarget = (float) n;
    released = false;
    if (glide) { pitchNow = glideFrom; gliding = true; glideRate = 0.0f; }
    else { pitchNow = pitchTarget; gliding = false; }
}

void Voice::release (float relVel) noexcept
{
    if (state != State::Active || released) return;
    released = true;
    relVelocity = relVel;
    for (auto& e : env) e.noteOff();
    mseg.release();
}

void Voice::beginStealFade() noexcept
{
    if (state != State::Active) return;
    state = State::Fading;
    fadeRemain = fadeLen;
}

// ------------------------------------------------------------------ control tick

void Voice::controlTick (const RenderContext& ctx, int pos) noexcept
{
    const float* row = ctx.normRowAt (pos);
    std::memcpy (slowNorm, row, sizeof (float) * P::NumParams);
    const RoutingTable& rt = *ctx.routes;

    // per-voice constant sources
    snap[(int) Src::Velocity] = velocity;
    snap[(int) Src::RelVelocity] = relVelocity;
    snap[(int) Src::Note] = clampf (((float) note - 60.0f) / 60.0f, -1.0f, 1.0f);
    snap[(int) Src::PolyAT] = polyAT;
    snap[(int) Src::Slide] = slide;
    snap[(int) Src::RandomNote] = randomNote;
    snap[(int) Src::Alternate] = alternate;
    for (int s = 0; s < NumSrc; ++s)
        if (ctx.globalSrc[s] != nullptr) snap[s] = ctx.globalSrc[s][pos];

    for (int l = 0; l < 4; ++l)
        tv.lfoBipolar[l] = std::lround (row[P::lfo (l, P::l_polarity)]) >= 1; // choice norm 0/1 for 2 entries
    auto nativeBi = [this] (int s) {
        if (s >= (int) Src::Lfo1 && s <= (int) Src::Lfo4) return tv.lfoBipolar[s - (int) Src::Lfo1];
        return srcIsBipolar (s);
    };
    auto srcVal = [&] (const RoutingRef& r) {
        float v = convertSrc (snap[r.src], nativeBi (r.src), r.polarity, r.curve);
        if (r.aux >= 0) v *= snap[r.aux];
        return v;
    };

    // depths (mod-on-mod first)
    for (int a = 0; a < rt.nActive; ++a) { const int s = rt.activeSlots[a]; depthVal[s] = row[P::modDepth (s)]; }
    for (int i = 0; i < rt.nDepth; ++i)
    {
        const auto& r = rt.depth[i];
        const float base = row[P::modDepth (r.slot)] * 2.0f - 1.0f;
        depthVal[r.dest - P::ModDepthBase] += base * srcVal (r);
    }
    for (int a = 0; a < rt.nActive; ++a) { const int s = rt.activeSlots[a]; depthVal[s] = clampf (depthVal[s], 0.0f, 1.0f) * 2.0f - 1.0f; }

    for (int i = 0; i < rt.nSlow; ++i)
    {
        const auto& r = rt.slow[i];
        slowNorm[r.dest] += depthVal[r.slot] * srcVal (r);
    }
    for (int i = 0; i < rt.nSlow; ++i)
    {
        const int d = rt.slow[i].dest;
        slowNorm[d] = clampf (slowNorm[d], 0.0f, 1.0f);
    }

    // ---- derived values
    tv.drift = real (P::drift);
    tv.glideTime = real (P::glide_time);
    tv.glideMode = (int) iround (real (P::glide_mode));
    tv.parallel = iround (real (P::filter_routing)) == 1;
    for (int o = 0; o < 2; ++o)
    {
        tv.oscSemis[o] = 12.0f * std::round (real (P::osc (o, P::oct))) + std::round (real (P::osc (o, P::semi)))
                       + std::round (real (P::osc (o, P::fine))) * 0.01f;
        tv.warpMode[o] = (int) iround (real (P::osc (o, P::warp_mode)));
        tv.dest[o] = (int) iround (real (P::osc (o, P::dest)));
        WavetableOsc::Unison u;
        u.count = (int) iround (real (P::osc (o, P::unison)));
        u.detune = real (P::osc (o, P::detune));
        u.blend = real (P::osc (o, P::blend));
        u.spread = real (P::osc (o, P::spread));
        u.wtSpread = real (P::osc (o, P::wt_spread));
        osc[o].setUnison (u);
    }
    for (int o = 0; o < 2; ++o)
    {
        const int w = tv.warpMode[1 - o];
        tv.modulator[o] = w == (int) WarpMode::FMOther || w == (int) WarpMode::AM || w == (int) WarpMode::RM;
    }
    // unison-index routings -> per-lane offsets
    {
        float lp[2] {}, lw[2] {}, lpan[2] {};
        for (int i = 0; i < rt.nUnison; ++i)
        {
            const auto& r = rt.unison[i];
            const float d = depthVal[r.slot];
            if (r.dest == P::voice_pitch) { lp[0] += d * 48.0f; lp[1] += d * 48.0f; }
            for (int o = 0; o < 2; ++o)
            {
                if (r.dest == P::osc (o, P::pitch)) lp[o] += d * 48.0f;
                if (r.dest == P::osc (o, P::wt_pos)) lw[o] += d;
                if (r.dest == P::osc (o, P::pan)) lpan[o] += d * 2.0f;
            }
        }
        for (int o = 0; o < 2; ++o) osc[o].setLaneMod (lp[o], lw[o], lpan[o]);
    }

    tv.subShape = (int) iround (real (P::sub (P::sub_shape)));
    tv.subOct = (int) iround (real (P::sub (P::sub_oct))) + 1;
    tv.subDest = (int) iround (real (P::sub (P::sub_dest)));
    tv.noiseType = (int) iround (real (P::noise (P::noise_type)));
    tv.noiseDest = (int) iround (real (P::noise (P::noise_dest)));
    tv.noiseStereo = real (P::noise (P::noise_stereo)) >= 0.5f;
    tv.noiseTone = real (P::noise (P::noise_tone));
    {
        // tone < 0.5: low-pass from 200 Hz (0) to 20 kHz (0.5); > 0.5: blend in a 1 kHz high-pass
        const float t = std::min (tv.noiseTone, 0.5f) * 2.0f;
        const float fcLow = 200.0f * std::pow (100.0f, t);
        tv.ntLow = 1.0f - std::exp (-kTwoPiF * std::min (fcLow, (float) sr * 0.45f) / (float) sr);
        tv.ntHigh = 1.0f - std::exp (-kTwoPiF * 1000.0f / (float) sr);
    }
    for (int f = 0; f < 2; ++f)
    {
        tv.fOn[f] = real (P::filt (f, P::f_enable)) >= 0.5f;
        filter[f].setType ((FilterType) (int) iround (real (P::filt (f, P::f_type))));
        tv.reso[f] = real (P::filt (f, P::f_reso));
        tv.drive[f] = real (P::filt (f, P::f_drive));
        tv.keytrack[f] = real (P::filt (f, P::f_keytrack));
        tv.vowel[f] = real (P::filt (f, P::f_vowel));
        tv.fmix[f] = real (P::filt (f, P::f_mix));
    }
    for (int e = 0; e < 3; ++e)
    {
        Envelope::Times t;
        t.delay = real (P::env (e, P::e_delay));
        t.attack = real (P::env (e, P::e_attack));
        t.hold = real (P::env (e, P::e_hold));
        t.decay = real (P::env (e, P::e_decay));
        t.sustain = real (P::env (e, P::e_sustain));
        t.release = real (P::env (e, P::e_release));
        t.curveA = real (P::env (e, P::e_curve_a));
        t.curveD = real (P::env (e, P::e_curve_d));
        t.curveR = real (P::env (e, P::e_curve_r));
        const auto& o = lastTimes[e];
        if (! timesValid || t.delay != o.delay || t.attack != o.attack || t.hold != o.hold || t.decay != o.decay
            || t.sustain != o.sustain || t.release != o.release || t.curveA != o.curveA || t.curveD != o.curveD || t.curveR != o.curveR)
        {
            env[e].setTimes (t);
            lastTimes[e] = t;
        }
    }
    timesValid = true;
    velGain = 1.0f - real (P::env (0, P::e_vel_amt)) * (1.0f - velocity);

    for (int l = 0; l < 4; ++l)
    {
        const int mode = (int) iround (real (P::lfo (l, P::l_mode)));
        tv.lfoActive[l] = mode != (int) LfoMode::Free && (rt.usedSrc & (1u << ((int) Src::Lfo1 + l))) != 0;
        if (! tv.lfoActive[l]) continue;
        Lfo::Settings s;
        const bool sync = real (P::lfo (l, P::l_sync)) >= 0.5f;
        s.rateHz = sync ? (float) (ctx.bpm / 60.0 / syncDivBeats ((int) iround (real (P::lfo (l, P::l_div)))))
                        : real (P::lfo (l, P::l_rate));
        s.shape = (LfoShape) (int) iround (real (P::lfo (l, P::l_shape)));
        s.bipolar = tv.lfoBipolar[l];
        s.phaseOffset = real (P::lfo (l, P::l_phase));
        s.delaySec = real (P::lfo (l, P::l_delay));
        s.fadeSec = real (P::lfo (l, P::l_fade));
        s.smooth = real (P::lfo (l, P::l_smooth));
        s.oneShot = mode == (int) LfoMode::Env || real (P::lfo (l, P::l_oneshot)) >= 0.5f;
        lfo[l].set (s);
    }
    tv.msegOn = (rt.usedSrc & (1u << (int) Src::Mseg)) != 0;
    if (tv.msegOn)
    {
        const bool sync = real (P::mseg (P::mseg_sync)) >= 0.5f;
        int div = (int) iround (real (P::mseg (P::mseg_div)));
        div = std::max (3, std::min (21, div)); // 16 bars .. 1/16
        tv.msegLen = sync ? syncDivBeats (div) * 60.0 / ctx.bpm * sr : (double) real (P::mseg (P::mseg_rate)) * sr;
        tv.msegMode = (int) iround (real (P::mseg (P::mseg_mode)));
    }

    if (gliding && glideRate == 0.0f)
    {
        const float dist = std::abs (pitchTarget - pitchNow);
        if (tv.glideTime <= 0.0f) { pitchNow = pitchTarget; gliding = false; }
        else if (tv.glideMode == 0) glideRate = dist / (tv.glideTime * (float) sr);
        else glideRate = 12.0f / (tv.glideTime * (float) sr);
        if (glideRate <= 0.0f) { pitchNow = pitchTarget; gliding = false; }
    }

    ticksLeft = kTick;
}

// ------------------------------------------------------------------ render

void Voice::render (const RenderContext& ctx, int start, int end, float* outL, float* outR) noexcept
{
    int pos = start;
    if (startDelay > 0)
    {
        const int skip = std::min (startDelay, end - pos);
        startDelay -= skip;
        pos += skip;
    }
    while (pos < end && state != State::Free)
    {
        if (ticksLeft <= 0) controlTick (ctx, pos);
        const int len = std::min (end - pos, ticksLeft);
        renderChunk (ctx, pos, len, outL, outR);
        pos += len;
        ticksLeft -= len;
    }
}

void Voice::renderChunk (const RenderContext& ctx, int pos, int len, float* outL, float* outR) noexcept
{
    const RoutingTable& rt = *ctx.routes;
    const uint32_t used = rt.usedSrc;

    // ---- 1. per-sample sources
    for (int i = 0; i < len; ++i) src[(int) Src::Env1][i] = env[0].next();
    for (int e = 1; e < 3; ++e)
        if (used & (1u << ((int) Src::Env1 + e)))
            for (int i = 0; i < len; ++i) src[(int) Src::Env1 + e][i] = env[e].next();
    for (int l = 0; l < 4; ++l)
    {
        const int s = (int) Src::Lfo1 + l;
        if (! (used & (1u << s))) continue;
        if (tv.lfoActive[l]) { const Shape* cs = &ctx.extras->lfoCustom[(size_t) l]; for (int i = 0; i < len; ++i) src[s][i] = lfo[l].next (cs); }
        else if (ctx.globalSrc[s] != nullptr) std::memcpy (src[s], ctx.globalSrc[s] + pos, sizeof (float) * (size_t) len);
    }
    if (tv.msegOn)
        for (int i = 0; i < len; ++i) src[(int) Src::Mseg][i] = mseg.next (ctx.extras->mseg, (MsegMode) tv.msegMode, tv.msegLen);
    for (int s = (int) Src::Macro1; s < NumSrc; ++s)
    {
        if (! (used & (1u << s))) continue;
        if (ctx.globalSrc[s] != nullptr) { std::memcpy (src[s], ctx.globalSrc[s] + pos, sizeof (float) * (size_t) len); continue; }
        const float v = snap[s];
        for (int i = 0; i < len; ++i) src[s][i] = v;
    }
    for (int s = 0; s < NumSrc; ++s)
        if (s <= (int) Src::Mseg || (used & (1u << s))) snap[s] = src[s][len - 1];

    // ---- 2. fast destinations (normalized), base + per-sample routings
    for (int k = 0; k < NumFast; ++k) std::memcpy (fast[k], ctx.baseFast[k] + pos, sizeof (float) * (size_t) len);
    for (int r = 0; r < rt.nFast; ++r)
    {
        const auto& ref = rt.fast[r];
        const float d = depthVal[ref.slot];
        if (d == 0.0f) continue;
        bool nb = srcIsBipolar (ref.src);
        if (ref.src >= (int) Src::Lfo1 && ref.src <= (int) Src::Lfo4) nb = tv.lfoBipolar[ref.src - (int) Src::Lfo1];
        float* dst = fast[ref.fast];
        const float* sv = src[ref.src];
        const float* av = ref.aux >= 0 ? src[ref.aux] : nullptr;
        for (int i = 0; i < len; ++i)
        {
            float v = convertSrc (sv[i], nb, ref.polarity, ref.curve);
            if (av) v *= av[i];
            dst[i] += d * v;
        }
    }
    bool fastVar[NumFast];
    for (int k = 0; k < NumFast; ++k) fastVar[k] = ctx.baseFast[k][pos] != ctx.baseFast[k][pos + len - 1];
    for (int r = 0; r < rt.nFast; ++r)
    {
        const auto& ref = rt.fast[r];
        if (depthVal[ref.slot] == 0.0f) continue;
        float* fd = fast[ref.fast];
        for (int i = 0; i < len; ++i) fd[i] = clampf (fd[i], 0.0f, 1.0f);
        if (! fastVar[ref.fast])
            for (int i = 1; i < len; ++i) if (fd[i] != fd[0]) { fastVar[ref.fast] = true; break; }
    }

    for (int k = 0; k < NumFast; ++k) lastFast[k] = fast[k][len - 1];

    // ---- 3. pitch per sample
    const float* pbArr = ctx.globalSrc[(int) Src::PitchBend];
    const float wanderAmt = 3.0f * tv.drift;
    const float driftCents = driftPitchU * 8.0f * tv.drift;
    // 0.3 Hz wander evaluated at control rate and interpolated across the chunk
    const float wanderFrom = wanderNow;
    wanderNow = wander.advance (len);
    const float wanderStep = (wanderNow - wanderFrom) / (float) len;
    const bool wasGliding = gliding;
    for (int i = 0; i < len; ++i)
    {
        if (gliding)
        {
            if (pitchNow < pitchTarget) { pitchNow += glideRate; if (pitchNow >= pitchTarget) { pitchNow = pitchTarget; gliding = false; } }
            else { pitchNow -= glideRate; if (pitchNow <= pitchTarget) { pitchNow = pitchTarget; gliding = false; } }
        }
        const float w = (wanderFrom + wanderStep * (float) (i + 1)) * wanderAmt;
        const float pb = pbArr != nullptr ? pbArr[pos + i] * ctx.pbRange : 0.0f;
        pitchBuf[i] = pitchNow + noteBend + pb + (fast[FD_VoicePitch][i] * 48.0f - 24.0f) + (driftCents + w) * 0.01f;
    }

    // ---- 4. oscillators
    const bool warpOn = tv.warpMode[0] != 0 || tv.warpMode[1] != 0;
    const int os = warpOn ? std::max (1, ctx.osFactor) : 1;
    if (os != curOs)
    {
        for (auto& a : decS) for (auto& d : a) d.reset();
        for (auto& a : decR) for (auto& d : a) d.reset();
        curOs = os;
    }
    const float invSr = 1.0f / (float) sr;
    bool oscOn[2];
    for (int o = 0; o < 2; ++o)
    {
        const int lvIdx = o == 0 ? FD_ALevel : FD_BLevel;
        float mx = 0.0f;
        for (int i = 0; i < len; ++i) mx = std::max (mx, fast[lvIdx][i]);
        oscOn[o] = mx > 0.0f || tv.modulator[o];
    }
    // FM-from-sub modulator at oscillator rate
    const bool needSubFm = tv.warpMode[0] == (int) WarpMode::FMSub || tv.warpMode[1] == (int) WarpMode::FMSub;
    const float subRatio = tv.subOct == 2 ? 0.25f : 0.5f;
    if (needSubFm)
        for (int i = 0; i < len; ++i)
        {
            const float inc = 440.0f * fastExp2 ((pitchBuf[i] - 69.0f) / 12.0f) * invSr * subRatio / (float) os;
            for (int j = 0; j < os; ++j)
            {
                subFmBuf[i * os + j] = sinCycle (subFmPhase);
                subFmPhase += inc;
                if (subFmPhase >= 1.0f) subFmPhase -= 1.0f;
            }
        }

    // order: an oscillator that is modulated by the other renders second
    int renderOrder[2] = { 0, 1 };
    const bool aUsesB = tv.modulator[1]; // osc B's output is a modulator for A
    const bool bUsesA = tv.modulator[0];
    if (aUsesB && ! bUsesA) { renderOrder[0] = 1; renderOrder[1] = 0; }
    bool rendered[2] = { false, false };

    for (int oi = 0; oi < 2; ++oi)
    {
        const int o = renderOrder[oi];
        if (! oscOn[o]) continue;
        const int b = o == 0 ? FD_APos : FD_BPos;
        const int n = len * os;
        for (int i = 0; i < len; ++i)
        {
            const float semis = pitchBuf[i] + tv.oscSemis[o] + (fast[b + 1][i] * 48.0f - 24.0f);
            const float inc = 440.0f * fastExp2 ((semis - 69.0f) / 12.0f) * invSr / (float) os;
            for (int j = 0; j < os; ++j)
            {
                incBuf[o][i * os + j] = inc;
                posBuf[o][i * os + j] = fast[b][i];
                warpBuf[o][i * os + j] = fast[b + 4][i];
            }
        }
        const WarpMode wm = (WarpMode) tv.warpMode[o];
        const float* mod = nullptr;
        if (wm == WarpMode::FMOther || wm == WarpMode::AM || wm == WarpMode::RM)
        {
            static const float zeros[WavetableOsc::MaxBlock] = {};
            mod = rendered[1 - o] ? oscMono[1 - o] : zeros;
        }
        else if (wm == WarpMode::FMSub) mod = subFmBuf;
        osc[o].render (ctx.table[o], n, incBuf[o], posBuf[o], warpBuf[o], wm, mod, *ctx.remap[o],
                       oscBufL[o], oscBufR[o], oscMono[o], os);
        rendered[o] = true;
    }
    // decimate to host rate (in place)
    if (os > 1)
        for (int o = 0; o < 2; ++o)
        {
            if (! rendered[o]) continue;
            float* chs[2] = { oscBufL[o], oscBufR[o] };
            for (int c = 0; c < 2; ++c)
            {
                float* x = chs[c];
                if (os == 4)
                {
                    for (int i = 0; i < len * 2; ++i) x[i] = decR[o][c].process (x[2 * i], x[2 * i + 1], relaxedCoefs());
                }
                for (int i = 0; i < len; ++i) x[i] = decS[o][c].process (x[2 * i], x[2 * i + 1], steepCoefs());
            }
        }

    // ---- 5. sources -> buses (F1, F2, Direct), filters, amp
    float (*bus)[2][kTick] = busBuf;
    bool busUsed[3] = { false, false, false };
    bool busMono[3] = { true, true, true };
    auto target = [&] (int dest, int& a, int& b) {
        a = dest == 0 ? 0 : (dest == 1 ? 1 : (dest == 2 ? 0 : 2));
        b = (dest == 2 && tv.parallel) ? 1 : -1;
    };
    auto useBus = [&] (int k) {
        if (k < 0 || busUsed[k]) return;
        busUsed[k] = true;
        std::fill (bus[k][0], bus[k][0] + len, 0.0f);
        std::fill (bus[k][1], bus[k][1] + len, 0.0f);
    };
    auto addSource = [&] (int dest, const float* l, const float* r, int lvIdx, int panIdx, bool srcMono) {
        int a, b;
        target (dest, a, b);
        const bool centred = srcMono && ! fastVar[panIdx] && fast[panIdx][0] == 0.5f;
        if (! centred) { if (a >= 0) busMono[a] = false; if (b >= 0) busMono[b] = false; }
        useBus (a); useBus (b);
        const float* lv = fast[lvIdx];
        const float* pn = fast[panIdx];
        for (int k : { a, b })
        {
            if (k < 0) continue;
            float* L = bus[k][0];
            float* R = bus[k][1];
            if (! fastVar[panIdx])
            {
                float pl, pr;
                panGains (pn[0] * 2.0f - 1.0f, pl, pr);
                pl *= 1.41421356f; pr *= 1.41421356f;
                for (int i = 0; i < len; ++i) { L[i] += l[i] * lv[i] * pl; R[i] += r[i] * lv[i] * pr; }
            }
            else
                for (int i = 0; i < len; ++i)
                {
                    float pl, pr;
                    panGains (pn[i] * 2.0f - 1.0f, pl, pr);
                    L[i] += l[i] * lv[i] * pl * 1.41421356f;
                    R[i] += r[i] * lv[i] * pr * 1.41421356f;
                }
        }
    };
    for (int o = 0; o < 2; ++o)
    {
        if (! rendered[o]) continue;
        const int b = o == 0 ? FD_APos : FD_BPos;
        bool audible = false;
        for (int i = 0; i < len && ! audible; ++i) audible = fast[b + 2][i] > 0.0f;
        if (audible) addSource (tv.dest[o], oscBufL[o], oscBufR[o], b + 2, b + 3, osc[o].isMonoOutput() && os == 1);
    }
    bool subOn = false, noiseOn = false;
    for (int i = 0; i < len; ++i) { subOn |= fast[FD_SubLevel][i] > 0.0f; noiseOn |= fast[FD_NoiseLevel][i] > 0.0f; }
    if (subOn)
    {
        const float ratio = tv.subOct == 2 ? 0.25f : 0.5f;
        for (int i = 0; i < len; ++i)
        {
            const float inc = 440.0f * fastExp2 ((pitchBuf[i] - 69.0f) / 12.0f) * invSr * ratio;
            tmpA[i] = tmpB[i] = sub.next (inc, tv.subShape);
        }
        addSource (tv.subDest, tmpA, tmpB, FD_SubLevel, FD_SubPan, true);
    }
    if (noiseOn)
    {
        for (int i = 0; i < len; ++i)
        {
            tmpA[i] = noise[0].tone (noise[0].next (tv.noiseType), tv.noiseTone, tv.ntLow, tv.ntHigh);
            tmpB[i] = tv.noiseStereo ? noise[1].tone (noise[1].next (tv.noiseType), tv.noiseTone, tv.ntLow, tv.ntHigh) : tmpA[i];
        }
        addSource (tv.noiseDest, tmpA, tmpB, FD_NoiseLevel, FD_NoisePan, ! tv.noiseStereo);
    }

    static constexpr float kLog2_1000 = 9.965784285f;
    const float driftCut = driftCutU * 0.15f * tv.drift;
    auto runFilter = [&] (int f, int k) {
        float* L = bus[k][0];
        float* R = bus[k][1];
        const int cutIdx = f == 0 ? FD_Cut1 : FD_Cut2;
        const float kt = tv.keytrack[f];
        const bool mono = busMono[k];
        if (! fastVar[cutIdx] && ! wasGliding)
        {
            const float c = 20.0f * fastExp2 (fast[cutIdx][0] * kLog2_1000 + (pitchNow - 60.0f) / 12.0f * kt + driftCut);
            filter[f].processBlock (L, R, len, nullptr, c, tv.reso[f], tv.vowel[f], tv.drive[f], tv.fmix[f], mono);
        }
        else
        {
            for (int i = 0; i < len; ++i)
            {
                const float kp = pitchBuf[i] - noteBend - (fast[FD_VoicePitch][i] * 48.0f - 24.0f);
                tmpA[i] = 20.0f * fastExp2 (fast[cutIdx][i] * kLog2_1000 + (kp - 60.0f) / 12.0f * kt + driftCut);
            }
            filter[f].processBlock (L, R, len, tmpA, 0.0f, tv.reso[f], tv.vowel[f], tv.drive[f], tv.fmix[f], mono);
        }
    };
    if (! tv.parallel)
    {
        if (busUsed[0])
        {
            if (tv.fOn[0]) runFilter (0, 0);
            const bool wasUsed = busUsed[1];
            useBus (1);
            busMono[1] = (wasUsed ? busMono[1] : true) && busMono[0];
            for (int i = 0; i < len; ++i) { bus[1][0][i] += bus[0][0][i]; bus[1][1][i] += bus[0][1][i]; }
        }
        if (busUsed[1] && tv.fOn[1]) runFilter (1, 1);
    }
    else
    {
        if (busUsed[0] && tv.fOn[0]) runFilter (0, 0);
        if (busUsed[1] && tv.fOn[1]) runFilter (1, 1);
        if (busUsed[0])
        {
            useBus (1);
            for (int i = 0; i < len; ++i) { bus[1][0][i] += bus[0][0][i]; bus[1][1][i] += bus[0][1][i]; }
        }
    }
    if (busUsed[2])
    {
        useBus (1);
        for (int i = 0; i < len; ++i) { bus[1][0][i] += bus[2][0][i]; bus[1][1][i] += bus[2][1][i]; }
    }

    const float* e1 = src[(int) Src::Env1];
    const float* ampP = fast[FD_Amp];
    if (busUsed[1])
        for (int i = 0; i < len; ++i)
        {
            float amp = e1[i] * velGain * ampP[i];
            if (state == State::Fading)
            {
                amp *= (float) fadeRemain / (float) fadeLen;
                if (fadeRemain > 0) --fadeRemain;
            }
            outL[pos + i] += bus[1][0][i] * amp;
            outR[pos + i] += bus[1][1][i] * amp;
        }
    else if (state == State::Fading)
        fadeRemain = std::max (0, fadeRemain - len);

    if (! env[0].isActive() || (state == State::Fading && fadeRemain <= 0))
        state = State::Free;
}
} // namespace prism
