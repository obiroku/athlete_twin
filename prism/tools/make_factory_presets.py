#!/usr/bin/env python3
"""Generates the PRISM factory bank (SPEC section 9) as .prismpreset JSON files.

Values are written in real units. Unlisted parameters are Init defaults (the
loader fills them in). Loudness is balanced afterwards with
`PrismTool normalize Presets/Factory` (it only adjusts master_vol).

Run from the prism/ directory:  python3 tools/make_factory_presets.py
"""
import json, os

OUT = os.path.join(os.path.dirname(__file__), "..", "Presets", "Factory")

DIV = {}
for i, b in enumerate(["8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"]):
    DIV[b] = i * 3; DIV[b + "D"] = i * 3 + 1; DIV[b + "T"] = i * 3 + 2

FT = dict(LP12=0, LP24=1, HP12=2, HP24=3, BP12=4, BP24=5, Notch=6, Peak=7, CombP=8, CombM=9, Ladder=10, Diode=11, Formant=12)
LFO = dict(Sine=0, Tri=1, SawUp=2, SawDown=3, Square=4, SH=5, Smooth=6, Noise=7, Custom=8)
FX_ALL = ["distortion", "tape", "dimension", "chorus", "phaser", "flanger", "rotary", "delay", "reverb", "compressor", "multiband", "eq"]
FX_ON = dict(distortion="dist_on", tape="tape_on", dimension="dim_on", chorus="chorus_on", phaser="phaser_on", flanger="flanger_on",
             rotary="rotary_on", delay="delay_on", reverb="reverb_on", compressor="comp_on", multiband="mb_on", eq="eq_on")
ST = 1.0 / 48.0   # one semitone of voice_pitch / osc pitch in normalized depth
OCT = 0.1         # one octave of cutoff in normalized depth


class Preset:
    def __init__(self, name, category, macros, description="", tags=(), author="Asher", base=None):
        self.name, self.category, self.description, self.tags, self.author = name, category, description, list(tags), author
        self.macro_names = list(macros)
        self.params = {}
        self.routes = []       # dicts
        self.labels = {}       # label -> slot
        self.fx = []
        self.tables = {"oscA": {"factory": "Basic Shapes"}, "oscB": {"factory": "Basic Shapes"}}
        self.chord = None
        self.aftertouch = True
        if base is not None:
            self.params = dict(base.params)
            self.routes = [dict(r) for r in base.routes]
            self.labels = dict(base.labels)
            self.fx = list(base.fx)
            self.tables = json.loads(json.dumps(base.tables))
            self.chord = base.chord
            self.aftertouch = base.aftertouch

    def p(self, **kw):
        self.params.update(kw)
        return self

    def table(self, osc, name):
        self.tables["osc" + osc] = {"factory": name}
        return self

    def mod(self, src, dest, depth, label=None, polarity="native", aux="none", curve=0.0):
        if isinstance(dest, tuple):          # ("depth", label): modulate another routing's depth
            dest = "mod%02d_depth" % (self.labels[dest[1]] + 1)
        self.routes.append(dict(source=src, dest=dest, depth=depth, polarity=polarity, aux=aux, curve=curve, bypass=False))
        if label:
            self.labels[label] = len(self.routes) - 1 + (2 if self.aftertouch else 1)
        return self

    def macro(self, m, *targets):
        for dest, depth in targets:
            self.mod("macro%d" % m, dest, depth)
        return self

    def start(self, *vals):
        # macro values on load (so the preset sounds finished before you touch anything)
        for i, v in enumerate(vals):
            self.params["macro%d" % (i + 1)] = v
        return self

    def punchy(self):
        # same start phase every note: consistent attacks for basses, keys and plucks
        self.params.update(oscA_rand_phase=0.0, oscB_rand_phase=0.0)
        return self

    def wide(self, detune=None):
        self.params.update(oscA_spread=0.9, oscB_spread=0.9)
        if detune is not None:
            self.params.update(oscA_detune=detune)
        return self

    def fxchain(self, *mods, **params):
        self.fx = list(mods)
        self.params.update(params)
        return self

    def to_json(self):
        routes = [dict(source="env2", dest="f1_cutoff", depth=0.0, polarity="native", aux="none", curve=0.0, bypass=False)]
        if self.aftertouch:
            routes.append(dict(source="aftertouch", dest="f1_cutoff", depth=0.1, polarity="native", aux="none", curve=0.0, bypass=False))
        routes += self.routes
        for i, r in enumerate(routes):
            r["slot"] = i + 1
        params = dict(self.params)
        for r in routes:
            params["mod%02d_depth" % r["slot"]] = r["depth"]
        for m in FX_ALL:
            params[FX_ON[m]] = 1.0 if m in self.fx else 0.0
        order = list(self.fx) + [m for m in FX_ALL if m not in self.fx]
        d = dict(format_version=1, name=self.name, author=self.author, category=self.category, tags=self.tags,
                 description=self.description,
                 macros=[dict(name=n, value=params.get("macro%d" % (i + 1), 0.0)) for i, n in enumerate(self.macro_names)],
                 params=params, mod_routings=routes, fx_order=order, wavetables=self.tables)
        if self.chord:
            d["chord"] = {"notes": self.chord}
        return d


