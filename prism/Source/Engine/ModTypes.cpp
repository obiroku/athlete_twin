#include "ModTypes.h"

namespace prism
{
namespace
{
struct SrcInfo { const char* id; const char* name; bool bipolar; bool perVoice; };
const SrcInfo kSrc[NumSrc] = {
    { "env1", "Env 1", false, true }, { "env2", "Env 2", false, true }, { "env3", "Env 3", false, true },
    { "lfo1", "LFO 1", true, true }, { "lfo2", "LFO 2", true, true }, { "lfo3", "LFO 3", true, true }, { "lfo4", "LFO 4", true, true },
    { "mseg", "MSEG", false, true },
    { "macro1", "Macro 1", false, false }, { "macro2", "Macro 2", false, false }, { "macro3", "Macro 3", false, false }, { "macro4", "Macro 4", false, false },
    { "velocity", "Velocity", false, true }, { "release_velocity", "Release Velocity", false, true },
    { "note", "Note", true, true }, { "mod_wheel", "Mod Wheel", false, false }, { "pitch_bend", "Pitch Bend", true, false },
    { "aftertouch", "Aftertouch", false, false }, { "poly_aftertouch", "Poly AT / MPE Pressure", false, true },
    { "slide", "MPE Slide", false, true }, { "random", "Random per Note", true, true }, { "alternate", "Alternate", true, true },
    { "unison_index", "Unison Voice", true, true }, { "cc_a", "CC A", false, false }, { "cc_b", "CC B", false, false } };

struct FxInfo { const char* id; const char* name; int enableParam; };
const FxInfo kFx[NumFx] = {
    { "distortion", "Distortion", P::DistBase + P::dist_on }, { "tape", "Tape", P::TapeBase + P::tape_on },
    { "dimension", "Dimension", P::DimBase + P::dim_on }, { "chorus", "Chorus", P::ChorusBase + P::chorus_on },
    { "phaser", "Phaser", P::PhaserBase + P::phaser_on }, { "flanger", "Flanger", P::FlangerBase + P::flanger_on },
    { "rotary", "Rotary", P::RotaryBase + P::rotary_on }, { "delay", "Delay", P::DelayBase + P::delay_on },
    { "reverb", "Reverb", P::ReverbBase + P::reverb_on }, { "compressor", "Compressor", P::CompBase + P::comp_on },
    { "multiband", "Multiband", P::MbBase + P::mb_on }, { "eq", "EQ", P::EqBase + P::eq_on } };
} // namespace

const char* srcId (int s) { return s >= 0 && s < NumSrc ? kSrc[s].id : "none"; }
const char* srcName (int s) { return s >= 0 && s < NumSrc ? kSrc[s].name : "None"; }
int srcFromId (const std::string& id)
{
    for (int i = 0; i < NumSrc; ++i) if (id == kSrc[i].id) return i;
    return -1;
}
bool srcIsBipolar (int s) { return s >= 0 && s < NumSrc && kSrc[s].bipolar; }
bool srcIsPerVoice (int s) { return s >= 0 && s < NumSrc && kSrc[s].perVoice; }

const char* fxId (int f) { return f >= 0 && f < NumFx ? kFx[f].id : ""; }
const char* fxName (int f) { return f >= 0 && f < NumFx ? kFx[f].name : ""; }
int fxFromId (const std::string& id)
{
    for (int i = 0; i < NumFx; ++i) if (id == kFx[i].id) return i;
    return -1;
}
int fxEnableParam (int f) { return f >= 0 && f < NumFx ? kFx[f].enableParam : -1; }
} // namespace prism
