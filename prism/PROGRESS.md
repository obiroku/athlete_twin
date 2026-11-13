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