bank = []

# ---------------------------------------------------------------- Init
init = Preset("Init", "Init", ["Macro 1", "Macro 2", "Macro 3", "Macro 4"], "Sine, open filter, empty rack.")
init.p(env1_attack=0.001, env1_decay=0.0, env1_sustain=1.0, env1_release=0.05)
bank.append(init)

# ---------------------------------------------------------------- Neo-Soul Keys
nsk = Preset("Neo-Soul Keys", "Keys", ["Bark", "Tremolo", "Tape", "Space"], "FM-bell electric piano with stereo tremolo.", ["keys", "neo-soul", "ep"])
nsk.table("A", "FM Bell").table("B", "Basic Shapes")
nsk.p(oscA_wt_pos=0.25, oscB_wt_pos=0.0, oscB_level=0.6, f1_type=FT["LP12"], f1_cutoff=4000.0, f1_keytrack=0.5,
      env1_attack=0.001, env1_decay=1.8, env1_sustain=0.3, env1_release=0.5, drift=0.2,
      lfo1_rate=5.0, lfo1_mode=0, lfo1_shape=LFO["Sine"])
nsk.p(env2_attack=0.0, env2_decay=0.7, env2_sustain=0.15, f1_cutoff=2600.0)
nsk.punchy()
nsk.mod("velocity", "oscA_wt_pos", 0.3, label="bark")
nsk.mod("env2", "f1_cutoff", 0.18, aux="velocity")
nsk.mod("lfo1", "oscA_pan", 0.0, label="tremA").mod("lfo1", "oscB_pan", 0.0, label="tremB")
nsk.fxchain("tape", "chorus", "reverb", tape_wow_depth=0.12, tape_flutter=0.05, tape_sat=0.2, tape_age=0.15,
            chorus_mix=0.25, chorus_rate=0.4, reverb_mix=0.2, reverb_size=0.5, reverb_decay=2.2)
nsk.macro(1, (("depth", "bark"), 0.25), ("f1_drive", 0.35))
nsk.macro(2, (("depth", "tremA"), 0.35), (("depth", "tremB"), -0.35))
nsk.macro(3, ("tape_wow_depth", 0.6), ("tape_age", 0.5))
nsk.macro(4, ("reverb_mix", 0.45), ("reverb_size", 0.3))
nsk.start(0.3, 0.35, 0.3, 0.35)
bank.append(nsk)

# ---------------------------------------------------------------- Gospel Pad
gp = Preset("Gospel Pad", "Pad", ["Air", "Movement", "Width", "Warmth"], "Choir-ish pad with an organ undertone.", ["pad", "gospel", "warm"])
gp.table("A", "Choirish").table("B", "Organ-ish")
gp.p(oscA_wt_pos=0.3, oscA_unison=5, oscA_detune=0.15, oscB_oct=-1, oscB_level=0.2, f1_type=FT["LP24"], f1_cutoff=4200.0,
     env1_attack=0.8, env1_release=2.5, env2_attack=1.5, env2_decay=1.0, env2_sustain=1.0,
     lfo1_rate=0.2, lfo1_mode=0)
gp.mod("env2", "f1_cutoff", 0.15)
gp.wide()
gp.p(oscB_unison=3, oscB_detune=0.1, sub_level=0.15, sub_dest=1)
gp.mod("lfo1", "oscA_wt_pos", 0.1, label="move")
gp.fxchain("chorus", "reverb", chorus_mix=0.35, reverb_mix=0.6, reverb_size=0.75, reverb_decay=4.5)
gp.macro(1, ("reverb_mix", 0.3), ("f1_cutoff", 0.12))
gp.macro(2, (("depth", "move"), 0.3))
gp.macro(3, ("oscA_spread", 0.4), ("master_width", 0.25))
gp.macro(4, ("f1_drive", 0.35))
gp.start(0.25, 0.4, 0.3, 0.2)
bank.append(gp)

# ---------------------------------------------------------------- Folk-Gaze Wash
fg = Preset("Folk-Gaze Wash", "Pad", ["Haze", "Shimmer", "Wobble", "Wash"], "Detuned saw wash with shimmer and ping-pong echoes.", ["pad", "shoegaze", "folk"])
fg.table("A", "Saw Sweep").table("B", "Glass")
fg.p(oscA_wt_pos=0.6, oscA_unison=7, oscA_detune=0.25, oscB_oct=1, oscB_level=0.3, noise_type=1, noise_level=0.1, noise_tone=0.15,
     f1_type=FT["LP24"], f1_cutoff=3600.0, env1_attack=1.2, env1_release=4.0, lfo1_rate=0.1, lfo1_mode=0)
