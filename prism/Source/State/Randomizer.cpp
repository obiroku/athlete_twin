#include "Randomizer.h"
#include "../DSP/FactoryTables.h"
#include <cmath>

namespace prism
{
namespace
{
enum class Section { None, Osc, Filter, Env, Mod, FX, Arp };

Section sectionOf (int p)
{
    if (p >= P::OscBase && p < P::FiltBase) return Section::Osc;
    if (p >= P::FiltBase && p < P::EnvBase) return Section::Filter;
    if (p >= P::EnvBase && p < P::LfoBase) return Section::Env;
    if ((p >= P::LfoBase && p < P::MacroBase) || (p >= P::ModDepthBase && p < P::NumParams)) return Section::Mod;
    if (p >= P::DistBase && p < P::ModDepthBase) return Section::FX;
    if (p >= P::ArpBase && p < P::DistBase) return Section::Arp;
    return Section::None;
}

bool locked (Section s, const RandomLocks& l)
{
    switch (s)
    {
        case Section::Osc: return l.osc;
        case Section::Filter: return l.filter;
        case Section::Env: return l.env;
        case Section::Mod: return l.mod;
        case Section::FX: return l.fx;
        case Section::Arp: return l.arp;
        default: return true; // master/voice globals are never randomised
    }
}

bool excluded (int p)
{
    // parameters that randomise badly or are user settings
    static const int ex[] = { P::filt (0, P::f_enable), P::osc (0, P::unison), P::osc (1, P::unison),
                              P::lfo (0, P::l_mode), P::lfo (1, P::l_mode), P::lfo (2, P::l_mode), P::lfo (3, P::l_mode),
                              P::osc (0, P::dest), P::osc (1, P::dest), P::sub (P::sub_dest), P::noise (P::noise_dest),
                              P::ReverbBase + P::reverb_freeze, P::arp (P::arp_on), P::arp (P::arp_latch) };
    for (int e : ex) if (e == p) return true;
    return false;
}
} // namespace

void applyRandomConstraints (PresetData& p)
{
    auto& v = p.params;
    v[(size_t) P::env (0, P::e_attack)] = std::min (v[(size_t) P::env (0, P::e_attack)], 2.0f);
    v[(size_t) P::env (0, P::e_release)] = std::min (v[(size_t) P::env (0, P::e_release)], 6.0f);
    v[(size_t) P::env (0, P::e_delay)] = std::min (v[(size_t) P::env (0, P::e_delay)], 0.2f);
    // the amp must actually open: sustain or a decent decay
    if (v[(size_t) P::env (0, P::e_sustain)] < 0.3f) v[(size_t) P::env (0, P::e_decay)] = std::max (v[(size_t) P::env (0, P::e_decay)], 0.25f);
    v[(size_t) P::amp_level] = std::max (v[(size_t) P::amp_level], 0.7f);

    // cutoff >= 200 Hz unless an envelope opens it
    for (int f = 0; f < 2; ++f)
    {
        const int cut = P::filt (f, P::f_cutoff);
        bool envOpens = false;
        for (int s = 0; s < P::NumModSlots; ++s)
        {
            const auto& r = p.extras.routings[(size_t) s];
            if (r.active() && r.dest == cut && r.src >= (int) Src::Env1 && r.src <= (int) Src::Env3 && v[(size_t) P::modDepth (s)] > 0.15f)
                envOpens = true;
        }
        if (! envOpens) v[(size_t) cut] = std::max (v[(size_t) cut], 200.0f);
        v[(size_t) P::filt (f, P::f_reso)] = std::min (v[(size_t) P::filt (f, P::f_reso)], 0.85f);
        v[(size_t) P::filt (f, P::f_mix)] = std::max (v[(size_t) P::filt (f, P::f_mix)], 0.5f);
        // formant/comb/notch with a very low cutoff can still be quiet; keep types that pass energy
        const int ft = P::filt (f, P::f_type);
        if (v[(size_t) ft] == (float) FilterType::HP24 || v[(size_t) ft] == (float) FilterType::HP12)
            v[(size_t) cut] = std::min (v[(size_t) cut], 2000.0f);
        if (v[(size_t) ft] == (float) FilterType::BP12 || v[(size_t) ft] == (float) FilterType::BP24)
            v[(size_t) cut] = juce::jlimit (300.0f, 4000.0f, v[(size_t) cut]);
    }
    // at least one audible source routed through a sane path
    const float levelA = v[(size_t) P::osc (0, P::level)];
    const float levelB = v[(size_t) P::osc (1, P::level)];
    if (std::max (levelA, levelB) < 0.4f) v[(size_t) P::osc (0, P::level)] = 0.75f;
    v[(size_t) P::osc (0, P::pitch)] = juce::jlimit (-12.0f, 12.0f, v[(size_t) P::osc (0, P::pitch)]);
    v[(size_t) P::osc (1, P::pitch)] = juce::jlimit (-12.0f, 12.0f, v[(size_t) P::osc (1, P::pitch)]);
    v[(size_t) P::osc (0, P::oct)] = juce::jlimit (-1.0f, 1.0f, v[(size_t) P::osc (0, P::oct)]);
    v[(size_t) P::osc (1, P::oct)] = juce::jlimit (-2.0f, 1.0f, v[(size_t) P::osc (1, P::oct)]);
    // output stays under the limiter
    v[(size_t) P::master_vol] = std::min (v[(size_t) P::master_vol], -6.0f);
    v[(size_t) P::filt (0, P::f_drive)] = std::min (v[(size_t) P::filt (0, P::f_drive)], 12.0f);
    v[(size_t) P::filt (1, P::f_drive)] = std::min (v[(size_t) P::filt (1, P::f_drive)], 12.0f);
    v[(size_t) (P::DelayBase + P::delay_fb)] = std::min (v[(size_t) (P::DelayBase + P::delay_fb)], 0.7f);
    v[(size_t) (P::ChorusBase + P::chorus_fb)] = std::min (v[(size_t) (P::ChorusBase + P::chorus_fb)], 0.6f);
    v[(size_t) (P::ReverbBase + P::reverb_decay)] = std::min (v[(size_t) (P::ReverbBase + P::reverb_decay)], 8.0f);
    v[(size_t) (P::ReverbBase + P::reverb_freeze)] = 0.0f;
    v[(size_t) (P::CompBase + P::comp_makeup)] = std::min (v[(size_t) (P::CompBase + P::comp_makeup)], 6.0f);
    v[(size_t) (P::DistBase + P::dist_drive)] = std::min (v[(size_t) (P::DistBase + P::dist_drive)], 18.0f);
    v[(size_t) (P::EqBase + P::eq_hp)] = std::min (v[(size_t) (P::EqBase + P::eq_hp)], 300.0f);
    v[(size_t) (P::EqBase + P::eq_lp)] = std::max (v[(size_t) (P::EqBase + P::eq_lp)], 3000.0f);
    v[(size_t) (P::DistBase + P::dist_bits)] = std::max (v[(size_t) (P::DistBase + P::dist_bits)], 4.0f);
    v[(size_t) (P::DistBase + P::dist_downsample)] = std::max (v[(size_t) (P::DistBase + P::dist_downsample)], 4000.0f);
    // keep sustained notes audible through amp modulation: no deep negative amp routings
    for (int s = 0; s < P::NumModSlots; ++s)
    {
        const auto& r = p.extras.routings[(size_t) s];
        if (r.active() && (r.dest == P::amp_level || r.dest == P::osc (0, P::level)))
            v[(size_t) P::modDepth (s)] = juce::jlimit (-0.3f, 0.5f, v[(size_t) P::modDepth (s)]);
    }
}

PresetData randomizePreset (const PresetData& current, const RandomLocks& locks, uint64_t seed)
{
    dsp::Rng rng (seed);
    PresetData p = current;
    p.name = "Random";
    const auto& defs = paramDefs();
    for (int i = 0; i < P::NumParams; ++i)
    {
        const auto sec = sectionOf (i);
        if (locked (sec, locks) || excluded (i)) continue;
        const auto& d = defs[(size_t) i];
        if (i >= P::ModDepthBase) continue; // depths are set with routings below
        float n = rng.uniform();
        if (d.type == PType::Float) p.params[(size_t) i] = d.fromNorm (n);
        else p.params[(size_t) i] = d.snap (d.fromNorm (n));
    }
    if (! locks.osc)
    {
        const auto& names = dsp::factoryTableNames();
        for (int o = 0; o < 2; ++o)
        {
            p.tables[o] = {};
            p.tables[o].factory = names[(size_t) (rng.next() % names.size())];
        }
        p.params[(size_t) P::osc (0, P::unison)] = (float) (1 + (int) (rng.uniform() * 5.0f));
        p.params[(size_t) P::osc (1, P::unison)] = (float) (1 + (int) (rng.uniform() * 3.0f));
        p.params[(size_t) P::osc (0, P::level)] = 0.5f + 0.4f * rng.uniform();
        p.params[(size_t) P::osc (1, P::level)] = 0.6f * rng.uniform();
        p.params[(size_t) P::sub (P::sub_level)] = rng.uniform() < 0.3f ? 0.4f * rng.uniform() : 0.0f;
        p.params[(size_t) P::noise (P::noise_level)] = rng.uniform() < 0.25f ? 0.15f * rng.uniform() : 0.0f;
        for (int o = 0; o < 2; ++o)
            p.params[(size_t) P::osc (o, P::warp_mode)] = rng.uniform() < 0.6f ? 0.0f : (float) (1 + (int) (rng.uniform() * 9.0f));
    }
    if (! locks.mod)
    {
        for (auto& r : p.extras.routings) r = Routing();
        const int n = 1 + (int) (rng.uniform() * 5.0f);
        static const int dests[] = { P::filt (0, P::f_cutoff), P::osc (0, P::wt_pos), P::osc (1, P::wt_pos), P::osc (0, P::warp_amt),
                                     P::osc (0, P::pan), P::voice_pitch, P::filt (0, P::f_reso), P::osc (1, P::level) };
        for (int s = 0; s < n; ++s)
        {
            auto& r = p.extras.routings[(size_t) s];
            r.src = (int16_t) (rng.uniform() < 0.5f ? (int) Src::Env2 + (int) (rng.uniform() * 2.0f) : (int) Src::Lfo1 + (int) (rng.uniform() * 4.0f));
            r.dest = (int16_t) dests[rng.next() % (sizeof (dests) / sizeof (dests[0]))];
            float depth = rng.bipolar() * 0.5f;
            if (r.dest == P::voice_pitch) depth *= 0.02f;
            p.params[(size_t) P::modDepth (s)] = depth;
        }
        for (int s = n; s < P::NumModSlots; ++s) p.params[(size_t) P::modDepth (s)] = 0.0f;
        for (int l = 0; l < 4; ++l) p.params[(size_t) P::lfo (l, P::l_mode)] = 1.0f;
    }
    if (! locks.fx)
    {
        // at most four modules, shuffled order
        for (int f = 0; f < NumFx; ++f) p.params[(size_t) fxEnableParam (f)] = 0.0f;
        for (int k = 0; k < 3; ++k) p.params[(size_t) fxEnableParam ((int) (rng.next() % NumFx))] = 1.0f;
        for (int i = NumFx - 1; i > 0; --i) std::swap (p.extras.fxOrder[(size_t) i], p.extras.fxOrder[(size_t) (rng.next() % (uint64_t) (i + 1))]);
    }
    applyRandomConstraints (p);
    return p;
}

PresetData mutatePreset (const PresetData& current, float amount, const RandomLocks& locks, uint64_t seed)
{
    dsp::Rng rng (seed);
    PresetData p = current;
    const auto& defs = paramDefs();
    for (int i = 0; i < P::NumParams; ++i)
    {
        const auto& d = defs[(size_t) i];
        if (d.type != PType::Float || locked (sectionOf (i), locks) || excluded (i)) continue;
        if (i >= P::ModDepthBase && p.extras.routings[(size_t) (i - P::ModDepthBase)].src < 0) continue;
        const float n = d.toNorm (p.params[(size_t) i]) + rng.bipolar() * 0.15f * amount;
        p.params[(size_t) i] = d.fromNorm (juce::jlimit (0.0f, 1.0f, n));
    }
    applyRandomConstraints (p);
    return p;
}
} // namespace prism
