#pragma once
// Modulation sources, routings and the non-parameter patch state that is
// sent to the audio thread through the command FIFO (POD, no heap).
#include "../DSP/Shape.h"
#include "../Params/Params.h"
#include <array>
#include <string>
#include <vector>

namespace prism
{
enum class Src : int
{
    Env1, Env2, Env3, Lfo1, Lfo2, Lfo3, Lfo4, Mseg,
    Macro1, Macro2, Macro3, Macro4,
    Velocity, RelVelocity, Note, ModWheel, PitchBend, ChanAT, PolyAT, Slide,
    RandomNote, Alternate, UnisonIndex, CCA, CCB,
    Count
};
constexpr int NumSrc = (int) Src::Count;

const char* srcId (int s);          // stable id for presets ("env2", "lfo1", ...)
const char* srcName (int s);        // display name
int srcFromId (const std::string&); // -1 if unknown
bool srcIsBipolar (int s);          // native polarity (LFOs: decided by their param)
bool srcIsPerVoice (int s);

struct Routing
{
    int16_t src = -1;      // Src, -1 = empty slot
    int16_t dest = -1;     // parameter index (continuous params only)
    int16_t aux = -1;      // Src or -1
    int8_t bipolar = -1;   // -1 = source native, 0 = uni, 1 = bi
    int8_t bypass = 0;
    float curve = 0.0f;    // -1..1 power curve
    bool active() const noexcept { return src >= 0 && dest >= 0 && ! bypass; }
};

struct ChordShapeData
{
    int count = 0;
    std::array<int8_t, 8> intervals {};
};

struct PatchExtras
{
    std::array<Routing, P::NumModSlots> routings {};
    dsp::Shape mseg = dsp::Shape::envelope();
    std::array<dsp::Shape, 4> lfoCustom { dsp::Shape::triangle(), dsp::Shape::triangle(), dsp::Shape::triangle(), dsp::Shape::triangle() };
    std::array<dsp::Shape, 2> remap {};
    std::array<int8_t, 12> fxOrder { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    std::array<float, 16> arpAccents { 1, 0.6f, 0.8f, 0.6f, 1, 0.6f, 0.8f, 0.6f, 1, 0.6f, 0.8f, 0.6f, 1, 0.6f, 0.8f, 0.7f };
    ChordShapeData chord {};
    int ccA = 2, ccB = 11;
};

// The FX modules in their default rack order
enum class FxId : int { Distortion, Tape, Dimension, Chorus, Phaser, Flanger, Rotary, Delay, Reverb, Compressor, Multiband, EQ, Count };
constexpr int NumFx = (int) FxId::Count;
const char* fxId (int fx);   // stable id for presets
const char* fxName (int fx);
int fxFromId (const std::string&);
int fxEnableParam (int fx);  // parameter index of the module's on/off
} // namespace prism
