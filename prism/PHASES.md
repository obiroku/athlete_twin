# PRISM — Phase prompts

Paste one phase at a time into Claude Code.

---

## PHASE 1 — Scaffold
Read CLAUDE.md and SPEC.md. Implement Phase 1:
- Add JUCE (latest 8.x tag) as a git submodule. Fetch Catch2 v3 with
  FetchContent.
- CMake project with AU and Standalone targets, x86_64 only, plugin
  codes and settings from SPEC §1.
- APVTS with a parameter-ID registry ready for the SPEC §5 table, plus
  parameter groups.
- A temporary sine voice, 16-voice poly, so MIDI can be verified.
- PresetManager: JSON save/load in the §9 format, state =
  preset JSON + format_version, and an empty migration chain.
- tools/get_pluginval.sh: downloads pluginval from its GitHub releases
  into tools/.
- Test helper: renders a MIDI sequence offline at a chosen sample rate
  and block size (fixed or a variable-size pattern). Later phases use
  it for block-size and determinism tests.
- Headless benchmark harness (Tests/bench) that renders N seconds
  offline and prints the real-time factor and the CPU model.

Acceptance:
- `lipo -archs` on the AU binary shows x86_64.
- `auval -v aumu Prsm Ashr` passes.
- pluginval at strictness 10 passes for the AU.
- State round-trip test: save → load → identical parameters.
- Block-size test: the same MIDI rendered at block sizes 1, 64, 517,
  4096, and a variable pattern matches within 1e-6 peak difference.
- The Standalone plays a sine from MIDI.

---

## PHASE 2 — Core voice
Implement:
- Wavetable engine per SPEC §2: table hygiene, 10-level mip-maps at
  2048 samples, Nyquist-safe level crossfade, Hermite/linear
  interpolation, frame interpolation, background table generation,
  sine fallback, offline wait.
- Factory tables 1, 2, 8, 4, 20, 24 from §3.
- Osc A with unison (SIMD, 1/√N gain) and drift.
- voice_pitch and oscA_pitch.
- Sub and noise oscillators.
- F1 with LP12/24, HP12/24, BP12/24, and Ladder, with C3-pivot key
  tracking.
- Env 1 → amp.
- Voice manager: poly, mono, and legato modes; glide; stealing with
  tail-voice fades; pitch bend; velocity curve.
- Seeded random generators that reset on prepareToPlay and transport
  start.
- Use JUCE's GenericAudioProcessorEditor for now.

Acceptance:
- Aliasing test: a saw at MIDI 96 and MIDI 108, 48 kHz, analyzed with
  a Blackman-Harris FFT. All non-harmonic energy is below −80 dB
  relative to the fundamental. Sweep every note MIDI 24–108: below
  −70 dB.
- Glide sweep MIDI 36 → 120: no level jump > 0.5 dB between
  consecutive 10 ms windows at mip boundaries.
- Click test on note-on, note-off, steal, and retrigger, using a sine
  patch at A4 (MIDI 69), 48 kHz, with random phase at 0% and 100%: the
  max sample-to-sample delta stays ≤ 1.5 × the steady-state max.
- Ladder self-oscillation: cutoff 261.63 Hz, keytrack 100%, reso 1.0.
  Pitch is within ±5 cents of each note over MIDI 48–96.
- Determinism: two renders of the same MIDI after a reset, with drift
  100% and random phase 100%, are bit-identical.
- Block-size test (from Phase 1) passes on a unison + drift patch.
- Benchmark: Init patch, 16 voices held, < 4%.

---

## PHASE 3 — Full engine
Implement:
- Osc B (including oscB_pitch).
- F2 and the remaining filter types: Notch, Peak, Comb±, Diode,
  Formant (with the vowel parameter).
- Per-source destinations and Serial/Parallel routing.
- 5 ms crossfade on filter-type change.
- All warp modes, including FM/AM/RM, with oscillator oversampling per
  the Quality setting (polyphase IIR).
- The remaining 18 factory tables.
- WAV/AIFF import backend: all modes, "clm " chunk support, table
  hygiene (including single-offset rotation and 512-harmonic
  truncation), embedding in presets.

Acceptance:
- Measure and report aliasing for each warp mode at Eco, High, and
  Ultra. Targets: High ≤ −50 dB, Ultra ≤ −65 dB.
- Routing tests for every destination option.
- Formant test: each vowel setting's three peaks land within 5% of
  their target frequencies.
- Frame-morph test: on every factory table, sweeping WT position 0 → 1
  never drops RMS by more than 3 dB between adjacent frames.
- Import tests using generated test WAVs: correct frame count,
  DC < 0.001, peak −1 dBFS, no energy above harmonic 512.
- A preset with an embedded table round-trips on a clean user folder.
- Reported latency stays the same at Eco, High, and Ultra.

---

## PHASE 4 — Modulation engine
Implement SPEC §6:
- All sources, LFOs, envelopes, MSEG, and macros.
- 64-slot matrix with curve, aux, and mod-on-mod. Continuous
  destinations only.