fg.mod("lfo1", "f1_cutoff", 0.08)
fg.wide(0.22)
fg.start(0.3, 0.3, 0.25, 0.3)
fg.fxchain("tape", "chorus", "delay", "reverb", tape_wow_depth=0.25, chorus_mix=0.35, delay_sync=1.0, delay_div=DIV["1/4D"],
           delay_pingpong=1.0, delay_mix=0.4, delay_fb=0.45, reverb_size=0.9, reverb_shimmer=0.4, reverb_mix=0.55, reverb_decay=6.0)
fg.macro(1, ("f1_cutoff", 0.15), ("noise_level", 0.1))
fg.macro(2, ("reverb_shimmer", 0.5))
fg.macro(3, ("tape_wow_depth", 0.6), ("tape_flutter", 0.3))
fg.macro(4, ("reverb_mix", 0.4), ("delay_mix", 0.3))
bank.append(fg)

# ---------------------------------------------------------------- Upright Sub
us = Preset("Upright Sub", "Bass", ["Thump", "Tone", "Length", "Grit"], "Round mono bass with a finger thump.", ["bass", "upright", "mono"])
us.table("A", "Sub Stack")
us.p(voice_mode=2, glide_time=0.03, oscA_wt_pos=0.1, sub_shape=0, sub_oct=0, sub_level=0.5, sub_dest=3,
     noise_type=2, noise_level=0.0, f1_type=FT["Ladder"], f1_cutoff=600.0, f1_reso=0.15, f1_keytrack=0.6,
     env1_attack=0.002, env1_decay=0.6, env1_sustain=0.5, env1_release=0.12,
     env2_decay=0.25, env2_sustain=0.0, env3_attack=0.0, env3_decay=0.04, env3_sustain=0.0, bass_mono=150.0)
us.mod("env3", "noise_level", 0.45, label="thump")
us.mod("env2", "f1_cutoff", 0.2, aux="velocity")
us.punchy()
us.start(0.4, 0.3, 0.2, 0.15)
us.fxchain("compressor", "eq", comp_thresh=-20.0, comp_ratio=3.0, comp_attack=8.0, comp_release=120.0, eq_ls_gain=2.0, eq_ls_freq=90.0)
us.macro(1, (("depth", "thump"), 0.4))
us.macro(2, ("f1_cutoff", 0.2))
us.macro(3, ("env1_decay", 0.3), ("env1_release", 0.2))
us.macro(4, ("f1_drive", 0.5), ("f1_reso", 0.3))
bank.append(us)

# ---------------------------------------------------------------- Tape Lead
tl = Preset("Tape Lead", "Lead", ["Wobble", "Grit", "Echo", "Vibrato"], "Saw/square lead through tape and a dotted echo.", ["lead", "tape", "mono"])
tl.table("A", "Saw Sweep").table("B", "Square Stack")
tl.p(voice_mode=2, glide_time=0.08, oscA_wt_pos=0.9, oscB_wt_pos=0.3, oscB_fine=7, oscB_level=0.5,
     f1_type=FT["Ladder"], f1_cutoff=4800.0, f1_reso=0.25, f1_drive=6.0,
     lfo2_rate=5.5, lfo2_delay=0.4, lfo2_fade=0.6)
tl.mod("lfo2", "voice_pitch", 0.15 * ST, label="vib", aux="mod_wheel")
tl.p(oscA_unison=2, oscA_detune=0.08, oscA_spread=0.5)
tl.start(0.3, 0.25, 0.4, 0.0)
tl.fxchain("distortion", "tape", "delay", "reverb", dist_mode=3, dist_mix=0.2, dist_drive=8.0, tape_wow_depth=0.4, tape_flutter=0.3,
           delay_sync=1.0, delay_div=DIV["1/4D"], delay_mix=0.25, delay_fb=0.35, reverb_mix=0.2)
tl.macro(1, ("tape_wow_depth", 0.5))
tl.macro(2, ("dist_mix", 0.5), ("f1_drive", 0.4))
tl.macro(3, ("delay_mix", 0.35), ("delay_fb", 0.25))
tl.macro(4, (("depth", "vib"), 0.004))
bank.append(tl)

# ---------------------------------------------------------------- Choir Stack
cs = Preset("Choir Stack", "Choir/Vox", ["Vowel", "Breath", "Size", "Shimmer"], "Two vocal tables through a formant filter.", ["choir", "vox", "pad"])
cs.table("A", "Vocal Ah").table("B", "Vocal Ee")
cs.p(oscA_unison=6, oscB_unison=6, oscB_level=0.6, oscA_detune=0.18, oscB_detune=0.18, f1_type=FT["Formant"], f1_reso=0.3,
     env1_attack=0.4, env1_release=1.5, lfo1_rate=0.08, lfo1_mode=0, noise_type=1, noise_level=0.0)
cs.mod("lfo1", "oscA_wt_pos", 0.3).mod("lfo1", "oscB_wt_pos", 0.3)
cs.wide()
cs.start(0.0, 0.3, 0.3, 0.2)
cs.fxchain("dimension", "reverb", dim_mix=0.5, reverb_size=0.85, reverb_mix=0.5, reverb_decay=5.0)
cs.macro(1, ("f1_vowel", 1.0))
cs.macro(2, ("noise_level", 0.12))
cs.macro(3, ("reverb_size", 0.15), ("reverb_decay", 0.3))
cs.macro(4, ("reverb_shimmer", 0.6))
bank.append(cs)

