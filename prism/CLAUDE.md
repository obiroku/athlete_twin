# CLAUDE.md — rules for building PRISM

PRISM is a personal project. It is built for one Intel Mac running Logic Pro
and will not be distributed.

## How to work
- Read SPEC.md before every task. Build ONLY the phase I ask for. Phase
  prompts live in PHASES.md.
- At the end of a phase: run all tests, the benchmark, auval, and pluginval
  (AU); update PROGRESS.md; commit; tag `phase-N`; then STOP and report:
  (1) what was built, (2) test and benchmark results with numbers,
  (3) exactly what I should test by ear in Logic, (4) known issues.
- Commit after every working step, not just at phase end.
- Scope tags in SPEC.md: MUST > SHOULD > LATER. If a phase is too big,
  cut SHOULD items and say so. Never ship a broken MUST.
- Do not modify the JUCE submodule.
- Approved dependencies: JUCE 8 (used under the JUCE Starter licence),
  Catch2 v3 (Boost Software License), and the Inter font (SIL OFL).
  Ask before adding anything else. Anything new must be MIT, BSD, ISC,
  zlib, or Boost licensed. No GPL/AGPL code other than JUCE itself.
- Note names follow Logic: middle C (MIDI 60) is C3. Tests always state
  MIDI note numbers.

## Real-time safety (non-negotiable)
On the audio thread: no allocation, no locks, no file I/O, no logging,
no exceptions, no std::function construction, and no destroying
heap objects.
- `juce::ScopedNoDenormals` at the top of processBlock.
- Preallocate everything in prepareToPlay. If the host sends a block
  larger than declared, process it in chunks.
- Support sample rate and block size changes at any time.
- Params: APVTS atomics, smoothed per SPEC.md.
- GUI → audio, non-parameter edits (mod routings, FX order, wavetable
  swaps): a lock-free SPSC command FIFO, applied at block start.
- Audio → GUI (playheads, mod values, meters): atomics or SPSC FIFO,
  written once per block.
- Heavy objects (wavetables, filter banks) are swapped via atomic
  pointer. Retired objects go to a garbage list that the message
  thread frees.
- The editor may be destroyed at any time. The processor never holds
  pointers into the editor.
- One exception: when `isNonRealtime()` is true (offline bounce),
  processBlock may wait for a wavetable that is still generating, by
  polling an atomic ready flag (sleep 1 ms; give up after 5 s and use
  the sine fallback). Never wait in real time.

## Repeatable output
- Output must not depend on block size. Control-rate ticks, smoothers,
  LFOs, and envelopes run on a sample counter that carries across
  block boundaries.
- Every random source (drift, random phase, S&H, random-per-note,
  alternate, noise) uses seeded generators. Seeds reset in
  prepareToPlay and on host transport start, so the same MIDI from the
  same start point renders identically. Offline and real-time bounces
  in Logic should null.

## Originality
All code, wavetables, presets, graphics, and names are original.
Do not copy or imitate any commercial synth's code, wavetables,
presets, UI layout, artwork, or naming. Wavetables are generated
procedurally in code or imported by the user.

## Code
- C++17, CMake, JUCE 8. Build x86_64 only.
- DSP lives in /Source/DSP, has no GUI dependencies, and is testable
  headless.
- Parameter IDs are permanent. Never rename or remove one; deprecate it.
- Every phase adds Catch2 tests in /Tests. DSP tests render offline and
  assert numbers. "Sounds fine" is not a test.