- Per-sample and control-rate paths (control rate on a sample counter
  that carries across blocks).
- Host-synced LFO phase; unsynced Free LFOs reset on transport start.
- Default routings: Env2 → F1 cutoff (depth 0), aftertouch → F1 cutoff
  (depth +0.1).
- Still using the generic editor; this phase is headless and
  test-driven.

Acceptance:
- Tests for: LFO phase lock to ppqPosition, envelope stage timing
  within 1%, matrix sum and clamp, curve shapes, macro → multiple
  destinations, retrigger from current level (no click).
- Pitch test: LFO → voice_pitch ±15 ct measures ±15 ct (±1 ct).
- Cutoff test: depth 0.1 on cutoff moves it one octave (±5%).
- Parameter sweep test: every parameter is swept across its full range
  during a held chord. No NaN or inf, and nothing above +6 dBFS before
  the limiter.
- Block-size test passes on a patch with every LFO and envelope
  routed.
- State round-trip includes routings and MSEG.

---

## PHASE 5 — GUI
Implement SPEC §10:
- LookAndFeel, layout, and all tabs except WAVETABLE. The FX tab and
  the arp half of ARP/VOICING are placeholders until Phases 6 and 8.
- Wavetable displays with live playhead and scrub.
- Mod-source drag handles, knob mod rings, right-click menus.
- MOD matrix page and LFO/MSEG drawable editors.
- Preset browser with search, categories, favorites, A/B, undo/redo.
- Randomize with section locks.
- CPU meter, voice count, panic.
- GUI scaling.
- SHOULD: tooltips and accessibility names.

Acceptance:
- pluginval at strictness 10 on the AU, including its editor tests.
- A test that creates and destroys the editor 200 times while a render
  thread runs: no glitches, no leaks (Debug build under ASan).
- Idle editor < 5% CPU.
- Dragging modulation while playing causes no dropouts.

---

## PHASE 6 — FX rack and master
Implement SPEC §8:
- All modules, including Tape, Rotary, and Reverb with shimmer.
- FX tab UI: module panels, drag-reorder with crossfade; FX order saved
  in presets.
- Master section: bass mono, width, soft clip, always-on limiter with
  reported latency, meter.
- Auto-sleep for silent FX.
- Tail length per SPEC §1 (capped at 30 s).

Acceptance:
- Tail test: freeze holds indefinitely; other FX tails end and the
  module sleeps.
- Denormal test: loud input, then 60 s of silence — CPU stays flat.
- Reorder and bypass while playing: no click (same delta test as
  Phase 2).
- Reported latency matches the measured latency.
- Block-size test passes with every FX module on.
- Benchmark: heavy patch < 25% (report the number either way).

---

## PHASE 7 — Wavetable editor (SHOULD; cut items if needed)
Implement the WAVETABLE tab:
- Frame drawing with keyframe interpolation.
- Formula editor per SPEC §3.
- Import UI with mode selection and preview.
- Morph between two tables.
- Export with "clm " chunk.

Acceptance:
- Formula fuzz test: 10,000 random expressions — never a NaN, inf, or
  crash.
- Edits apply without audio dropouts.
- An exported table re-imports identically.

---

## PHASE 8 — Arp, voicing extras, presets, polish
Implement:
- Arpeggiator, plus its half of the ARP/VOICING tab.
- Chord mode and MPE (SHOULD).
- The factory bank from SPEC §9 as JSON files in /Presets/Factory,
  embedded via BinaryData. Leave out Testimony Chop Keys if chord mode
  was cut.
- Mutate (SHOULD).
- Scope/analyzer (SHOULD).
- A CPU profiling pass with `xcrun xctrace record --template
  'Time Profiler'`: fix the top 3 hotspots.

Acceptance:
- Every factory preset loads, saves, and reloads identically.
- Loudness test: all presets within ±3 dB RMS on the test chord
  (MIDI 60, 64, 67); peaks below −1 dBFS.
- Stress test: 1,000 rapid preset switches while notes play — no crash,
  no click above threshold.
- Randomize ×500: never silent, never above 0 dBFS.
- Arp stays in sync with the host transport over 64 bars.
- Determinism: a 16-bar arp render repeats bit-identically after a
  transport restart.

---

# Checklist after every phase (in Logic)

1. Play it in the Standalone first, then in Logic on a Software
   Instrument track.
2. If Logic doesn't list PRISM, run `killall -9 AudioComponentRegistrar`,
   then rescan in Logic Pro > Settings > Plug-in Manager.
3. Automate three knobs, save the project, quit, and reopen. It should
   sound identical.
4. Close and reopen the plugin window while notes play.
5. With the project open, change the buffer size (32 → 1024) and the
   sample rate (44.1 → 96 kHz).
6. Bounce offline and in real time from the same start bar. Put both
   bounces on tracks, flip the polarity of one, and play them together:
   you should hear silence or near-silence.
7. Switch presets rapidly while holding a chord.
8. Play C7 (the top key of an 88-key piano) with a bright patch and
   listen for whistling or inharmonic tones.