# ---------------------------------------------------------------- Donut Shop Bass
dsb = Preset("Donut Shop Bass", "Bass", ["Thump", "Warmth", "Wobble", "Bite"], "Plucky lo-fi bass.", ["bass", "lo-fi", "hip-hop"])
dsb.table("A", "Basic Shapes")
dsb.p(voice_mode=1, glide_time=0.04, oscA_wt_pos=0.4, sub_shape=0, sub_oct=0, sub_level=0.5, f1_type=FT["LP24"], f1_cutoff=400.0,
      env2_decay=0.18, env2_sustain=0.0, drift=0.3, bass_mono=120.0)
dsb.mod("env2", "f1_cutoff", 0.3, label="pluck").mod("velocity", "f1_cutoff", 0.15)
dsb.punchy()
dsb.start(0.3, 0.35, 0.2, 0.3)
dsb.fxchain("distortion", "compressor", "eq", dist_mode=0, dist_mix=0.15, dist_drive=9.0, comp_thresh=-18.0, comp_ratio=4.0,
            eq_ls_gain=3.0, eq_ls_freq=100.0)
dsb.macro(1, ("env1_decay", 0.0), ("sub_level", 0.3))
dsb.macro(2, ("dist_mix", 0.4), ("f1_cutoff", 0.08))
dsb.macro(3, ("drift", 0.6))
dsb.macro(4, (("depth", "pluck"), 0.3), ("f1_reso", 0.3))
bank.append(dsb)

# ---------------------------------------------------------------- Shoegaze Shimmer
ss = Preset("Shoegaze Shimmer", "Pad", ["Bloom", "Shimmer", "Drift", "Freeze"], "Glass bloom into a shimmering wall.", ["pad", "shoegaze", "ambient"])
ss.table("A", "Glass").table("B", "Saw Sweep")
ss.p(oscA_unison=5, oscA_detune=0.2, oscB_wt_pos=0.4, oscB_level=0.4, f1_type=FT["HP12"], f1_cutoff=300.0,
     env1_attack=2.0, env1_release=6.0)
ss.fxchain("chorus", "delay", "reverb", chorus_mix=0.4, delay_sync=1.0, delay_div=DIV["1/4"], delay_pingpong=1.0, delay_mix=0.35,
           delay_fb=0.45, reverb_shimmer=0.7, reverb_mix=0.7, reverb_size=0.9, reverb_decay=8.0)
ss.wide(0.2)
ss.macro(1, ("env1_attack", -0.25), ("oscB_level", 0.3))
ss.macro(2, ("reverb_shimmer", 0.3))
ss.macro(3, ("drift", 0.8), ("chorus_depth", 0.4))
ss.macro(4, ("reverb_freeze", 1.0))
ss.start(0.3, 0.5, 0.3, 0.0)
bank.append(ss)

# ---------------------------------------------------------------- Pew Organ
po = Preset("Pew Organ", "Organ", ["Drawbars", "Rotor", "Drive", "Room"], "Drawbar organ with a rotary speaker.", ["organ", "gospel", "rotary"])
po.table("A", "Organ-ish").table("B", "Organ-ish")
po.p(oscA_wt_pos=0.3, oscB_oct=1, oscB_level=0.4, oscA_rand_phase=0.0, oscB_rand_phase=0.0, f1_enable=0.0,
     env1_attack=0.005, env1_sustain=1.0, env1_release=0.08, env1_vel_amt=0.0,
     noise_type=0, noise_tone=0.8, env3_decay=0.015, env3_sustain=0.0, drift=0.05)
po.mod("env3", "noise_level", 0.25)
po.fxchain("distortion", "rotary", "reverb", dist_mode=3, dist_mix=0.1, dist_drive=6.0, rotary_mw=1.0, reverb_mix=0.25, reverb_size=0.7)
po.macro(1, ("oscA_wt_pos", 0.7))
po.macro(2, ("rotary_fast", 1.0))
po.macro(3, ("dist_mix", 0.4), ("rotary_drive", 0.5))
po.macro(4, ("reverb_mix", 0.35))
po.start(0.4, 0.0, 0.25, 0.3)
bank.append(po)

# ---------------------------------------------------------------- Nebraska Winter
nw = Preset("Nebraska Winter", "Pad", ["Cold", "Tape Age", "Wind", "Distance"], "Hollow, aged tape pad.", ["pad", "folk", "lo-fi"])
nw.table("A", "Hollow").table("B", "Triangle Stack")
nw.p(oscA_wt_pos=0.3, oscB_wt_pos=0.1, oscB_oct=-1, oscB_level=0.5, env1_attack=1.5, env1_release=5.0, lfo1_rate=0.05, lfo1_mode=0,
     noise_type=1, noise_tone=0.7, noise_level=0.0, f1_cutoff=5000.0, f1_type=FT["LP12"])
