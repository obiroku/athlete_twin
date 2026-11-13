#pragma once
#include "ModTypes.h"
#include "../DSP/WavetableOsc.h"
#include "../DSP/Envelope.h"
#include "../DSP/Lfo.h"
#include "../DSP/Mseg.h"
#include "../DSP/SubNoise.h"
#include "../DSP/Filters.h"
#include "../DSP/Halfband.h"
#include <cstdint>

namespace prism
{
constexpr int kTick = 32;           // control-rate period in samples
constexpr int kMaxChunk = 512;      // engine processes host blocks in chunks of this size

// The 18 per-sample (fast) destinations
enum FastDest : int
{
    FD_VoicePitch, FD_Amp,
    FD_APos, FD_APitch, FD_ALevel, FD_APan, FD_AWarp,
    FD_BPos, FD_BPitch, FD_BLevel, FD_BPan, FD_BWarp,
    FD_SubLevel, FD_SubPan, FD_NoiseLevel, FD_NoisePan,
    FD_Cut1, FD_Cut2,
    NumFast
};
int fastIndexForParam (int param); // -1 if not fast
int paramForFastIndex (int fd);

struct RoutingRef
{
    int slot = 0, src = 0, aux = -1, dest = 0, fast = -1;
    int8_t polarity = -1; // -1 native
    float curve = 0.0f;
};

struct RoutingTable
{
    int nDepth = 0, nSlow = 0, nFast = 0, nUnison = 0, nGlobal = 0;
    RoutingRef depth[P::NumModSlots], slow[P::NumModSlots], fast[P::NumModSlots], unison[P::NumModSlots], global[P::NumModSlots];
    uint32_t usedSrc = 0;
    void build (const PatchExtras& x);
};

// Everything a voice needs from the engine while rendering a chunk.
struct RenderContext
{
    double sr = 48000.0;
    int osFactor = 2;                    // Quality: 1, 2, 4
    const float* tickNorm = nullptr;     // [numTicks][NumParams]
    int tickPhase = 0;                   // (absolute sample of chunk start) % kTick
    const float* baseFast[NumFast] {};   // per-sample normalized base values for the chunk
    const float* globalSrc[NumSrc] {};   // per-sample global sources (null for per-voice sources)
    const PatchExtras* extras = nullptr;
    const RoutingTable* routes = nullptr;
    const dsp::Wavetable* table[2] {};
    const dsp::RemapCurve* remap[2] {};
    double bpm = 120.0;
    float pbRange = 2.0f;

    const float* normRowAt (int pos) const noexcept
    {
        return tickNorm + (size_t) ((pos + tickPhase) / kTick + 1) * P::NumParams;
    }
};

class Voice
{
public:
    enum class State { Free, Active, Fading };

    void prepare (double sr);
    void reset() noexcept;

    // note: MIDI note (float for MPE), vel 0..1 (after curve)
    void start (const RenderContext& ctx, int pos, int note, float vel, int channel, uint64_t order, dsp::Rng& rng,
                float alternate, float glideFrom, bool glide) noexcept;
    void retrigger (float vel, uint64_t order) noexcept;               // same key again
    void legatoTo (int note, float glideFrom, bool glide) noexcept;    // mono legato: no env retrigger
    void release (float relVel) noexcept;
    void beginStealFade() noexcept;
    void kill() noexcept { state = State::Free; }

    // Adds output for block positions [start, end)
    void render (const RenderContext& ctx, int start, int end, float* outL, float* outR) noexcept;

    State getState() const noexcept { return state; }
    bool isActive() const noexcept { return state == State::Active; }
    bool isReleasing() const noexcept { return state == State::Active && released; }
    bool isHeldByKey() const noexcept { return state == State::Active && ! released; }
    int getNote() const noexcept { return note; }
    int getChannel() const noexcept { return channel; }
    uint64_t getOrder() const noexcept { return order; }
    float loudness() const noexcept { return env[0].getLevel() * velGain; }
    float currentPitch() const noexcept { return pitchNow; }

