# PRISM — Progress

## Where things are built and tested

The project lives in `prism/` of this repository. Development happened in a
Linux cloud container (no macOS), so:

- Everything that is platform-neutral is built and tested here: the DSP, the
  engine, presets/state, the Catch2 suite, the benchmark, and **pluginval at
  strictness 10** (run against a Linux-only VST3 build made purely for
  validation, `-DPRISM_LINUX_VST3_FOR_VALIDATION=ON`; the shipping build is AU +
  Standalone only).
- Checks that need the Mac (`lipo -archs`, `auval -v aumu Prsm Ashr`, pluginval
  on the AU, Logic itself, and the benchmark on the development Mac's CPU) are
  listed under "To run on the Mac" for each phase.

Benchmark machine used here: `Intel(R) Xeon(R) Processor @ 2.10GHz` (cloud VM,
one core). Record the Mac's CPU model and clock when you run it there:
`./build/PrismBench init 10` prints both.

## Phase 1 — Scaffold (tag `phase-1`)

Built: CMake + JUCE 8.0.15 submodule (unmodified), Catch2 v3.7.1 via
FetchContent, AU + Standalone targets (x86_64, macOS 11.0, codes Ashr/Prsm),
APVTS with the full parameter registry (`Source/Params`) and parameter groups,
`juce::ParameterID` version hint 1 everywhere, JSON preset/state format with
`format_version` and a migration chain, `tools/get_pluginval.sh`, the offline
render helper (`Tests/TestHelpers`), and the headless benchmark
(`Tests/bench`). Instead of a throw-away sine voice, the real engine landed
directly (Init patch = sine).

Tests (all pass): parameter registry consistency; every parameter exposed with
version hint 1; state round-trip (save → load → identical parameters,
routings, MSEG, FX order, macro names); format_version + migration; block-size
test 1 / 64 / 517 / 4096 / variable pattern — max difference ≤ 1e-6 (measured
0.0); sine plays at 440.0 Hz for MIDI 69.

pluginval strictness 10 (Linux VST3, in-process, Xvfb): **SUCCESS**, all 25
test groups including editor, state restoration, parameter thread safety and
fuzzing.

To run on the Mac:
```
lipo -archs build/PRISM_artefacts/Release/AU/PRISM.component/Contents/MacOS/PRISM   # expect x86_64
auval -v aumu Prsm Ashr
tools/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate-in-process build/PRISM_artefacts/Release/AU/PRISM.component
```

## Phase 2 — Core voice (tag `phase-2`)

Built: wavetable engine (hygiene: DC removal, 512-harmonic truncation, one
whole-table rotation to an upward zero crossing, one −1 dBFS gain), 10-level
per-frame mip-maps at 2048 samples, Nyquist-safe crossfade (the darker level
reaches 100 % exactly where the brighter would alias; the crossfade spans the
top half-octave), Hermite on levels 0–1 / linear above, frame interpolation,
background generation with atomic hand-off and garbage FIFO, sine fallback,
offline wait. All 24 factory tables (not only the six of this phase). Osc A/B
with SIMD unison (`juce::dsp::SIMDRegister`, 4 lanes; scalar fast path for a
single lane), 1/√N gain, drift (pitch offset, 0.3 Hz wander, cutoff offset),
sub (polyBLEP square/saw), noise (white / Kellet pink / brown, tone tilt),
F1/F2 with all types, C3-pivot key tracking, DAHDSR envelopes, voice manager
(poly/mono/legato, priority, glide time/rate, legato-only, stealing with a 3 ms
tail fade and a 4-voice tail pool, sustain pedal), pitch bend, velocity curve,
seeded generators reset on prepare and transport start.

Results:

| Test | Target | Measured |
|---|---|---|
| Saw aliasing MIDI 96 | < −80 dB | −98.2 dB |
| Saw aliasing MIDI 108 | < −80 dB | −93.4 dB |
| Aliasing sweep MIDI 24–108 | < −70 dB | worst −78.0 dB (MIDI 24) |
| Glide 36→120, 10 ms level steps | ≤ 0.5 dB | 0.124 dB |
| Click: note-on/off, retrigger, steal (rand phase 0 % / 100 %) | ≤ 1.5× | 1.00 / 1.00 / 1.05 |
| Ladder self-oscillation MIDI 48–96 | ±5 ct | worst 0.43 ct |
| Determinism (drift 100 %, random phase 100 %) | bit-identical | bit-identical |
| Block-size test, unison + drift patch | ≤ 1e-6 | pass |

Benchmark (this VM, 48 kHz, 128-sample blocks, one core):

| Patch | Budget | Measured |
|---|---|---|
| Idle, FX on, no notes | < 1 % | 1.1 % |
| Init, 16 voices held | < 4 % | 4.1 % |
| Heavy (8 voices × 2 osc × 8 unison, both filters + drive, reverb, delay, chorus, tape) | < 25 % | 15.3 % |

The VM core is a 2.1 GHz server part; the Mac's desktop Intel core should be
noticeably faster. Re-measure on the Mac and record it here.

Notes: a voice that replaces a stolen voice starts after the 3 ms steal fade
(otherwise the overlapping sines can add constructively and exceed the 1.5×
click threshold).

## Phase 3 — Full engine (tag `phase-3`)

Built: Osc B (+ `oscB_pitch`), F2 and all filter types (Notch, Peak, Comb±,
Diode ladder solved as a trapezoidal state-space system with its
self-oscillation point computed at start-up, Formant A-E-I-O-U bank), per-source
destinations with Serial/Parallel routing, 5 ms crossfade on filter-type change,
all warp modes (Sync, Bend±, PWM, Mirror, Asym±, Quantize, Remap, FM from the
other oscillator / from the sub, AM, RM) with Eco/High/Ultra oversampling using
polyphase IIR halfbands (latency reported stays 64 samples = limiter only), and
the WAV/AIFF importer (Fixed, Pitch-detect by autocorrelation, Single-cycle,
`clm ` chunk) plus the exporter (32-bit float + `clm ` chunk).

Design notes that came out of the measurements:
- When oscillating at 2×/4×, the mip level is still chosen against the host
  Nyquist; the oversampling headroom is reserved for harmonics the warp
  creates. Otherwise table harmonics land in the halfband transition band and
  fold back.
- Sync uses a 6-sample raised-cosine dip around the master reset (removes both
  the value and the slope discontinuity). Quantize is a phase staircase with a
  whole number of steps per cycle and polyBLEP-corrected steps.

Warp aliasing (worst non-harmonic component below 20 kHz relative to the
fundamental; saw, MIDI 84 = C6, warp 50 %, 48 kHz):

| Mode | Eco | High (target ≤ −50) | Ultra (target ≤ −65) |
|---|---|---|---|
| Sync | −21.8 | −65.7 | −79.5 |
| Bend+ / Bend− | −40.6 | −59.5 | −74.0 |
| Bend± | −42.3 | −75.1 | −92.6 |
| PWM | −34.7 | −53.1 | −65.6 |
| Mirror | −42.2 | −60.3 | −74.7 |
| Asym+ / Asym− | −43.1 | −61.9 | −76.4 |
| Quantize | −92.6* | −62.0 | −82.7 |
| Remap | −75.1 | −92.3 | −92.6 |
| FM (Other) | −92.6 | −92.6 | −92.6 |
| FM (Sub) | −74.7 | −85.2 | −85.2 |
| AM / RM | −92.6 | −92.6 | −92.6 |

\* At Eco and C6 the staircase is finer than one sample, so Quantize is inactive
there.

The 20 kHz limit is deliberate: partials just above 24 kHz fold to just below
24 kHz inside the halfband's transition band; they are inaudible and would
otherwise dominate the number.

Other Phase 3 results: halfband passband flat (< 0.05 dB) and stopband
< −80 dB; latency identical at Eco/High/Ultra (64); routing tests for F1, F2,
Both and Direct in Serial and Parallel; every filter type stable at
resonance 0.9; filter-type switch delta ≤ 1.5× steady state; formant peaks
within 5 % for A, E, I, O, U; frame-morph test: no factory table drops more
than 3 dB between adjacent positions; import tests (correct frame counts, DC <
0.001, peak −1 dBFS ± 0.02 dB, nothing above harmonic 512, export → re-import
identical within 1e-4); a preset with an embedded table round-trips through a
clean folder and plays.

## Phase 4 — Modulation engine (tag `phase-4`)

Built (in the engine since Phase 2, tested now): sources Env 1–3, LFO 1–4
(9 shapes incl. Custom, Free/Retrig/Env, delay, fade, smooth, phase,
one-shot), MSEG (32 points, per-segment curves, loop region, one-shot / loop /
sustain-loop, time or BPM length), macros, velocity, release velocity, note,
mod wheel, pitch bend, aftertouch, poly AT / MPE pressure, MPE slide,
random-per-note, alternate, unison voice index, CC A/B. 64-slot matrix in
normalized space with polarity, power curve, aux and mod-on-mod. Per-sample
path for pitch, WT position, warp, cutoff, amp and pan; 32-sample control rate
on a counter that carries across blocks for everything else. Free + sync LFOs
lock to ppq while playing; unsynced Free LFOs restart on transport start.

| Test | Target | Measured |
|---|---|---|
| Envelope stage timing | ±1 % | pass (delay, attack, hold, decay, release) |
| LFO phase lock to ppqPosition | — | max error < 1e-4 cycle |
| LFO → voice_pitch ±15 ct | ±1 ct | −15.23 / +15.02 ct |
| Cutoff depth 0.1 | 1 octave ±5 % | ×1.9953 |
| Matrix sum / clamp / curve / aux / macro→many / mod-on-mod | exact | pass |
| Parameter sweep (all 329 params, held chord) | no NaN/inf, ≤ +6 dBFS pre-limiter | pass, worst 1.000 (0 dBFS) |
| Block-size test, every LFO + envelope + MSEG routed | ≤ 1e-6 | pass |
| State round-trip incl. routings and MSEG | identical | pass (Phase 1 test) |

## Phase 5 — GUI (tag `phase-5`)

Built: custom vector LookAndFeel (graphite #1C1D20, panels #26282C, text
#D8D8D8, amber #F2A33A, Inter via BinaryData); 1000×700 window, resizable
75–200 % with locked aspect, size remembered; top bar (preset name/browser,
prev/next, search, category filter, favourite, Save/Save As, A/B, Undo/Redo,
Randomize with section locks + Mutate, Quality, CPU + voice count, master
volume, panic); tabs SYNTH, MOD, FX, WAVETABLE, ARP/VOICING, SETTINGS.
SYNTH: osc panels with large wavetable displays (live playhead of the newest
voice, warp drawn on the waveform, drag to scrub), sub, noise, both filters,
amp envelope, macros with drag handles, and the bottom tab strip (LFO 1–4,
Env 2–3, MSEG) with drag handles and drawable shape editors. Knobs: value on
hover/drag, double-click reset, Cmd/Ctrl-drag fine, click the value to type,
modulation ring with a live dot, drop target for sources, Alt-drag edits the
modulation depth, right-click menu (remove modulation, MIDI learn, reset,
copy/paste, type value). Tooltips and accessibility names/titles on controls.
MOD page: all 64 routings (source, destination, depth, polarity, curve, aux,
bypass, clear). Repaint is driven by `VBlankAttachment`, at most 60 fps, and
only components whose values changed repaint.

| Test | Target | Result |
|---|---|---|
| pluginval strictness 10 incl. editor tests (Linux VST3) | pass | SUCCESS |
| Editor created/destroyed 200× while audio renders (Debug + ASan) | no glitch, no leak | output bit-identical to a run without the editor; ASan clean |
| Idle editor | < 5 % of a core | 4.4 % here — see note |
| Dragging modulation while playing | no dropouts | worst audio-thread CPU per 2.67 ms block 0.29 ms |

Note on idle CPU: measured under Xvfb + openbox with software rendering. With
our VBlank work disabled and tooltips off it is the same 4.4–4.5 %, and the
editor repaints 1–2 times in 4 s of idle, so this is JUCE's X11 event loop on
this VM, not PRISM's GUI code. Check Activity Monitor on the Mac.

## Phase 6 — FX rack and master (tag `phase-6`)

Built: Distortion (tanh, hard clip, fold, tube, bitcrush; 2× IIR oversampled),
Tape (wow, flutter, saturation, hiss, age), Dimension, Chorus, Phaser (2–12
stages), Flanger, Rotary (horn/drum, acceleration ramps, drive, mic width, mod
wheel toggles speed), Delay (sync/ms, ping-pong, HP/LP in the loop, wow,
ducking), Reverb (8/16-line FDN, size, decay, damping, pre-delay, modulation,
width, low cut, freeze, granular +12 st shimmer in the loop), Compressor,
3-band up/down Multiband, EQ. Drag-reorderable rack (crossfaded through dry,
10 ms total), 10 ms bypass crossfades, auto-sleep (decided on absolute tick
boundaries, woken on the exact first non-silent sample, so it is
block-size-independent). Master: bass mono, width, soft clip, volume, 64-sample
lookahead limiter (ceiling −1.1 dBFS, reported as latency), meters, scope /
spectrum. Tail length = longest active FX tail, 30 s cap, 30 s when frozen.

| Test | Target | Result |
|---|---|---|
| Measured vs reported latency | equal | 64 = 64 |
| FX tails end and every module sleeps | yes | all 12 asleep, output silent |
| Reverb freeze holds | indefinitely | −0.11 dB after 32 s |
| Denormals: loud input then 60 s silence | CPU flat | worst 2.26 % first 10 s, 0.64 % last 10 s |
| Bypass / enable / reorder while playing | ≤ 1.5× delta | 1.00 / 0.77 / 0.80 |
| Block-size test with every module on | ≤ 1e-6 | pass |
| Heavy patch benchmark | < 25 % | 15.3 % (Phase 2 table) |

## Phase 7 — Wavetable editor (tag `phase-7`)

Built: WAVETABLE tab with frame drawing (mouse), keyframes + interpolation,
formula editor (in-house parser: x, y, pi, e; sin cos tan abs sqrt pow exp log
min max floor sign tri saw square rand; evaluated on a background thread,
non-finite → 0, then hygiene and an atomic swap), import with mode selection
and preview, morph A → B into a new table, export to 32-bit float WAV with a
`clm ` chunk. Table hygiene is now idempotent (a table that already starts on
an upward zero crossing is not rotated again).

| Test | Target | Result |
|---|---|---|
| Formula fuzz, 10,000 random/malformed expressions | no NaN/inf/crash | 8,119 parsed, 1,881 rejected, all finite |
| Edits while audio plays (formula + factory swaps) | no dropouts | worst audio-thread CPU per block 0.36 ms of 2.67 ms |
| Export → re-import | identical | max difference 6e-8 |

## Phase 8 — Arp, voicing extras, presets, polish (tag `phase-8`)

Built: arpeggiator (Up, Down, Up-Down, seeded Random, As-Played, Chord; 1–4
octaves; synced divisions incl. dotted/triplet; gate 1–200 %; swing; latch;
velocity as played / fixed / 16-step accent pattern; restarts on transport
start), chord mode (learn from held keys, strum 0–200 ms up/down/alternate),
MPE (lower zone, per-note bend range 48, pressure and slide; turning MPE on
adds pressure → cutoff and slide → WT position), Mutate, scope/spectrum, and
the 26-preset factory bank (all SPEC recipes including Testimony Chop Keys) in
`Presets/Factory`, embedded via BinaryData. The bank is generated by
`tools/make_factory_presets.py` and loudness-balanced with
`PrismTool normalize Presets/Factory -16`.

To let macros reach things the recipes ask for, three parameters are
continuous: `reverb_freeze` and `rotary_fast` (switch-like, ≥ 0.5 = on) and the
new `arp_speed` (½× / 1× / 2×). Preset switches mute until the new preset's
wavetables are generated (5 s cap), so the waveform never jumps under a held
note.

| Test | Target | Result |
|---|---|---|
| Every factory preset loads, saves, reloads identically | identical | pass (all 26) |
| Loudness on C3-E3-G3 (MIDI 60/64/67), loudest-400 ms RMS | ±3 dB | spread 0.65 dB (−16.28 … −15.64) |
| Peaks | < −1 dBFS | ≤ −1.10 dBFS |
| 1,000 rapid preset switches while notes play | no crash, no click | worst Δ = 0.45× steady state |
| Randomize ×500 | never silent, never > 0 dBFS | quietest −49.1 dBFS RMS, loudest −1.10 dBFS |
| Arp sync over 64 bars (variable chunk sizes) | in sync | 1,024 steps, worst ±1 sample |
| 16-bar arp render after transport restart (Random mode, drift 100 %) | bit-identical | bit-identical |
| pluginval strictness 10 (final build) | pass | SUCCESS |

Profiling: `xctrace` is macOS-only, so the profiling pass used callgrind on
the benchmark. The top hotspots it found and fixed: (1) per-sample per-channel
filter calls → block processing with a bit-exact mono path, (2) per-sample
routing/pan work → bus mixing with constant-pan fast paths and a single-lane
oscillator path with cached mip choice, (3) control-tick parameter
conversions → per-voice cached conversions and active-slot lists. Init went
from 10.9 % to ~4–6 % on this VM.

Final benchmark on this VM (Xeon 2.1 GHz, noisy, one core, 48 kHz, 128 blocks):
idle with FX on 1.2 %, Init 16 voices 4.5–6.4 % across runs, heavy patch
18.2 %. **Re-measure on the Mac** (`PrismBench init 10`) — the Init budget
of 4 % has not been confirmed on that hardware.

## Known issues / not verified here

- Not run (needs the Mac): `lipo`, `auval`, pluginval on the AU, Logic itself,
  `xctrace`, and the benchmark on the Mac's CPU.
- Idle editor CPU measured 4.4 % under Xvfb (JUCE's X11 loop dominates; PRISM
  repaints nothing at idle). Check on the Mac.
- A preset switch while playing can be silent for as long as its wavetables
  take to generate (typically 0.1–0.5 s; Sync Sweep about 2.5 s).
- Warp aliasing is measured below 20 kHz (content just above Nyquist folds
  into 20–24 kHz). Eco mode has no target and Sync is −22 dB there.
- Tags `phase-1` … `phase-8` exist locally; the session's git proxy refused tag
  pushes, so push them yourself: `git push origin --tags`.