nw.mod("lfo1", "voice_pitch", 0.04 * ST)
nw.p(oscA_unison=3, oscA_detune=0.12)
nw.wide()
nw.start(0.2, 0.4, 0.3, 0.35)
nw.fxchain("tape", "reverb", "eq", tape_age=0.6, tape_hiss=0.3, reverb_damp=0.7, reverb_decay=7.0, reverb_mix=0.45, reverb_size=0.8)
nw.macro(1, ("eq_hp", 0.45), ("f1_cutoff", -0.12))
nw.macro(2, ("tape_age", 0.4), ("tape_wow_depth", 0.4))
nw.macro(3, ("noise_level", 0.15))
nw.macro(4, ("reverb_mix", 0.35), ("reverb_predelay", 0.4))
bank.append(nw)

# ---------------------------------------------------------------- Voicemail Keys (built on Neo-Soul Keys)
vk = Preset("Voicemail Keys", "Keys", ["Signal", "Crush", "Dropout", "Room"], "Neo-soul keys through a bad phone line.",
            ["keys", "lo-fi"], base=nsk)
vk.macro_names = ["Signal", "Crush", "Dropout", "Room"]
vk.routes = [r for r in vk.routes if r["source"] != "macro1" and r["source"] != "macro2" and r["source"] != "macro3" and r["source"] != "macro4"]
vk.p(f1_type=FT["BP12"], f1_cutoff=1200.0, f1_reso=0.3, lfo3_rate=3.0, lfo3_shape=LFO["SH"], lfo3_polarity=0, lfo3_smooth=0.1)
vk.mod("lfo3", "amp_level", -0.2, label="drop")
vk.fxchain("distortion", "tape", "chorus", "reverb", dist_mode=4, dist_mix=1.0, dist_drive=0.0, dist_bits=10.0, dist_downsample=8000.0,
           tape_age=0.8, tape_wow_depth=0.15, chorus_mix=0.2, reverb_mix=0.15)
vk.macro(1, ("f1_reso", -0.25), ("f1_cutoff", 0.1))
vk.macro(2, ("dist_bits", -0.4), ("dist_downsample", -0.25))
vk.macro(3, (("depth", "drop"), -0.15))
vk.macro(4, ("reverb_mix", 0.4))
vk.start(0.3, 0.3, 0.4, 0.3)
bank.append(vk)

# ---------------------------------------------------------------- Trail Pluck
tp = Preset("Trail Pluck", "Pluck", ["Length", "Brightness", "Echo", "Space"], "Additive pluck with glassy overtones.", ["pluck", "indie"])
tp.table("A", "Additive 16").table("B", "Glass")
tp.p(oscA_wt_pos=0.6, oscB_oct=1, oscB_level=0.3, f1_type=FT["LP12"], f1_cutoff=900.0,
     env1_attack=0.001, env1_decay=0.4, env1_sustain=0.0, env1_release=0.3, env2_decay=0.25, env2_sustain=0.0)
tp.mod("env2", "oscA_wt_pos", -0.5)
tp.punchy()
tp.p(oscB_rand_phase=0.0)
tp.mod("env2", "f1_cutoff", 0.35, label="bright")
tp.fxchain("delay", "reverb", delay_sync=1.0, delay_div=DIV["1/8D"], delay_pingpong=1.0, delay_mix=0.3, delay_fb=0.4,
           reverb_mix=0.3, reverb_size=0.6)
tp.macro(1, ("env1_decay", 0.3), ("env1_release", 0.25), ("env2_decay", 0.2))
tp.macro(2, (("depth", "bright"), 0.25), ("oscB_level", 0.3))
tp.macro(3, ("delay_mix", 0.35), ("delay_fb", 0.3))
tp.macro(4, ("reverb_mix", 0.4))
tp.start(0.3, 0.4, 0.35, 0.35)
bank.append(tp)

# ---------------------------------------------------------------- Haze Pad
hp = Preset("Haze Pad", "Pad", ["Motion", "Haze", "Width", "Space"], "Slow PWM pad.", ["pad", "lo-fi", "warm"])
hp.table("A", "PWM Sweep").table("B", "Hollow")
hp.p(oscA_unison=4, oscB_unison=4, oscB_level=0.5, f1_type=FT["LP24"], f1_cutoff=2600.0, lfo1_rate=0.3, lfo1_mode=0,
     env1_attack=0.6, env1_release=2.5, env2_attack=2.0, env2_sustain=1.0)
hp.mod("lfo1", "oscA_wt_pos", 0.35, label="motion", polarity="uni")
hp.mod("env2", "f1_cutoff", 0.15)
hp.wide(0.18)
hp.start(0.4, 0.3, 0.4, 0.35)
hp.fxchain("tape", "chorus", "reverb", tape_wow_depth=0.2, tape_age=0.3, chorus_mix=0.4, reverb_mix=0.4, reverb_decay=4.0)
hp.macro(1, (("depth", "motion"), 0.3), ("lfo1_rate", 0.2))
hp.macro(2, ("tape_age", 0.5), ("f1_cutoff", -0.1))
hp.macro(3, ("oscA_spread", 0.4), ("chorus_depth", 0.4))
hp.macro(4, ("reverb_mix", 0.4))
bank.append(hp)

