#pragma once
#include "Wavetable.h"
#include "Random.h"
#include "../Params/Params.h"
#include <array>

namespace prism::dsp
{
// Mip-level choice for a given phase increment (cycles per sample, already
// multiplied by the fastest unison ratio and the warp slope factor).
struct MipChoice { int level = 0; float wDark = 0.0f; bool silent = false; };
MipChoice chooseMip (float incPerSample) noexcept;

// Curve used by the Remap warp (piecewise linear, 33 points)
struct RemapCurve
{
    std::array<float, 33> y {};
    float maxSlope = 1.0f;
    RemapCurve();
    float eval (float p) const noexcept
    {
        const float x = p * 32.0f;
        int i = (int) x;
        if (i >= 32) return y[32];
        if (i < 0) return y[0];
        const float f = x - (float) i;
        return y[(size_t) i] + (y[(size_t) i + 1] - y[(size_t) i]) * f;
    }
    void computeSlope() noexcept;
};

class WavetableOsc
{
public:
    static constexpr int MaxUnison = 16;
    static constexpr int MaxOS = 4;
    static constexpr int MaxBlock = 32 * MaxOS;

    struct Unison
    {
        int count = 1;
        float detune = 0.2f, blend = 0.7f, spread = 0.6f, wtSpread = 0.0f;
    };

    // Start a note: phases (fixed + random per lane)
    void start (Rng& rng, float phase, float randPhase) noexcept;
    // Update unison lane layout (call at control rate)
    void setUnison (const Unison& u) noexcept;
    // Per-lane extra offsets from the "unison voice index" mod source
    void setLaneMod (float pitchSemisPerIndex, float wtPerIndex, float panPerIndex) noexcept;

    float maxLaneRatio() const noexcept { return maxRatio; }
    // true when L and R outputs are bit-identical (single centred lane or no stereo spread)
    bool isMonoOutput() const noexcept { return gainL[0] == gainR[0] && (lanes == 1 || (uni.spread == 0.0f && lanePan == 0.0f)); }

    // Render `n` samples at the oscillator rate (host rate x os).
    //  inc[i]   : fundamental increment in cycles/sample at oscillator rate
    //  pos[i]   : WT position 0..1
    //  warp[i]  : warp amount 0..1
    //  mod      : modulator signal (FM/AM/RM), may be null
    //  outL/R   : stereo (pre level/pan), mono: sum pre-pan (for use as modulator)
    void render (const Wavetable* table, int n, const float* inc, const float* pos, const float* warp,
                 WarpMode mode, const float* mod, const RemapCurve& remap,
                 float* outL, float* outR, float* mono) noexcept;

    // Live state for GUI
    float lastPos() const noexcept { return lastPosition; }

private:
    alignas (16) float phase[MaxUnison] {};
    alignas (16) float ratio[MaxUnison] {};
    alignas (16) float gainL[MaxUnison] {};
    alignas (16) float gainR[MaxUnison] {};
    alignas (16) float gainM[MaxUnison] {};
    alignas (16) float wtOff[MaxUnison] {};
    float laneIndex[MaxUnison] {};
    int lanes = 1;
    float maxRatio = 1.0f;
    Unison uni;
    float lanePitch = 0, laneWt = 0, lanePan = 0;
    float lastPosition = 0;
    float mipInc = -1.0f;
    MipChoice mipCache;
    void rebuild() noexcept;
    void renderSingle (const Wavetable& t, int n, const float* inc, const float* pos, float* outL, float* outR, float* mono) noexcept;
};
} // namespace prism::dsp
