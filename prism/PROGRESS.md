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
