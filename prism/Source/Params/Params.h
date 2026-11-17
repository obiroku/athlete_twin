#pragma once
// PRISM parameter registry. Parameter IDs are permanent (CLAUDE.md):
// never rename or remove one; deprecate instead. Indices below are
// compile-time constants; paramDefs() builds the list in exactly this order.

#include <string>
#include <vector>
#include <cstdint>

namespace prism
{

enum class PType : uint8_t { Float, Int, Choice, Bool };
enum class Scale : uint8_t { Linear, Log, SkewMid };
enum class Unit : uint8_t { None, Percent, Hz, Seconds, Ms, Decibel, GainDb, Semitones, Cents, Octave, Degrees, Ratio, Pan };

// Where a parameter is consumed. Voice params are modulated per voice;
// Global params (FX, master, arp) are modulated once per control tick.
enum class Scope : uint8_t { Voice, Global, Source };

struct ParamDef
{
    std::string id, name, group;
    PType type = PType::Float;
    float min = 0, max = 1, def = 0;
    Scale scale = Scale::Linear;
    float mid = 0;              // SkewMid: real value at normalized 0.5
    Unit unit = Unit::None;
    std::vector<std::string> choices;
    Scope scope = Scope::Voice;
    bool fast = false;          // per-sample modulation destination
    float displayScale = 1.0f;  // e.g. 100 for 0..1 shown as percent
    float offValueBelow = -1e30f; // displays "Off" below this real value
    bool switchLike = false;      // continuous 0..1 shown as Off/On (>= 0.5 = on), so it can be modulated

    bool isModDest() const { return type == PType::Float && scope != Scope::Source; }
    float toNorm (float real) const noexcept;
    float fromNorm (float norm) const noexcept;
    std::string toText (float real) const;
    bool fromText (const std::string& text, float& real) const;
    float snap (float real) const noexcept; // rounds Int/Choice/Bool
};

const std::vector<ParamDef>& paramDefs();
int numParams();
int paramIndex (const std::string& id); // -1 if unknown

// ---- Sync divisions shared by LFOs, delay, arp, MSEG ----
const std::vector<std::string>& syncDivNames();
double syncDivBeats (int index); // length in quarter notes
int syncDivIndex (const std::string& name);

namespace P
{
    enum Global : int
    {
        master_vol, master_width, bass_mono, softclip, quality, drift, poly, voice_mode,
        mono_priority, glide_time, glide_mode, glide_legato, pb_range, vel_curve, voice_pitch,
        amp_level, filter_routing, mpe_on, mpe_pb_range,
        NumGlobal
    };

    enum OscP : int
    {
        wt_pos, oct, semi, fine, pitch, level, pan, phase, rand_phase, warp_mode, warp_amt,
        unison, detune, blend, spread, wt_spread, dest,
        warp2_mode, warp2_amt, uni_mode, uni_stack, uni_range,
        NumOscP
    };
    constexpr int OscBase = NumGlobal;
    constexpr int osc (int o, int p) { return OscBase + o * NumOscP + p; }

    enum SubP : int { sub_shape, sub_oct, sub_level, sub_pan, sub_dest, NumSubP };
    constexpr int SubBase = OscBase + 2 * NumOscP;
    constexpr int sub (int p) { return SubBase + p; }

    enum NoiseP : int { noise_type, noise_tone, noise_level, noise_pan, noise_dest, noise_stereo, NumNoiseP };
    constexpr int NoiseBase = SubBase + NumSubP;
    constexpr int noise (int p) { return NoiseBase + p; }

    enum FiltP : int { f_enable, f_type, f_cutoff, f_reso, f_drive, f_keytrack, f_vowel, f_mix, NumFiltP };
    constexpr int FiltBase = NoiseBase + NumNoiseP;
    constexpr int filt (int f, int p) { return FiltBase + f * NumFiltP + p; }

    enum EnvP : int { e_delay, e_attack, e_hold, e_decay, e_sustain, e_release, e_curve_a, e_curve_d, e_curve_r, e_vel_amt, NumEnvP };
    constexpr int EnvBase = FiltBase + 2 * NumFiltP;
    constexpr int env (int e, int p) { return EnvBase + e * NumEnvP + p; }

    enum LfoP : int { l_rate, l_sync, l_div, l_shape, l_mode, l_polarity, l_phase, l_delay, l_fade, l_smooth, l_oneshot, NumLfoP };
    constexpr int LfoBase = EnvBase + 3 * NumEnvP;
    constexpr int lfo (int l, int p) { return LfoBase + l * NumLfoP + p; }

    enum MsegP : int { mseg_rate, mseg_sync, mseg_div, mseg_mode, NumMsegP };
    constexpr int MsegBase = LfoBase + 4 * NumLfoP;
    constexpr int mseg (int p) { return MsegBase + p; }

    constexpr int MacroBase = MsegBase + NumMsegP;
    constexpr int macro (int m) { return MacroBase + m; }

