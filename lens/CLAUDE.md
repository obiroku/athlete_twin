# CLAUDE.md — rules for building LENS

LENS is a personal project: a visual dynamic EQ with a built-in Coach,
for one Intel Mac running Logic Pro. It will not be distributed.

## How to work
- Read SPEC.md before every task. Build ONLY the phase I ask for. Phase
  prompts live in PHASES.md. Coach text lives in LESSONS.md.
- At the end of a phase: run all tests, the benchmark, auval, and pluginval
  (AU); update PROGRESS.md; commit; tag `phase-N`; then STOP and report:
  (1) what was built, (2) test and benchmark results with numbers,
  (3) exactly what I should test by ear in Logic, (4) known issues,
  (5) any Coach text you needed that LESSONS.md doesn't have.
- Commit after every working step, not just at phase end.
- Scope tags in SPEC.md: MUST > SHOULD > LATER. If a phase is too big,
  cut SHOULD items and say so. Never ship a broken MUST.
- Do not modify the JUCE submodule.
- Approved dependencies: JUCE 8 (used under the JUCE Starter licence),
  Catch2 v3 (Boost Software License), and the Inter font (SIL OFL).
  Ask before adding anything else. Anything new must be MIT, BSD, ISC,
  zlib, or Boost licensed. No GPL/AGPL code other than JUCE itself.
- Note names follow Logic: middle C (MIDI 60) is C3.

## Coach content
- Every word the Coach shows comes from LESSONS.md. Convert it to JSON
  in /Resources/coach and embed it with BinaryData.
- Do not write new mixing or mastering advice. If a screen needs text
  that LESSONS.md doesn't have, add a placeholder marked TODO and list
  it in the phase report.
- The Coach never changes the sound by itself. Every Coach action is a
  button I click, and it is undoable.

## Real-time safety (non-negotiable)
On the audio thread: no allocation, no locks, no file I/O, no logging,
no exceptions, no std::function construction, and no destroying
heap objects.
- `juce::ScopedNoDenormals` at the top of processBlock.
- Preallocate everything in prepareToPlay. If the host sends a block
  larger than declared, process it in chunks.
- Support sample rate and block size changes at any time.
- Params: APVTS atomics, smoothed per SPEC.md.
- GUI → audio, non-parameter edits (A/B swaps, preset loads, band
  moves from the Coach): a lock-free SPSC command FIFO, applied at
  block start.
- Audio → GUI (analyzer samples, gain-reduction values, activity,
  loudness): atomics or SPSC FIFO, written once per block. The audio
  thread only pushes; if a FIFO is full it drops data, never waits.
- Heavy objects (linear-phase kernels, FFT plans) are built on a
  background thread and swapped via atomic pointer or JUCE's
  thread-safe Convolution loading. Retired objects go to a garbage list
  that the message thread frees.
- The editor may be destroyed at any time. The processor never holds
  pointers into the editor.

## Repeatable output
- Output must not depend on block size. Smoothers, envelopes, and
  coefficient-update ticks run on a sample counter that carries across
  block boundaries.
- Offline and real-time bounces in Logic should null.

## Originality
All code, graphics, presets, and names are original. Do not copy or
imitate any commercial EQ's code, UI layout, artwork, feature names, or
preset names.

## Code
- C++17, CMake, JUCE 8. Build x86_64 only.
- DSP lives in /Source/DSP, has no GUI dependencies, and is testable
  headless. Coach logic (tip rules, recipe checks, explain templates)
  lives in /Source/Coach and is also testable headless.
- Parameter IDs are permanent. Never rename or remove one; deprecate it.
- Every phase adds Catch2 tests in /Tests. DSP tests render offline and
  assert numbers. "Sounds fine" is not a test.
