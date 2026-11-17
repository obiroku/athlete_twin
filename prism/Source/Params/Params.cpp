#include "Params.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <algorithm>
#include <cassert>

namespace prism
{

// ------------------------------------------------------------------ conversions

static float skewExponent (const ParamDef& d)
{
    // Same definition as juce::NormalisableRange::setSkewForCentre
    const float prop = (d.mid - d.min) / (d.max - d.min);
    return std::log (0.5f) / std::log (prop);
}

float ParamDef::toNorm (float real) const noexcept
{
    if (max <= min) return 0.0f;
    real = std::min (max, std::max (min, real));
    switch (scale)
    {
        case Scale::Linear: return (real - min) / (max - min);
        case Scale::Log:    return std::log (real / min) / std::log (max / min);
        case Scale::SkewMid:
        {
            const float p = (real - min) / (max - min);
            return p <= 0.0f ? 0.0f : std::pow (p, skewExponent (*this));
        }
    }
    return 0.0f;
}

float ParamDef::fromNorm (float n) const noexcept
{
    n = std::min (1.0f, std::max (0.0f, n));
    float v = min;
    switch (scale)
    {
        case Scale::Linear: v = min + n * (max - min); break;
        case Scale::Log:    v = min * std::pow (max / min, n); break;
        case Scale::SkewMid:
            v = n <= 0.0f ? min : min + (max - min) * std::exp (std::log (n) / skewExponent (*this));
            break;
    }
    return std::min (max, std::max (min, v));
}

float ParamDef::snap (float real) const noexcept
{
    real = std::min (max, std::max (min, real));
    if (type == PType::Float) return real;
    return std::round (real);
}

std::string ParamDef::toText (float v) const
{
    char buf[64];
    if (type == PType::Choice)
    {
        const int i = std::max (0, std::min ((int) choices.size() - 1, (int) std::lround (v)));
        return choices.empty() ? std::to_string (i) : choices[(size_t) i];
    }
    if (type == PType::Bool || switchLike) return v >= 0.5f ? "On" : "Off";
    if (type == PType::Int)
    {
        std::snprintf (buf, sizeof (buf), "%d", (int) std::lround (v));
        std::string s (buf);
        if (unit == Unit::Semitones) s += " st";
        if (unit == Unit::Octave) s += " oct";
        if (unit == Unit::Cents) s += " ct";
        return s;
    }
    if (v < offValueBelow) return "Off";
    switch (unit)
    {
        case Unit::Percent: std::snprintf (buf, sizeof (buf), "%.0f%%", v * displayScale); break;
        case Unit::Hz:
            if (v >= 1000.0f) std::snprintf (buf, sizeof (buf), "%.2f kHz", v / 1000.0f);
            else if (v < 10.0f) std::snprintf (buf, sizeof (buf), "%.2f Hz", v);
            else std::snprintf (buf, sizeof (buf), "%.0f Hz", v);
            break;
        case Unit::Seconds:
            if (v < 1.0f) std::snprintf (buf, sizeof (buf), "%.0f ms", v * 1000.0f);
            else std::snprintf (buf, sizeof (buf), "%.2f s", v);
            break;
        case Unit::Ms: std::snprintf (buf, sizeof (buf), "%.1f ms", v); break;
        case Unit::Decibel:
            if (v <= min + 1e-4f && min <= -59.0f) return "-inf dB";
            std::snprintf (buf, sizeof (buf), "%.1f dB", v);
            break;
        case Unit::GainDb:
            if (v <= 1e-5f) return "-inf dB";
            std::snprintf (buf, sizeof (buf), "%.1f dB", 20.0f * std::log10 (v));
            break;
        case Unit::Semitones: std::snprintf (buf, sizeof (buf), "%.2f st", v); break;
        case Unit::Cents: std::snprintf (buf, sizeof (buf), "%.0f ct", v); break;
        case Unit::Degrees: std::snprintf (buf, sizeof (buf), "%.0f deg", v * displayScale); break;
        case Unit::Ratio: std::snprintf (buf, sizeof (buf), "%.1f:1", v); break;
        case Unit::Pan:
            if (std::abs (v) < 0.005f) return "C";
            std::snprintf (buf, sizeof (buf), "%.0f%s", std::abs (v) * 100.0f, v < 0 ? "L" : "R");
            break;
        case Unit::Octave: std::snprintf (buf, sizeof (buf), "%.2f oct", v); break;
        case Unit::None:
        default: std::snprintf (buf, sizeof (buf), "%.3f", v * displayScale); break;
    }
    return buf;
}

bool ParamDef::fromText (const std::string& text, float& out) const
{
    if (type == PType::Choice)
    {
        for (size_t i = 0; i < choices.size(); ++i)
            if (choices[i] == text) { out = (float) i; return true; }
    }
    std::string t = text;
    std::transform (t.begin(), t.end(), t.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    if (type == PType::Bool || switchLike)
    {
        if (t == "on" || t == "1" || t == "true") { out = 1; return true; }
        if (t == "off" || t == "0" || t == "false") { out = 0; return true; }
    }
    if (t == "off" && offValueBelow > -1e29f) { out = min; return true; }
    if (t.find ("inf") != std::string::npos) { out = min; return true; }
    char* end = nullptr;
    double v = std::strtod (t.c_str(), &end);
    if (end == t.c_str()) return false;
    std::string rest (end);
    const bool k = rest.find ('k') != std::string::npos;
    const bool ms = rest.find ("ms") != std::string::npos;
    switch (unit)
    {
        case Unit::Percent: v /= displayScale; break;
        case Unit::Degrees: v /= displayScale; break;
        case Unit::Hz: if (k) v *= 1000.0; break;
        case Unit::Seconds: if (ms) v /= 1000.0; break;
        case Unit::GainDb: v = std::pow (10.0, v / 20.0); break;
        case Unit::None: v /= displayScale; break;
        default: break;
    }
    out = snap ((float) v);
    return true;
}

// ------------------------------------------------------------------ sync divisions

const std::vector<std::string>& syncDivNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        const char* bases[] = { "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        for (auto* b : bases)
        {
            n.push_back (b);
            n.push_back (std::string (b) + "D");
            n.push_back (std::string (b) + "T");
        }
        return n;
    }();
    return names;
}

double syncDivBeats (int index)
{
    static const double baseBeats[] = { 32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 0.0625 };
    index = std::max (0, std::min (29, index));
    const double b = baseBeats[index / 3];
    switch (index % 3)
    {
        case 1: return b * 1.5;
        case 2: return b * 2.0 / 3.0;
        default: return b;
    }
}

int syncDivIndex (const std::string& name)
{
    const auto& n = syncDivNames();
    for (size_t i = 0; i < n.size(); ++i) if (n[i] == name) return (int) i;
    return 15; // 1/4
}

// ------------------------------------------------------------------ builder

namespace
{
struct Builder
{
    std::vector<ParamDef> defs;
    std::string group;
    Scope scope = Scope::Voice;

