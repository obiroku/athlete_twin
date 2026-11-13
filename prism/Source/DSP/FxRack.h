#pragma once
// Reorderable FX rack (SPEC §8) and the master section.
#include "FxModules.h"
#include <array>
#include <memory>
#include <vector>

namespace prism::dsp
{
class FxRack
{
public:
    static constexpr int NumFx = 12;

    FxRack();
    void prepare (double sr, int maxBlock);
    void reset();                                     // clears all tails
    void setOrder (const std::array<int8_t, NumFx>& order); // crossfaded (10 ms total)
    // real: parameter values (real units) for this tick; enableParams[i] = param index of module i's on/off
    void update (const float* real, const FxContext& ctx, const int* enableParams);
    void process (float* L, float* R, int n);
    double tailSeconds() const;          // longest active tail, capped at 30 s
    bool isSleeping (int fx) const { return sleeping[(size_t) fx]; }
    bool isEnabled (int fx) const { return enabled[(size_t) fx]; }
    const std::array<int8_t, NumFx>& currentOrder() const { return order; }

private:
    double sr = 48000.0;
    std::array<std::unique_ptr<FxModule>, NumFx> mods;
    std::array<int8_t, NumFx> order {}, pendingOrder {};
    bool orderPending = false;
    std::array<bool, NumFx> enabled {}, sleeping {};
    std::array<Ramp, NumFx> wet;          // bypass crossfade (10 ms)
    std::array<int64_t, NumFx> silentFor {};
    Ramp rackMix;                         // reorder crossfade via dry
    std::vector<float> dryL, dryR, tmpL, tmpR, rackDryL, rackDryR;
};

class Master
{
public:
    static constexpr int LimiterLookahead = 64;
    void prepare (double sr, int maxBlock);
    void reset();
    struct Settings { float volDb = -6, width = 1, bassMonoHz = 0; bool softclip = true; };
    void set (const Settings& s);
    void process (float* L, float* R, int n);
    float lastPreLimiterPeak = 0.0f; // for tests
    float peakL = 0, peakR = 0;      // meter (block peaks)
private:
    double sr = 48000.0;
    Settings st;
    Ramp vol;
    Biquad bassLp;
    bool bassOn = false;
    float lastBassHz = -1;
    // limiter
    std::array<float, 128> delayL {}, delayR {};
    std::array<float, 128> gq {};
    int dPos = 0;
    float relEnv = 1.0f, relCoef = 0.9999f;
    double avgSum = 0.0;
    std::array<float, 128> holdBuf {};
    float ceiling = 0.881f;
};
} // namespace prism::dsp