# ---------------------------------------------------------------- Basement Sermon Bass
bsb = Preset("Basement Sermon Bass", "Bass", ["Growl", "Drive", "Punch", "Tone"], "Driven square bass with an FM growl.", ["bass", "gritty", "mono"])
bsb.table("A", "Square Stack").table("B", "FM Growl")
bsb.p(voice_mode=1, oscB_level=0.25, oscB_wt_pos=0.3, f1_type=FT["Ladder"], f1_cutoff=300.0, f1_reso=0.4, f1_drive=12.0,
      env2_decay=0.2, env2_sustain=0.0, bass_mono=120.0, oscA_warp_mode=11, oscA_warp_amt=0.0)
bsb.mod("env2", "f1_cutoff", 0.3, label="punch", aux="velocity")
bsb.punchy()
bsb.start(0.3, 0.3, 0.4, 0.3)
bsb.fxchain("distortion", "compressor", dist_mode=2, dist_mix=0.2, dist_drive=6.0, comp_thresh=-18.0, comp_ratio=4.0)
bsb.macro(1, ("oscB_level", 0.4), ("oscA_warp_amt", 0.35))
bsb.macro(2, ("f1_drive", 0.3), ("dist_mix", 0.4))
bsb.macro(3, (("depth", "punch"), 0.3))
bsb.macro(4, ("f1_cutoff", 0.2))
bank.append(bsb)

# ---------------------------------------------------------------- Testimony Chop Keys (chord mode)
tck = Preset("Testimony Chop Keys", "Keys", ["Strum", "Length", "Crunch", "Space"], "One key plays a strummed maj9.", ["keys", "chords", "gospel"])
tck.table("A", "Organ-ish").table("B", "Additive 16")
tck.p(oscA_wt_pos=0.75, oscB_level=0.5, oscB_wt_pos=0.4, env1_decay=0.25, env1_sustain=0.2, env1_release=0.25,
      chord_on=1.0, chord_strum=15.0, f1_cutoff=9000.0, f1_type=FT["LP12"])
tck.chord = [0, 4, 7, 11, 14]
tck.punchy()
tck.start(0.25, 0.3, 0.25, 0.35)
tck.fxchain("distortion", "delay", "reverb", dist_mode=0, dist_mix=0.1, dist_drive=6.0, delay_sync=1.0, delay_div=DIV["1/16"],
            delay_mix=0.2, delay_fb=0.3, reverb_size=0.4, reverb_damp=0.2, reverb_decay=1.5, reverb_mix=0.25)
tck.macro(1, ("chord_strum", 0.4))
tck.macro(2, ("env1_decay", 0.3), ("env1_sustain", 0.3))
tck.macro(3, ("dist_mix", 0.4), ("dist_drive", 0.3))
tck.macro(4, ("reverb_mix", 0.35))
bank.append(tck)

# ---------------------------------------------------------------- Sunday Morning
sm = Preset("Sunday Morning", "Organ", ["Light", "Rotor", "Choir", "Room"], "Choir and organ on a slow rotor.", ["organ", "gospel", "choir"])
sm.table("A", "Choirish").table("B", "Organ-ish")
sm.p(oscB_level=0.5, oscB_wt_pos=0.4, env1_attack=0.3, env1_release=1.5, f1_type=FT["LP12"], f1_cutoff=5000.0)
sm.fxchain("rotary", "reverb", reverb_mix=0.4, reverb_size=0.75, reverb_decay=3.5)
sm.p(oscA_unison=4, oscA_detune=0.12, oscA_spread=0.8)
sm.start(0.4, 0.0, 0.4, 0.3)
sm.macro(1, ("f1_cutoff", 0.15), ("oscB_level", 0.25))
sm.macro(2, ("rotary_fast", 1.0))
sm.macro(3, ("oscA_level", 0.25))
sm.macro(4, ("reverb_mix", 0.35))
bank.append(sm)

# ---------------------------------------------------------------- Glide Sub
gs = Preset("Glide Sub", "Bass", ["Drop", "Glide", "Dirt", "Length"], "Sine sub with a pitch drop.", ["bass", "808", "mono"])
gs.table("A", "Basic Shapes")
gs.p(voice_mode=1, glide_time=0.12, glide_legato=1.0, oscA_wt_pos=0.0, sub_shape=0, sub_oct=0, sub_level=0.5,
     env2_attack=0.3, env2_sustain=1.0, env1_decay=1.2, env1_sustain=0.7, env1_release=0.3, bass_mono=150.0, f1_enable=0.0)