    enum ArpP : int { arp_on, arp_mode, arp_octaves, arp_div, arp_gate, arp_swing, arp_latch, arp_vel_mode, arp_vel_fixed, arp_speed, NumArpP };
    constexpr int ArpBase = MacroBase + 4;
    constexpr int arp (int p) { return ArpBase + p; }

    enum ChordP : int { chord_on, chord_strum, chord_dir, NumChordP };
    constexpr int ChordBase = ArpBase + NumArpP;
    constexpr int chord (int p) { return ChordBase + p; }

    // ---- FX ----
    enum DistP : int { dist_on, dist_mode, dist_drive, dist_tone, dist_mix, dist_bits, dist_downsample, NumDistP };
    constexpr int DistBase = ChordBase + NumChordP;
    enum TapeP : int { tape_on, tape_wow_rate, tape_wow_depth, tape_flutter, tape_sat, tape_hiss, tape_age, tape_mix, NumTapeP };
    constexpr int TapeBase = DistBase + NumDistP;
    enum DimP : int { dim_on, dim_size, dim_mix, NumDimP };
    constexpr int DimBase = TapeBase + NumTapeP;
    enum ChorusP : int { chorus_on, chorus_rate, chorus_depth, chorus_fb, chorus_mix, NumChorusP };
    constexpr int ChorusBase = DimBase + NumDimP;
    enum PhaserP : int { phaser_on, phaser_stages, phaser_rate, phaser_depth, phaser_fb, phaser_mix, NumPhaserP };
    constexpr int PhaserBase = ChorusBase + NumChorusP;
    enum FlangerP : int { flanger_on, flanger_rate, flanger_depth, flanger_fb, flanger_mix, NumFlangerP };
    constexpr int FlangerBase = PhaserBase + NumPhaserP;
    enum RotaryP : int { rotary_on, rotary_fast, rotary_accel, rotary_balance, rotary_drive, rotary_width, rotary_mix, rotary_mw, NumRotaryP };
    constexpr int RotaryBase = FlangerBase + NumFlangerP;
    enum DelayP : int { delay_on, delay_sync, delay_div, delay_time, delay_pingpong, delay_fb, delay_hp, delay_lp, delay_wow, delay_duck, delay_mix, NumDelayP };
    constexpr int DelayBase = RotaryBase + NumRotaryP;
    enum ReverbP : int { reverb_on, reverb_lines, reverb_size, reverb_decay, reverb_damp, reverb_predelay, reverb_mod, reverb_width, reverb_lowcut, reverb_freeze, reverb_shimmer, reverb_mix, NumReverbP };
    constexpr int ReverbBase = DelayBase + NumDelayP;
    enum CompP : int { comp_on, comp_thresh, comp_ratio, comp_attack, comp_release, comp_knee, comp_makeup, comp_mix, NumCompP };
    constexpr int CompBase = ReverbBase + NumReverbP;
    enum MbP : int { mb_on, mb_depth, mb_time, mb_up, mb_down, mb_low_x, mb_high_x, mb_mix, NumMbP };
    constexpr int MbBase = CompBase + NumCompP;
    enum EqP : int { eq_on, eq_ls_freq, eq_ls_gain, eq_pk_freq, eq_pk_gain, eq_pk_q, eq_hs_freq, eq_hs_gain, eq_hp, eq_lp, NumEqP };
    constexpr int EqBase = MbBase + NumMbP;

    constexpr int NumModSlots = 64;
    constexpr int ModDepthBase = EqBase + NumEqP;
    constexpr int modDepth (int slot) { return ModDepthBase + slot; } // slot 0..63

    constexpr int NumParams = ModDepthBase + NumModSlots;
}

// Choice enumerations (indices into the parameter's choice list)
enum class WarpMode : int { None, Sync, BendPlus, BendMinus, BendBoth, PWM, Mirror, AsymPlus, AsymMinus, Quantize, Remap, FMOther, FMSub, AM, RM, Flip, Drive, Fold, Count };
enum class UniMode : int { Linear, Super, Exp, Inv, Random };
enum class UniStack : int { Off, Oct, OctFifth, CenterMinus12, CenterMinus24 };
enum class Dest : int { F1, F2, Both, Direct };
enum class FilterType : int { LP12, LP24, HP12, HP24, BP12, BP24, Notch, Peak, CombPlus, CombMinus, Ladder, Diode, Formant, LP6, DJ, Diffuser, Count };
enum class VoiceMode : int { Poly, Mono, Legato };
enum class Priority : int { Last, Low, High };
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Noise, Custom };
enum class LfoMode : int { Free, Retrig, Env };
enum class MsegMode : int { OneShot, Loop, SustainLoop };
enum class Quality : int { Eco, High, Ultra };
enum class DistMode : int { Tanh, HardClip, Fold, Tube, Bitcrush };
enum class ArpMode : int { Up, Down, UpDown, Random, AsPlayed, Chord };
enum class ArpVel : int { AsPlayed, Fixed, Accent };
enum class StrumDir : int { Up, Down, Alternate };

} // namespace prism
