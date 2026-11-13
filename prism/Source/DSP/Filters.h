#pragma once
// Voice filters (SPEC §4): TPT/ZDF SVF family, 4-pole ZDF ladder with tanh,
// diode ladder, comb +/- and a 3-band formant bank. Stereo, per sample.
#include "Common.h"
#include "Halfband.h"
#include "../Params/Params.h"
#include <vector>

namespace prism::dsp
{
struct DiodeConstants { double kCrit = 0, oscRatio = 1; };
const DiodeConstants& diodeConstants(); // call once off the audio thread (prepare)

struct VowelFormants { float f[3]; float g[3]; };
VowelFormants vowelAt (float vowel) noexcept; // 0..1 morphs A-E-I-O-U

class FilterCore
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setType (FilterType t) noexcept { type = t; lastCut = -1.0f; }
    FilterType getType() const noexcept { return type; }

    void process (float& l, float& r, float cutoffHz, float reso, float vowel) noexcept;

private:
    struct Svf { float ic1[2] {}, ic2[2] {}; };

    double sr = 48000.0;
    FilterType type = FilterType::LP24;
    // cached coefficients
    float lastCut = -1, lastReso = -1, lastVowel = -1;
    float g = 0, k = 2, a1 = 0, a2 = 0, a3 = 0;
    float k2 = 1.41421356f, b1 = 0, b2 = 0, b3 = 0; // second SVF stage
    bool satStates = false;
    Svf s1, s2;
    // ladder / diode
    float lad[4][2] {};
    float G = 0, ladK = 0;
    double dio[2][4] {};
    double dioG = 0, dioK = 0;
    // formant
    Svf fs[3];
    float fa1[3] {}, fa2[3] {}, fa3[3] {}, fk[3] {}, fgain[3] {};
    // comb
    std::vector<float> comb[2];
    int combLen = 0, combW = 0;
    float combDelay = 1, combFb = 0;

    void updateCoefs (float cutoffHz, float reso, float vowel) noexcept;
    inline float svfTick (Svf& s, int ch, float v0, float A1, float A2, float A3, float& bp, float& hp, float K) noexcept;
};

class Filter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setType (FilterType t) noexcept;
    void forceType (FilterType t) noexcept { type = t; active = 0; xfadeRemain = 0; core[0].setType (t); core[0].reset(); drv[0].reset(); drv[1].reset(); }
    // drive in dB (0..24), mix 0..1
    void process (float& l, float& r, float cutoffHz, float reso, float vowel, float driveDb, float mix) noexcept;
    FilterType getType() const noexcept { return type; }

private:
    FilterCore core[2];
    int active = 0;
    int xfadeRemain = 0, xfadeLen = 240;
    FilterType type = FilterType::LP24;
    Oversampled2x drv[2];
    float lastDrive = 0.0f;
};
} // namespace prism::dsp