gs.mod("env2", "voice_pitch", -12 * ST, label="drop")
gs.punchy()
gs.start(0.0, 0.2, 0.25, 0.3)
gs.fxchain("distortion", dist_mode=0, dist_mix=0.1, dist_drive=6.0)
gs.macro(1, (("depth", "drop"), -0.1))
gs.macro(2, ("glide_time", 0.3))
gs.macro(3, ("dist_mix", 0.5), ("dist_drive", 0.25))
gs.macro(4, ("env1_release", 0.3), ("env1_decay", 0.2))
bank.append(gs)

# ---------------------------------------------------------------- Ladder Funk Bass
lfb = Preset("Ladder Funk Bass", "Bass", ["Squelch", "Decay", "Drive", "Sub"], "Velocity-driven ladder squelch.", ["bass", "funk", "mono"])
lfb.table("A", "Saw Sweep")
lfb.p(voice_mode=2, oscA_wt_pos=1.0, f1_type=FT["Ladder"], f1_cutoff=250.0, f1_reso=0.6, env2_decay=0.15, env2_sustain=0.0, bass_mono=120.0)
lfb.mod("env2", "f1_cutoff", 0.5, aux="velocity")
lfb.punchy()
lfb.p(sub_level=0.3, sub_dest=3)
lfb.start(0.2, 0.3, 0.25, 0.3)
lfb.macro(1, ("f1_reso", 0.3))
lfb.macro(2, ("env2_decay", 0.25))
lfb.macro(3, ("f1_drive", 0.6))
lfb.macro(4, ("sub_level", 0.5))
bank.append(lfb)

# ---------------------------------------------------------------- Reese Haze
rh = Preset("Reese Haze", "Bass", ["Width", "Motion", "Grit", "Tone"], "Detuned saw reese with slow filter motion.", ["bass", "reese"])
rh.table("A", "Saw Sweep").table("B", "Saw Sweep")
rh.p(oscA_wt_pos=1.0, oscA_unison=2, oscA_detune=0.08, oscB_wt_pos=1.0, oscB_oct=-1, oscB_level=0.6,
     f1_type=FT["LP24"], f1_cutoff=1200.0, lfo1_rate=0.15, lfo1_mode=0, bass_mono=100.0)
rh.mod("lfo1", "f1_cutoff", 0.06, label="motion")
rh.start(0.3, 0.4, 0.3, 0.3)
rh.fxchain("chorus", "distortion", chorus_mix=0.35, dist_mode=0, dist_mix=0.15, dist_drive=8.0)
rh.macro(1, ("oscA_detune", 0.25), ("chorus_mix", 0.3))
rh.macro(2, (("depth", "motion"), 0.1))
rh.macro(3, ("dist_mix", 0.45))
rh.macro(4, ("f1_cutoff", 0.15))
bank.append(rh)

# ---------------------------------------------------------------- Breathing Choir
bc = Preset("Breathing Choir", "Choir/Vox", ["Breath", "Vowel Speed", "Size", "Shimmer"], "Choir whose vowels breathe.", ["choir", "vox", "ambient"])
bc.table("A", "Choirish")
bc.p(oscA_unison=6, oscA_detune=0.2, noise_type=1, noise_level=0.08, f1_type=FT["Formant"], f1_reso=0.3,
     lfo1_rate=0.15, lfo1_mode=0, lfo1_polarity=0, env1_attack=0.5, env1_release=2.0)
bc.mod("lfo1", "f1_vowel", 0.6)
bc.wide()
bc.start(0.3, 0.3, 0.35, 0.2)
bc.fxchain("reverb", reverb_mix=0.5, reverb_size=0.8, reverb_decay=5.0)
bc.macro(1, ("noise_level", 0.15))
bc.macro(2, ("lfo1_rate", 0.25))
bc.macro(3, ("reverb_size", 0.2), ("reverb_decay", 0.3))
bc.macro(4, ("reverb_shimmer", 0.6))
bank.append(bc)

# ---------------------------------------------------------------- Brass Swell
bs = Preset("Brass Swell", "Lead", ["Bloom", "Bite", "Width", "Room"], "Swelling saw brass.", ["lead", "brass"])
bs.table("A", "Saw Sweep")
bs.p(oscA_wt_pos=0.8, oscA_unison=3, oscA_detune=0.15, f1_type=FT["LP24"], f1_cutoff=500.0, env2_attack=0.3, env2_decay=0.8, env2_sustain=0.6,
     env1_attack=0.08, env1_release=0.35)
bs.mod("env2", "f1_cutoff", 0.4, label="bloom", aux="velocity")
bs.p(oscA_spread=0.7)
bs.start(0.3, 0.3, 0.3, 0.3)
bs.fxchain("chorus", "reverb", chorus_mix=0.25, reverb_mix=0.25)
bs.macro(1, (("depth", "bloom"), 0.2), ("env2_attack", 0.2))
bs.macro(2, ("f1_reso", 0.3), ("f1_drive", 0.35))
bs.macro(3, ("oscA_spread", 0.4), ("oscA_detune", 0.15))
bs.macro(4, ("reverb_mix", 0.35))
bank.append(bs)

