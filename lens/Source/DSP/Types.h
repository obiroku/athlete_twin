#pragma once

#include <array>

// Plain parameter snapshots shared by the DSP, the display, and the Coach.
// No JUCE dependencies, so everything here is testable headless.
namespace lens::dsp
{
constexpr int kNumBands = 24;

enum class Shape : int
{
    Bell = 0,
    LowShelf,
    HighShelf,
    LowCut,
    HighCut,
    Notch,
    BandPass,
    Tilt,
    AllPass
};

enum class StereoMode : int
{
    Stereo = 0,
    Mid,
    Side,
    Left,
    Right
};

enum class DynMode : int
{
    Above = 0,
    Below
};

// Slope parameter index → filter order (6 dB/oct per order). Brickwall
// (index 8) exists only in Linear Phase; Zero Latency uses 96 dB/oct.
constexpr int kSlopeBrickwall = 8;
constexpr int kNumSlopes = 9;
inline int slopeOrder (int slopeIndex) noexcept
{
    constexpr int orders[kNumSlopes] = { 1, 2, 3, 4, 6, 8, 12, 16, 16 };
    return orders[slopeIndex < 0 ? 0 : (slopeIndex >= kNumSlopes ? kNumSlopes - 1 : slopeIndex)];
}
inline int slopeDbPerOct (int slopeIndex) noexcept { return 6 * slopeOrder (slopeIndex); }

inline bool isCut (Shape s) noexcept { return s == Shape::LowCut || s == Shape::HighCut; }
inline bool isShelf (Shape s) noexcept { return s == Shape::LowShelf || s == Shape::HighShelf; }
inline bool canBeDynamic (Shape s) noexcept { return s == Shape::Bell || isShelf (s); }
// Shapes whose gain parameter does something.
inline bool usesGain (Shape s) noexcept
{
    return s == Shape::Bell || isShelf (s) || s == Shape::Tilt;
}

struct BandParams
{
    bool on = false;
    Shape shape = Shape::Bell;
    float freq = 1000.0f;
    float gain = 0.0f;
    float q = 1.0f;
    int slope = 1;
    StereoMode stereo = StereoMode::Stereo;

    bool dynOn = false;
    DynMode dynMode = DynMode::Above;
    float dynRange = -6.0f;
    float dynThresh = -20.0f;
    float dynRatio = 3.0f;
    float dynAttack = 10.0f;
    float dynRelease = 120.0f;

    bool detExternal = false;
    bool detCustom = false;
    float detHp = 20.0f;
    float detLp = 20000.0f;

    bool specOn = false;
    float specThresh = 6.0f;
    float specAmount = 12.0f;

    bool isDynamic() const noexcept { return on && dynOn && canBeDynamic (shape); }
    bool isSpectral() const noexcept { return on && specOn && shape == Shape::Bell; }
};

enum class LpResolution : int
{
    Low = 0,
    Medium,
    High
};

struct GlobalParams
{
    bool linearPhase = false;
    LpResolution lpResolution = LpResolution::Medium;
    bool warm = false;
    float outputGain = 0.0f;
    bool autoGain = true;
    bool delta = false;
    bool bypass = false;
    int abSlot = 0;
    float lookaheadMs = 0.0f;
};

struct EngineParams
{
    std::array<BandParams, kNumBands> bands {};
    GlobalParams global {};
};
} // namespace lens::dsp