    // per-note expression (MPE / poly AT)
    void setPolyPressure (float v) noexcept { polyAT = v; }
    void setSlide (float v) noexcept { slide = v; }
    void setNoteBend (float semis) noexcept { noteBend = semis; }

    // last per-voice source values (for FX modulation & GUI)
    const float* sourceSnapshot() const noexcept { return snap; }
    float modulatedNorm (int p) const noexcept { const int f = fastIndexForParam (p); return f >= 0 ? lastFast[f] : slowNorm[p]; }
    float oscPosition (int o) const noexcept { return osc[o].lastPos(); }

private:
    double sr = 48000.0;
    State state = State::Free;
    bool released = false;
    int note = 60, channel = 1;
    uint64_t order = 0;
    float velocity = 1, relVelocity = 0.5f, velGain = 1;
    float randomNote = 0, alternate = 1, polyAT = 0, slide = 0, noteBend = 0;
    float driftPitchU = 0, driftCutU = 0;

    // pitch / glide (semitones)
    float pitchNow = 60, pitchTarget = 60, glideRate = 0;
    bool gliding = false;

    int ticksLeft = 0;
    int fadeRemain = 0, fadeLen = 144;

    dsp::WavetableOsc osc[2];
    dsp::SubOsc sub;
    dsp::NoiseGen noise[2];
    dsp::Envelope env[3];
    dsp::Lfo lfo[4];
    dsp::Lfo wander;
    dsp::MsegPlayer mseg;
    dsp::Filter filter[2];
    float subFmPhase = 0;
    int curOs = 1;
    dsp::HalfbandDecimator<8> decS[2][2];   // [osc][ch], last 2x stage
    dsp::HalfbandDecimator<4> decR[2][2];   // [osc][ch], first stage of 4x

    // control-rate state
    float slowNorm[P::NumParams] {};
    float depthVal[P::NumModSlots] {};
    float snap[NumSrc] {};
    struct TickVals
    {
        float drift = 0, glideTime = 0; int glideMode = 0;
        float oscSemis[2] {}; int warpMode[2] {}; int dest[2] {}; bool modulator[2] {};
        int subShape = 0, subOct = 1, subDest = 0;
        int noiseType = 0, noiseDest = 0; bool noiseStereo = true; float noiseTone = 0.5f, ntLow = 1, ntHigh = 0.1f;
        bool fOn[2] {}; float reso[2] {}, drive[2] {}, keytrack[2] {}, vowel[2] {}, fmix[2] {};
        bool parallel = false;
        bool lfoActive[4] {}; bool lfoBipolar[4] {};
        bool msegOn = false; double msegLen = 48000; int msegMode = 0;
    } tv;

    // per-chunk buffers (host rate unless noted)
    float src[NumSrc][kTick] {};
    float fast[NumFast][kTick] {};
    float oscBufL[2][dsp::WavetableOsc::MaxBlock] {}, oscBufR[2][dsp::WavetableOsc::MaxBlock] {}, oscMono[2][dsp::WavetableOsc::MaxBlock] {};
    float incBuf[2][dsp::WavetableOsc::MaxBlock] {}, posBuf[2][dsp::WavetableOsc::MaxBlock] {}, warpBuf[2][dsp::WavetableOsc::MaxBlock] {};
    float subFmBuf[dsp::WavetableOsc::MaxBlock] {};
    float pitchBuf[kTick] {};
    float lastFast[NumFast] {};

    void controlTick (const RenderContext& ctx, int pos) noexcept;
    void renderChunk (const RenderContext& ctx, int pos, int len, float* outL, float* outR) noexcept;
    float real (int p) const noexcept { return paramDefs()[(size_t) p].fromNorm (slowNorm[p]); }
};
} // namespace prism