# ---------------------------------------------------------------- Harp Arp (built on Trail Pluck)
ha = Preset("Harp Arp", "Arp", ["Rate", "Length", "Echo", "Space"], "Latched two-octave pluck arp.", ["arp", "pluck"], base=tp)
ha.macro_names = ["Rate", "Length", "Echo", "Space"]
ha.routes = [r for r in ha.routes if not r["source"].startswith("macro")]
ha.p(arp_on=1.0, arp_mode=0, arp_octaves=2, arp_div=DIV["1/16"], arp_latch=1.0, arp_gate=0.6)
ha.macro(1, ("arp_speed", 0.5))
ha.macro(2, ("env1_decay", 0.3), ("arp_gate", 0.3))
ha.macro(3, ("delay_mix", 0.35))
ha.macro(4, ("reverb_mix", 0.4))
ha.start(0.5, 0.3, 0.35, 0.35)
bank.append(ha)

# ---------------------------------------------------------------- Dream Arp
da = Preset("Dream Arp", "Arp", ["Rate", "Shimmer", "Echo", "Gate"], "Up-down glass arp in triplets.", ["arp", "ambient"])
da.table("A", "Glass")
da.p(oscA_wt_pos=0.6, arp_on=1.0, arp_mode=2, arp_octaves=3, arp_div=DIV["1/8T"], arp_gate=0.7,
     env1_attack=0.003, env1_decay=0.5, env1_sustain=0.3, env1_release=0.4)
da.fxchain("delay", "reverb", delay_sync=1.0, delay_div=DIV["1/4D"], delay_pingpong=1.0, delay_mix=0.4, delay_fb=0.4,
           reverb_shimmer=0.5, reverb_mix=0.45, reverb_decay=5.0)
da.macro(1, ("arp_speed", 0.5))
da.macro(2, ("reverb_shimmer", 0.4))
da.macro(3, ("delay_mix", 0.3), ("delay_fb", 0.25))
da.macro(4, ("arp_gate", -0.3))
da.p(oscA_unison=3, oscA_detune=0.1, oscA_spread=0.8)
da.start(0.5, 0.4, 0.3, 0.0)
bank.append(da)

# ---------------------------------------------------------------- Kalimba Drift
kd = Preset("Kalimba Drift", "Pluck", ["Tine", "Length", "Tape", "Space"], "FM tine pluck that drifts.", ["pluck", "kalimba", "lo-fi"])
kd.table("A", "FM Bell")
kd.p(oscA_wt_pos=0.5, env1_attack=0.001, env1_decay=0.35, env1_sustain=0.0, env1_release=0.35, drift=0.4, f1_type=FT["LP12"], f1_cutoff=7000.0)
kd.mod("velocity", "oscA_wt_pos", 0.15, label="tine")
kd.punchy()
kd.start(0.2, 0.3, 0.35, 0.35)
kd.fxchain("tape", "reverb", tape_wow_depth=0.3, tape_age=0.3, reverb_mix=0.25)
kd.macro(1, ("oscA_wt_pos", 0.3))
kd.macro(2, ("env1_decay", 0.3), ("env1_release", 0.2))
kd.macro(3, ("tape_wow_depth", 0.4), ("tape_age", 0.4))
kd.macro(4, ("reverb_mix", 0.4))
bank.append(kd)

# ---------------------------------------------------------------- Frozen Drone
fd = Preset("Frozen Drone", "Texture", ["Motion", "Air", "Dark", "Freeze"], "Wobbling drone that can freeze.", ["texture", "drone", "ambient"])
fd.table("A", "Wobble")
fd.p(lfo1_rate=0.03, lfo1_mode=0, lfo1_polarity=0, noise_type=1, noise_level=0.2, noise_tone=0.3, env1_attack=1.5, env1_release=6.0,
     f1_type=FT["LP24"], f1_cutoff=6000.0)
fd.mod("lfo1", "oscA_wt_pos", 0.8, label="motion")
fd.fxchain("chorus", "reverb", chorus_mix=0.3, reverb_mix=0.5, reverb_size=0.85, reverb_decay=8.0)
fd.macro(1, (("depth", "motion"), 0.2), ("lfo1_rate", 0.2))
fd.macro(2, ("noise_level", 0.15), ("noise_tone", 0.3))
fd.macro(3, ("f1_cutoff", -0.3))
fd.macro(4, ("reverb_freeze", 1.0))
fd.wide()
fd.p(oscA_unison=4, oscA_detune=0.15)
fd.start(0.4, 0.3, 0.0, 0.0)
bank.append(fd)

os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT):
    if f.endswith(".prismpreset"):
        os.remove(os.path.join(OUT, f))
for i, pr in enumerate(bank):
    fn = "%02d %s.prismpreset" % (i, pr.name)
    with open(os.path.join(OUT, fn), "w") as fh:
        json.dump(pr.to_json(), fh, indent=2)
print("wrote %d presets to %s" % (len(bank), os.path.abspath(OUT)))