    ParamDef& add (std::string id, std::string name, PType t, float mn, float mx, float df)
    {
        ParamDef d;
        d.id = std::move (id); d.name = std::move (name); d.group = group;
        d.type = t; d.min = mn; d.max = mx; d.def = df; d.scope = scope;
        defs.push_back (std::move (d));
        return defs.back();
    }
    ParamDef& f (std::string id, std::string name, float mn, float mx, float df, Unit u = Unit::None)
    {
        auto& d = add (std::move (id), std::move (name), PType::Float, mn, mx, df);
        d.unit = u;
        if (u == Unit::Percent && d.displayScale == 1.0f) d.displayScale = (mx <= 1.0f ? 100.0f : 1.0f);
        return d;
    }
    ParamDef& fs (std::string id, std::string name, float mn, float mx, float df, float mid, Unit u)
    {
        auto& d = f (std::move (id), std::move (name), mn, mx, df, u);
        d.scale = Scale::SkewMid; d.mid = mid;
        return d;
    }
    ParamDef& flog (std::string id, std::string name, float mn, float mx, float df, Unit u)
    {
        auto& d = f (std::move (id), std::move (name), mn, mx, df, u);
        d.scale = Scale::Log;
        return d;
    }
    ParamDef& i (std::string id, std::string name, int mn, int mx, int df, Unit u = Unit::None)
    {
        auto& d = add (std::move (id), std::move (name), PType::Int, (float) mn, (float) mx, (float) df);
        d.unit = u;
        return d;
    }
    ParamDef& c (std::string id, std::string name, std::vector<std::string> ch, int df)
    {
        auto& d = add (std::move (id), std::move (name), PType::Choice, 0.0f, (float) (ch.size() - 1), (float) df);
        d.choices = std::move (ch);
        return d;
    }
    ParamDef& b (std::string id, std::string name, bool df)
    {
        return add (std::move (id), std::move (name), PType::Bool, 0.0f, 1.0f, df ? 1.0f : 0.0f);
    }
};

std::vector<ParamDef> buildDefs()
{
    Builder B;
    const std::vector<std::string> destChoices { "F1", "F2", "Both", "Direct" };

    // ---- Global / voice (order == P::Global)
    B.group = "Master"; B.scope = Scope::Global;
    B.f ("master_vol", "Master Volume", -60.0f, 6.0f, -6.0f, Unit::Decibel);
    B.f ("master_width", "Master Width", 0.0f, 2.0f, 1.0f, Unit::Percent).displayScale = 100.0f;
    { auto& d = B.flog ("bass_mono", "Bass Mono", 55.0f, 250.0f, 55.0f, Unit::Hz); d.offValueBelow = 60.0f; }
    B.b ("softclip", "Soft Clip", true);
    B.c ("quality", "Quality", { "Eco", "High", "Ultra" }, 1);
    B.group = "Voice"; B.scope = Scope::Voice;
    B.f ("drift", "Drift", 0.0f, 1.0f, 0.15f, Unit::Percent);
    B.i ("poly", "Polyphony", 1, 32, 16);
    B.c ("voice_mode", "Voice Mode", { "Poly", "Mono", "Legato" }, 0);
    B.c ("mono_priority", "Mono Priority", { "Last", "Low", "High" }, 0);
    B.fs ("glide_time", "Glide Time", 0.0f, 5.0f, 0.0f, 0.2f, Unit::Seconds);
    B.c ("glide_mode", "Glide Mode", { "Time", "Rate" }, 0);
    B.b ("glide_legato", "Glide Legato Only", false);
    B.i ("pb_range", "Pitch Bend Range", 0, 48, 2, Unit::Semitones);
    B.f ("vel_curve", "Velocity Curve", -1.0f, 1.0f, 0.0f);
    B.f ("voice_pitch", "Voice Pitch", -24.0f, 24.0f, 0.0f, Unit::Semitones).fast = true;
    B.f ("amp_level", "Amp", 0.0f, 1.0f, 1.0f, Unit::GainDb).fast = true;
    B.c ("filter_routing", "Filter Routing", { "Serial", "Parallel" }, 0);
    B.b ("mpe_on", "MPE", false);
    B.i ("mpe_pb_range", "MPE Bend Range", 0, 96, 48, Unit::Semitones);
    assert ((int) B.defs.size() == P::NumGlobal);

    // ---- Oscillators
    const std::vector<std::string> warpChoices { "None", "Sync", "Bend+", "Bend-", "Bend+/-", "PWM", "Mirror", "Asym+", "Asym-",
                                                 "Quantize", "Remap", "FM (Other)", "FM (Sub)", "AM", "RM" };
    for (int o = 0; o < 2; ++o)
    {
        const std::string p = o == 0 ? "oscA_" : "oscB_";
        const std::string n = o == 0 ? "Osc A " : "Osc B ";
        B.group = o == 0 ? "Osc A" : "Osc B"; B.scope = Scope::Voice;
        B.f (p + "wt_pos", n + "WT Position", 0.0f, 1.0f, 0.0f).fast = true;
        B.i (p + "oct", n + "Octave", -3, 3, 0, Unit::Octave);
        B.i (p + "semi", n + "Semitone", -12, 12, 0, Unit::Semitones);
        B.i (p + "fine", n + "Fine", -100, 100, 0, Unit::Cents);
        B.f (p + "pitch", n + "Pitch", -24.0f, 24.0f, 0.0f, Unit::Semitones).fast = true;
        B.f (p + "level", n + "Level", 0.0f, 1.0f, o == 0 ? 0.75f : 0.0f, Unit::GainDb).fast = true;
        B.f (p + "pan", n + "Pan", -1.0f, 1.0f, 0.0f, Unit::Pan).fast = true;
        { auto& d = B.f (p + "phase", n + "Phase", 0.0f, 1.0f, 0.0f, Unit::Degrees); d.displayScale = 360.0f; }
        B.f (p + "rand_phase", n + "Random Phase", 0.0f, 1.0f, 1.0f, Unit::Percent);
        B.c (p + "warp_mode", n + "Warp Mode", warpChoices, 0);
        B.f (p + "warp_amt", n + "Warp Amount", 0.0f, 1.0f, 0.0f, Unit::Percent).fast = true;
        B.i (p + "unison", n + "Unison", 1, 16, 1);
        B.fs (p + "detune", n + "Detune", 0.0f, 1.0f, 0.2f, 0.15f, Unit::Percent);
        B.f (p + "blend", n + "Blend", 0.0f, 1.0f, 0.7f, Unit::Percent);
        B.f (p + "spread", n + "Stereo Spread", 0.0f, 1.0f, 0.6f, Unit::Percent);
        B.f (p + "wt_spread", n + "Unison WT Spread", 0.0f, 1.0f, 0.0f, Unit::Percent);
        B.c (p + "dest", n + "Destination", destChoices, 0);
    }
    assert ((int) B.defs.size() == P::SubBase);

    B.group = "Sub";
    B.c ("sub_shape", "Sub Shape", { "Sine", "Triangle", "Square", "Saw" }, 0);
    B.c ("sub_oct", "Sub Octave", { "-1", "-2" }, 0);
    B.f ("sub_level", "Sub Level", 0.0f, 1.0f, 0.0f, Unit::GainDb).fast = true;
    B.f ("sub_pan", "Sub Pan", -1.0f, 1.0f, 0.0f, Unit::Pan).fast = true;
    B.c ("sub_dest", "Sub Destination", destChoices, 0);

    B.group = "Noise";
    B.c ("noise_type", "Noise Type", { "White", "Pink", "Brown" }, 0);
    B.f ("noise_tone", "Noise Tone", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("noise_level", "Noise Level", 0.0f, 1.0f, 0.0f, Unit::GainDb).fast = true;
    B.f ("noise_pan", "Noise Pan", -1.0f, 1.0f, 0.0f, Unit::Pan).fast = true;
    B.c ("noise_dest", "Noise Destination", destChoices, 0);
    B.b ("noise_stereo", "Noise Stereo", true);
    assert ((int) B.defs.size() == P::FiltBase);

    const std::vector<std::string> filterChoices { "LP12", "LP24", "HP12", "HP24", "BP12", "BP24", "Notch", "Peak",
                                                   "Comb+", "Comb-", "Ladder", "Diode", "Formant" };
    for (int f = 0; f < 2; ++f)
    {
        const std::string p = f == 0 ? "f1_" : "f2_";
        const std::string n = f == 0 ? "Filter 1 " : "Filter 2 ";
        B.group = f == 0 ? "Filter 1" : "Filter 2";
        B.b (p + "enable", n + "Enable", f == 0);
        B.c (p + "type", n + "Type", filterChoices, 1);
        B.flog (p + "cutoff", n + "Cutoff", 20.0f, 20000.0f, 20000.0f, Unit::Hz).fast = true;
        B.f (p + "reso", n + "Resonance", 0.0f, 1.0f, 0.1f, Unit::Percent);
        B.f (p + "drive", n + "Drive", 0.0f, 24.0f, 0.0f, Unit::Decibel);
        B.f (p + "keytrack", n + "Key Track", 0.0f, 1.0f, 0.0f, Unit::Percent);
        B.f (p + "vowel", n + "Vowel", 0.0f, 1.0f, 0.0f);
        B.f (p + "mix", n + "Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);
    }
    assert ((int) B.defs.size() == P::EnvBase);

    for (int e = 0; e < 3; ++e)
    {
        const std::string p = "env" + std::to_string (e + 1) + "_";
        const std::string n = "Env " + std::to_string (e + 1) + " ";
        B.group = "Env " + std::to_string (e + 1);
        B.fs (p + "delay", n + "Delay", 0.0f, 10.0f, 0.0f, 0.2f, Unit::Seconds);
        B.fs (p + "attack", n + "Attack", 0.0f, 10.0f, 0.001f, 0.2f, Unit::Seconds);
        B.fs (p + "hold", n + "Hold", 0.0f, 10.0f, 0.0f, 0.2f, Unit::Seconds);
        B.fs (p + "decay", n + "Decay", 0.0f, 20.0f, 0.3f, 0.5f, Unit::Seconds);
        B.f (p + "sustain", n + "Sustain", 0.0f, 1.0f, e == 0 ? 1.0f : 0.0f, Unit::Percent);
        B.fs (p + "release", n + "Release", 0.0f, 20.0f, 0.05f, 0.5f, Unit::Seconds);
        B.f (p + "curve_a", n + "Attack Curve", -1.0f, 1.0f, 0.0f);
        // decay/release default to an exponential-like curve (natural, analog-style fades)
        B.f (p + "curve_d", n + "Decay Curve", -1.0f, 1.0f, 0.5f);
        B.f (p + "curve_r", n + "Release Curve", -1.0f, 1.0f, 0.5f);
        B.f (p + "vel_amt", n + "Velocity", 0.0f, 1.0f, e == 0 ? 0.5f : 0.0f, Unit::Percent);
    }
    assert ((int) B.defs.size() == P::LfoBase);

    for (int l = 0; l < 4; ++l)
    {
        const std::string p = "lfo" + std::to_string (l + 1) + "_";
        const std::string n = "LFO " + std::to_string (l + 1) + " ";
        B.group = "LFO " + std::to_string (l + 1);
        B.flog (p + "rate", n + "Rate", 0.01f, 50.0f, 1.0f, Unit::Hz);
        B.b (p + "sync", n + "Sync", false);
        B.c (p + "div", n + "Division", syncDivNames(), 15);
        B.c (p + "shape", n + "Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Random", "Noise", "Custom" }, 0);
        B.c (p + "mode", n + "Mode", { "Free", "Retrig", "Env" }, 1);
        B.c (p + "polarity", n + "Polarity", { "Uni", "Bi" }, 1);
        { auto& d = B.f (p + "phase", n + "Phase", 0.0f, 1.0f, 0.0f, Unit::Degrees); d.displayScale = 360.0f; }
        B.fs (p + "delay", n + "Delay", 0.0f, 10.0f, 0.0f, 0.5f, Unit::Seconds);
        B.fs (p + "fade", n + "Fade In", 0.0f, 10.0f, 0.0f, 0.5f, Unit::Seconds);
        B.f (p + "smooth", n + "Smooth", 0.0f, 1.0f, 0.0f, Unit::Percent);
        B.b (p + "oneshot", n + "One Shot", false);
    }
    assert ((int) B.defs.size() == P::MsegBase);

    B.group = "MSEG";
    B.fs ("mseg_rate", "MSEG Length", 0.01f, 30.0f, 1.0f, 1.0f, Unit::Seconds);
    B.b ("mseg_sync", "MSEG Sync", false);
    {
        // MSEG sync spans 1/16 .. 16 bars; we reuse the shared list (clamped in DSP).
        B.c ("mseg_div", "MSEG Division", syncDivNames(), 9);
    }
    B.c ("mseg_mode", "MSEG Mode", { "One-shot", "Loop", "Sustain Loop" }, 0);

    B.group = "Macros"; B.scope = Scope::Source;
    for (int m = 0; m < 4; ++m)
        B.f ("macro" + std::to_string (m + 1), "Macro " + std::to_string (m + 1), 0.0f, 1.0f, 0.0f, Unit::Percent);
    assert ((int) B.defs.size() == P::ArpBase);

    B.group = "Arp"; B.scope = Scope::Global;
    B.b ("arp_on", "Arp", false);
    B.c ("arp_mode", "Arp Mode", { "Up", "Down", "Up-Down", "Random", "As Played", "Chord" }, 0);
    B.i ("arp_octaves", "Arp Octaves", 1, 4, 1);
    B.c ("arp_div", "Arp Rate", syncDivNames(), 21); // 1/16
    B.f ("arp_gate", "Arp Gate", 0.01f, 2.0f, 0.8f, Unit::Percent).displayScale = 100.0f;
    B.f ("arp_swing", "Arp Swing", 0.0f, 0.75f, 0.0f, Unit::Percent).displayScale = 100.0f;
    B.b ("arp_latch", "Arp Latch", false);
    B.c ("arp_vel_mode", "Arp Velocity", { "As Played", "Fixed", "Accent" }, 0);
    B.f ("arp_vel_fixed", "Arp Fixed Velocity", 0.0f, 1.0f, 0.8f, Unit::Percent);
    {
        // rate multiplier for macros: < 1/3 -> half speed, < 2/3 -> as set, else double speed
        auto& d = B.f ("arp_speed", "Arp Speed", 0.0f, 1.0f, 0.5f);
        d.displayScale = 1.0f;
    }

    B.group = "Chord";
    B.b ("chord_on", "Chord Mode", false);
    B.f ("chord_strum", "Strum", 0.0f, 200.0f, 0.0f, Unit::Ms);
    B.c ("chord_dir", "Strum Direction", { "Up", "Down", "Alternate" }, 0);
    assert ((int) B.defs.size() == P::DistBase);

    // ---- FX (global)
    B.group = "FX Distortion";
    B.b ("dist_on", "Distortion", false);
    B.c ("dist_mode", "Dist Mode", { "Tanh", "Hard Clip", "Fold", "Tube", "Bitcrush" }, 0);
    B.f ("dist_drive", "Dist Drive", 0.0f, 36.0f, 6.0f, Unit::Decibel);
    B.f ("dist_tone", "Dist Tone", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("dist_mix", "Dist Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);
    B.f ("dist_bits", "Crush Bits", 2.0f, 16.0f, 12.0f);
    B.flog ("dist_downsample", "Crush Rate", 1000.0f, 48000.0f, 48000.0f, Unit::Hz);

    B.group = "FX Tape";
    B.b ("tape_on", "Tape", false);
    B.flog ("tape_wow_rate", "Wow Rate", 0.1f, 4.0f, 0.6f, Unit::Hz);
    B.f ("tape_wow_depth", "Wow Depth", 0.0f, 1.0f, 0.2f, Unit::Percent);
    B.f ("tape_flutter", "Flutter", 0.0f, 1.0f, 0.1f, Unit::Percent);
    B.f ("tape_sat", "Saturation", 0.0f, 1.0f, 0.3f, Unit::Percent);
    B.f ("tape_hiss", "Hiss", 0.0f, 1.0f, 0.0f, Unit::Percent);
    B.f ("tape_age", "Age", 0.0f, 1.0f, 0.2f, Unit::Percent);
    B.f ("tape_mix", "Tape Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);

    B.group = "FX Dimension";
    B.b ("dim_on", "Dimension", false);
    B.f ("dim_size", "Dimension Size", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("dim_mix", "Dimension Mix", 0.0f, 1.0f, 0.5f, Unit::Percent);

    B.group = "FX Chorus";
    B.b ("chorus_on", "Chorus", false);
    B.flog ("chorus_rate", "Chorus Rate", 0.05f, 8.0f, 0.5f, Unit::Hz);
    B.f ("chorus_depth", "Chorus Depth", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("chorus_fb", "Chorus Feedback", 0.0f, 0.9f, 0.1f, Unit::Percent).displayScale = 100.0f;
    B.f ("chorus_mix", "Chorus Mix", 0.0f, 1.0f, 0.5f, Unit::Percent);

    B.group = "FX Phaser";
    B.b ("phaser_on", "Phaser", false);
    B.i ("phaser_stages", "Phaser Stages", 2, 12, 4);
    B.flog ("phaser_rate", "Phaser Rate", 0.02f, 10.0f, 0.3f, Unit::Hz);
    B.f ("phaser_depth", "Phaser Depth", 0.0f, 1.0f, 0.6f, Unit::Percent);
    B.f ("phaser_fb", "Phaser Feedback", -0.95f, 0.95f, 0.3f, Unit::Percent).displayScale = 100.0f;
    B.f ("phaser_mix", "Phaser Mix", 0.0f, 1.0f, 0.5f, Unit::Percent);

    B.group = "FX Flanger";
    B.b ("flanger_on", "Flanger", false);
    B.flog ("flanger_rate", "Flanger Rate", 0.02f, 10.0f, 0.2f, Unit::Hz);
    B.f ("flanger_depth", "Flanger Depth", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("flanger_fb", "Flanger Feedback", -0.95f, 0.95f, 0.5f, Unit::Percent).displayScale = 100.0f;
    B.f ("flanger_mix", "Flanger Mix", 0.0f, 1.0f, 0.5f, Unit::Percent);

    B.group = "FX Rotary";
    B.b ("rotary_on", "Rotary", false);
    B.f ("rotary_fast", "Rotary Fast", 0.0f, 1.0f, 0.0f).switchLike = true;
    B.f ("rotary_accel", "Rotary Acceleration", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("rotary_balance", "Horn/Drum Balance", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("rotary_drive", "Rotary Drive", 0.0f, 1.0f, 0.2f, Unit::Percent);
    B.f ("rotary_width", "Mic Width", 0.0f, 1.0f, 0.7f, Unit::Percent);
    B.f ("rotary_mix", "Rotary Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);
    B.b ("rotary_mw", "Mod Wheel Speed", true);

    B.group = "FX Delay";
    B.b ("delay_on", "Delay", false);
    B.b ("delay_sync", "Delay Sync", true);
    B.c ("delay_div", "Delay Division", syncDivNames(), 19); // 1/8D
    B.fs ("delay_time", "Delay Time", 1.0f, 2000.0f, 375.0f, 300.0f, Unit::Ms);
    B.b ("delay_pingpong", "Ping-Pong", false);
    B.f ("delay_fb", "Delay Feedback", 0.0f, 0.95f, 0.35f, Unit::Percent).displayScale = 100.0f;
    B.flog ("delay_hp", "Delay HP", 20.0f, 2000.0f, 80.0f, Unit::Hz);
    B.flog ("delay_lp", "Delay LP", 500.0f, 20000.0f, 8000.0f, Unit::Hz);
    B.f ("delay_wow", "Delay Wow", 0.0f, 1.0f, 0.0f, Unit::Percent);
    B.f ("delay_duck", "Delay Ducking", 0.0f, 1.0f, 0.0f, Unit::Percent);
    B.f ("delay_mix", "Delay Mix", 0.0f, 1.0f, 0.3f, Unit::Percent);

    B.group = "FX Reverb";
    B.b ("reverb_on", "Reverb", false);
    B.c ("reverb_lines", "Reverb Lines", { "8", "16" }, 1);
    B.f ("reverb_size", "Reverb Size", 0.0f, 1.0f, 0.6f, Unit::Percent);
    B.fs ("reverb_decay", "Reverb Decay", 0.2f, 20.0f, 3.0f, 3.0f, Unit::Seconds);
    B.f ("reverb_damp", "Reverb Damping", 0.0f, 1.0f, 0.4f, Unit::Percent);
    B.fs ("reverb_predelay", "Pre-Delay", 0.0f, 0.25f, 0.01f, 0.05f, Unit::Seconds);
    B.f ("reverb_mod", "Reverb Modulation", 0.0f, 1.0f, 0.3f, Unit::Percent);
    B.f ("reverb_width", "Reverb Width", 0.0f, 1.0f, 1.0f, Unit::Percent);
    B.flog ("reverb_lowcut", "Reverb Low Cut", 20.0f, 1000.0f, 100.0f, Unit::Hz);
    B.f ("reverb_freeze", "Reverb Freeze", 0.0f, 1.0f, 0.0f).switchLike = true;
    B.f ("reverb_shimmer", "Shimmer", 0.0f, 1.0f, 0.0f, Unit::Percent);
    B.f ("reverb_mix", "Reverb Mix", 0.0f, 1.0f, 0.3f, Unit::Percent);

    B.group = "FX Compressor";
    B.b ("comp_on", "Compressor", false);
    B.f ("comp_thresh", "Comp Threshold", -60.0f, 0.0f, -18.0f, Unit::Decibel);
    B.fs ("comp_ratio", "Comp Ratio", 1.0f, 20.0f, 4.0f, 4.0f, Unit::Ratio);
    B.fs ("comp_attack", "Comp Attack", 0.1f, 200.0f, 10.0f, 15.0f, Unit::Ms);
    B.fs ("comp_release", "Comp Release", 5.0f, 2000.0f, 150.0f, 150.0f, Unit::Ms);
    B.f ("comp_knee", "Comp Knee", 0.0f, 24.0f, 6.0f, Unit::Decibel);
    B.f ("comp_makeup", "Comp Makeup", 0.0f, 24.0f, 0.0f, Unit::Decibel);
    B.f ("comp_mix", "Comp Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);

    B.group = "FX Multiband";
    B.b ("mb_on", "Multiband", false);
    B.f ("mb_depth", "MB Depth", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("mb_time", "MB Time", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("mb_up", "MB Upward", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.f ("mb_down", "MB Downward", 0.0f, 1.0f, 0.5f, Unit::Percent);
    B.flog ("mb_low_x", "MB Low Crossover", 50.0f, 1000.0f, 200.0f, Unit::Hz);
    B.flog ("mb_high_x", "MB High Crossover", 1000.0f, 12000.0f, 2500.0f, Unit::Hz);
    B.f ("mb_mix", "MB Mix", 0.0f, 1.0f, 1.0f, Unit::Percent);

    B.group = "FX EQ";
    B.b ("eq_on", "EQ", false);
    B.flog ("eq_ls_freq", "Low Shelf Freq", 20.0f, 1000.0f, 120.0f, Unit::Hz);
    B.f ("eq_ls_gain", "Low Shelf Gain", -18.0f, 18.0f, 0.0f, Unit::Decibel);
    B.flog ("eq_pk_freq", "Peak Freq", 50.0f, 15000.0f, 1000.0f, Unit::Hz);
    B.f ("eq_pk_gain", "Peak Gain", -18.0f, 18.0f, 0.0f, Unit::Decibel);
    B.flog ("eq_pk_q", "Peak Q", 0.2f, 10.0f, 0.8f, Unit::None);
    B.flog ("eq_hs_freq", "High Shelf Freq", 1000.0f, 18000.0f, 6000.0f, Unit::Hz);
    B.f ("eq_hs_gain", "High Shelf Gain", -18.0f, 18.0f, 0.0f, Unit::Decibel);
    B.flog ("eq_hp", "EQ High-Pass", 20.0f, 2000.0f, 20.0f, Unit::Hz);
    B.flog ("eq_lp", "EQ Low-Pass", 1000.0f, 20000.0f, 20000.0f, Unit::Hz);
    assert ((int) B.defs.size() == P::ModDepthBase);

    B.group = "Mod Matrix"; B.scope = Scope::Voice;
    for (int m = 0; m < P::NumModSlots; ++m)
    {
        char id[32], nm[32];
        std::snprintf (id, sizeof (id), "mod%02d_depth", m + 1);
        std::snprintf (nm, sizeof (nm), "Mod %d Depth", m + 1);
        B.f (id, nm, -1.0f, 1.0f, 0.0f, Unit::Percent).displayScale = 100.0f;
    }
    assert ((int) B.defs.size() == P::NumParams);
    return std::move (B.defs);
}
} // namespace

const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> defs = buildDefs();
    return defs;
}

int numParams() { return P::NumParams; }

int paramIndex (const std::string& id)
{
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        const auto& d = paramDefs();
        for (size_t i = 0; i < d.size(); ++i) m[d[i].id] = (int) i;
        return m;
    }();
    auto it = map.find (id);
    return it == map.end() ? -1 : it->second;
}

} // namespace prism
