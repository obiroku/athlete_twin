#pragma once
// DAHDSR envelope with per-stage curves (SPEC §6). Retrigger always starts
// from the current level. Rates are re-read at control rate so stage times can
// be modulated.
#include <cmath>
#include <algorithm>

namespace prism::dsp
{
class Envelope
{
public:
    enum class Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };

    struct Times
    {
        float delay = 0, attack = 0.001f, hold = 0, decay = 0.3f, sustain = 1, release = 0.05f;
        float curveA = 0, curveD = 0, curveR = 0;
    };

    void prepare (double sampleRate) noexcept { sr = sampleRate; }
    void reset() noexcept { stage = Stage::Idle; level = 0; }

    void setTimes (const Times& t) noexcept
    {
        times = t;
        updateStageRate();
    }

    void noteOn() noexcept
    {
        startLevel = level;
        enter (times.delay > 0.0f ? Stage::Delay : Stage::Attack);
    }

    void noteOff() noexcept
    {
        if (stage == Stage::Idle || stage == Stage::Release) return;
        startLevel = level;
        enter (Stage::Release);
    }

    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    Stage getStage() const noexcept { return stage; }
    float getLevel() const noexcept { return level; }

    inline float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return level = 0.0f;
            case Stage::Delay:
                t += inc;
                if (t >= 1.0f) enter (Stage::Attack);
                return level;
            case Stage::Attack:
            {
                advance();
                level = startLevel + (1.0f - startLevel) * shape;
                if (t >= 1.0f) { level = 1.0f; enter (times.hold > 0.0f ? Stage::Hold : Stage::Decay); }
                return level;
            }
            case Stage::Hold:
                t += inc;
                level = 1.0f;
                if (t >= 1.0f) enter (Stage::Decay);
                return level;
            case Stage::Decay:
            {
                advance();
                level = 1.0f + (times.sustain - 1.0f) * shape;
                if (t >= 1.0f) { level = times.sustain; enter (Stage::Sustain); }
                return level;
            }
            case Stage::Sustain:
                // follow sustain changes smoothly
                level += (times.sustain - level) * 0.002f;
                return level;
            case Stage::Release:
            {
                advance();
                level = startLevel * (1.0f - shape);
                if (t >= 1.0f) { level = 0.0f; stage = Stage::Idle; }
                return level;
            }
        }
        return level;
    }

private:
    double sr = 48000.0;
    Times times;
    Stage stage = Stage::Idle;
    float level = 0, startLevel = 0;
    float t = 0, inc = 1;        // stage progress 0..1
    float k = 0;                 // curve constant (0 = linear)
    double u = 1, mul = 1;       // exp(k t) tracked recursively
    float denom = 1;             // exp(k) - 1
    float shape = 0;

    inline void advance() noexcept
    {
        t += inc;
        if (t > 1.0f) t = 1.0f;
        if (k == 0.0f) shape = t;
        else
        {
            u *= mul;
            shape = (float) ((u - 1.0) / denom);
        }
    }

    static float curveConst (float c, bool rising) noexcept
    {
        // c > 0: slow start (convex) for rising, fast drop for falling.
        const float kk = c * 6.0f;
        return rising ? kk : -kk;
    }

    float stageTime() const noexcept
    {
        switch (stage)
        {
            case Stage::Delay: return times.delay;
            case Stage::Attack: return times.attack;
            case Stage::Hold: return times.hold;
            case Stage::Decay: return times.decay;
            case Stage::Release: return times.release;
            default: return 1.0f;
        }
    }

    void enter (Stage s) noexcept
    {
        stage = s;
        t = 0.0f;
        shape = 0.0f;
        switch (s)
        {
            case Stage::Attack: k = curveConst (times.curveA, true); break;
            case Stage::Decay: k = curveConst (times.curveD, false); break;
            case Stage::Release: k = curveConst (times.curveR, false); break;
            default: k = 0.0f; break;
        }
        u = 1.0;
        if (s == Stage::Release) startLevel = level;
        updateStageRate();
        // zero-length stages complete on the next sample
    }

    void updateStageRate() noexcept
    {
        const double len = std::max (1.0, (double) stageTime() * sr);
        inc = (float) (1.0 / len);
        if (stage == Stage::Attack) k = curveConst (times.curveA, true);
        else if (stage == Stage::Decay) k = curveConst (times.curveD, false);
        else if (stage == Stage::Release) k = curveConst (times.curveR, false);
        if (k != 0.0f)
        {
            denom = (float) (std::exp ((double) k) - 1.0);
            mul = std::exp ((double) k * inc);
            u = std::exp ((double) k * t); // keep continuity when the rate changes
        }
    }
};
} // namespace prism::dsp
