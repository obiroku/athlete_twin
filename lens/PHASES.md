# LENS — Phase prompts

## Start here (one time)
1. Make a folder, for example `~/Code/lens`, and run `git init` in it.
2. Put CLAUDE.md, SPEC.md, PHASES.md, and LESSONS.md in it.
3. In Terminal, `cd` into the folder and run `claude`.
4. Paste Phase 1. Start a fresh session (`/clear`) before each new
   phase.
5. After each phase, do the Logic checklist at the bottom of this file
   before pasting the next phase.

---

## PHASE 1 — Scaffold
Read CLAUDE.md and SPEC.md. Implement Phase 1:
- Add JUCE (latest 8.x tag) as a git submodule. Fetch Catch2 v3 with
  FetchContent.
- CMake project with AU and Standalone targets, x86_64 only, codes and
  bus layout from SPEC §1 (main mono/stereo plus optional side-chain).
- APVTS with every parameter in SPEC §10 (all 24 band slots and all
  globals), in their groups. The audio path passes sound through
  unchanged for now.
- State and preset JSON per SPEC §10 and §11, with an empty migration
  chain.
- tools/get_pluginval.sh: downloads pluginval from its GitHub releases
  into tools/.
- Test helper: processes a buffer or WAV offline at a chosen sample
  rate and block size (fixed or a variable-size pattern), with optional
  side-chain input.
- Headless benchmark harness (Tests/bench) that prints the real-time
  factor and the CPU model.

Acceptance:
- `lipo -archs` on the AU binary shows x86_64.
- `auval -v aufx Lens Ashr` passes.
- pluginval at strictness 10 passes for the AU.
- State round-trip: save → load → identical parameters.
- Pass-through is bit-identical at block sizes 1, 64, 517, 4096, and a
  variable pattern.
- LENS loads on a Logic audio track, and the Side Chain menu appears
  in the plugin header.

---

## PHASE 2 — EQ engine (Zero Latency)
Implement SPEC §3 for Zero Latency mode:
- All shapes except Brickwall, all slopes, Tilt, Notch, Band Pass,
  All Pass.
- Stereo, Mid, Side, Left, Right per band; greyed out on mono.
- Per-band on/off, solo, 10 ms shape crossfade.
- Output gain, auto-gain, delta, latency-aligned bypass, A/B with
  crossfade, undo/redo model (headless).
- Smoothing per SPEC §10.
- Use JUCE's GenericAudioProcessorEditor for now.

Acceptance:
- Null test: all bands off, or all at 0 dB, differs from the input by
  less than −120 dB.
- Response test: every shape at 20 frequencies × 5 gains × 3 Q values
  matches its digital design formula within 0.1 dB, and matches the
  analog prototype within 0.5 dB below a quarter of the sample rate.
- Slope test: each cut slope's attenuation two octaves past the corner
  is within 1 dB of the Butterworth value.
- Stereo tests: a Side band on mono-summed input leaves output
  unchanged (< −120 dB difference). A Left band leaves the right
  channel bit-identical. Mid/Side encode-decode with 0 dB bands is
  transparent.
- Click/zipper test: a 1 kHz sine while a band sweeps 100 Hz → 10 kHz
  in 1 s, while bands are added and removed, and while shapes change.
  The max sample-to-sample delta stays ≤ 1.5 × the larger of the
  steady-state values before and after.
- Delta test: delta output + aligned dry = processed output within
  −100 dB.
- Auto-gain test: pink noise through a +6 dB high shelf at 5 kHz, with
  auto-gain on, lands within 1 dB of the input's short-term LUFS.
- Block-size test passes with 8 bands of mixed shapes and stereo modes.
- Sample rates 44.1, 48, 88.2, 96, 176.4, 192 kHz all run without NaN
  or inf.
- Benchmark: Zero Latency, 8 static bands, < 1.5%.

---

## PHASE 3 — Display and analyzer
Implement SPEC §6 and §12 (without the Coach panel):
- LookAndFeel, layout, top bar (Coach toggle can be a placeholder),
  band panel, interactions.
- EQ curve drawn from the real digital response.
- Analyzer: pre, post, and side-chain traces, smoothing, speed, tilt,
  freeze, hover readout with note names, piano strip, zone strip from
  LESSONS.md, peak finder, click-to-add on a frozen analyzer.
- Track type selector and the four workflow step buttons (they
  highlight tools; Coach behavior comes in Phase 5).
- Loudness readout (input, output, difference).
- Preset browser UI (Init only for now).

Acceptance:
- Analyzer accuracy: sines at 100 Hz, 1 kHz, and 10 kHz show peaks
  within 1/24 octave and within 1 dB of the true level (window
  corrected).
- Peak finder: pink noise plus a resonance at 220 Hz (+10 dB, Q 8)
  gets a marker within 1/6 octave of 220 Hz within 3 s, and no markers
  on plain pink noise.
- Loudness: the EBU Tech 3341 stereo 1 kHz test tone at −23 dBFS reads
  −23.0 LUFS ±0.1.
- With the analyzer FIFO flooded and the editor open, the audio thread
  never waits (instrumented test).
- pluginval at strictness 10 on the AU, including its editor tests.
- A test that creates and destroys the editor 200 times while a render
  thread runs: no glitches, no leaks (Debug build under ASan).
- Idle editor < 5% CPU.

---

## PHASE 4 — Dynamic EQ
Implement SPEC §4:
- Dynamic Bell, Low Shelf, and High Shelf with Above/Below, range,
  threshold, ratio, soft knee, attack, release.
- Detector: band-following or Custom HP/LP, internal or external
  side-chain, Listen.
- Learn, activity %, ring meters on band dots, the dynamics panel.
- Global lookahead with reported latency.
- Side-chain trace on the analyzer when active.

Acceptance:
- Gain computer: steady tones at known levels give dynamic gain within
  0.5 dB of the SPEC §4 formula, for all four moves.
- Timing: a level step across threshold reaches 63% of its final gain
  change within ±10% of the attack time; release likewise.
- Meter: the ring-meter value matches the gain actually applied within
  0.5 dB.
- Detector: with Custom HP 5 kHz / LP 10 kHz, a 1 kHz tone at 0 dBFS
  doesn't trigger a band thresholded at −20 dB; a 7 kHz tone does.
- External side-chain: a band with det External reacts only to the
  side-chain signal, not the main input.
- Learn: on a test file with known level statistics, the band is
  active 15–25% of the time afterward.
- Lookahead: reported latency equals measured latency at 0, 5, and
  10 ms.
- Click test (Phase 2 method) with a 0.1 ms attack and 10 ms release
  on a bass-range dynamic band.
- Benchmark: Zero Latency, 8 bands including 4 dynamic, < 3%.

---

## PHASE 5 — Coach and presets
Implement SPEC §7 and §11, using LESSONS.md as the only source of Coach
text:
- Convert LESSONS.md to JSON in /Resources/coach (explain templates,
  zones, tips, recipes, lessons, glossary, controls) and embed it.
- Coach panel with Now, Recipes, Lessons, Glossary tabs.
- Explain line, tip engine with "Got it" / "Don't show again" / Quiet
  mode, recipes with completion checks and control highlights, lesson
  cards with Try it, glossary links, tooltips from the Controls table.
- Welcome card on first run.
- The factory bank from SPEC §11 as JSON in /Presets/Factory, embedded
  via BinaryData. Browser with search, categories, favorites, notes.

Acceptance:
- Every tip rule in LESSONS.md has a unit test that builds the trigger
  state and checks that the tip fires, and a test that it doesn't fire
  in a neutral state.
- Every recipe step's completion check has a unit test.
- Explain templates: tests for each shape, static and dynamic, both
  modes, cut and boost, check the generated sentence word for word.
- Content check: every ID in LESSONS.md appears in the JSON and vice
  versa; no TODO left unless listed in the report.
- No Coach action changes the sound without a click; every Coach
  action is undoable (tested).
- Every factory preset loads, saves, and reloads identically.
- Stress test: 1,000 rapid preset switches during playback — no
  crash, no click above threshold.

---

## PHASE 6 — Linear Phase
Implement the Linear Phase part of SPEC §3:
- 2×2 kernel builds on a background thread, Low/Medium/High
  resolution, juce::dsp::Convolution loading.
- Dynamic bands as parallel kernel paths with a delayed gain signal.
- Mode switch with fade and latency update; latency shown in the UI.

Acceptance:
- Impulse response of static bands is symmetric around its center
  (asymmetry below −100 dB).
- Magnitude matches the analog target within 0.2 dB from 40 Hz to
  20 kHz at Medium, and within 0.5 dB from 20 Hz at High.
- Reported latency equals measured latency at every resolution and
  sample rate.
- Mid/Side and Left/Right tests from Phase 2 pass in Linear Phase.
- A non-overlapping dynamic band's gain change matches Zero Latency
  mode within 1 dB and its timing within 1 ms.
- Mode switch: no click (Phase 2 method, allowing the planned fade).
- Block-size test passes in Linear Phase.
- Benchmark: Linear Phase Medium, 8 bands including 4 dynamic, < 15%.

---

## PHASE 7 — Extras (SHOULD; cut items if needed)
Implement in this order, and stop to report if the phase grows too big:
1. Spectral dynamics (SPEC §5).
2. Ear trainer (SPEC §8, levels from LESSONS.md).
3. Reference and EQ Match (SPEC §9).
4. Warm character (SPEC §3).
5. Brickwall cuts in Linear Phase.

Acceptance:
- Spectral: bins outside the band's span are untouched (difference
  below −90 dB); a resonance inside the span is reduced by the set
  amount within 1 dB; latency reported correctly in both modes.
- Ear trainer: never active after load, turns off when the editor
  closes, never written to state.
- EQ Match: suggestions never exceed 6 bands, Q 2, or ±6 dB, and are
  never applied without a click. Matching a file to itself suggests
  nothing.
- Warm: level within 0.5 dB of Clean on pink noise at −18 dBFS.
- Brickwall: attenuation at least 60 dB one-sixth octave past the
  corner.
- pluginval strictness 10 still passes.

---

# Checklist after every phase (in Logic)

1. Put LENS on an audio track and play.
2. If Logic doesn't list LENS, run `killall -9 AudioComponentRegistrar`,
   then rescan in Logic Pro > Settings > Plug-in Manager.
3. Drag bands while playing: no clicks, no zipper noise.
4. Automate three controls, save the project, quit, and reopen. It
   should sound identical.
5. Close and reopen the plugin window while playing.
6. Change the buffer size (32 → 1024) and the sample rate
   (44.1 → 96 kHz) with the project open.
7. Bounce offline and in real time from the same start bar. Put both on
   tracks, flip the polarity of one: you should hear silence or
   near-silence.
8. Switch presets rapidly while playing.
9. From Phase 3: play a sine sweep (Logic's Test Oscillator plugin in
   sweep mode) and check that the analyzer shows 100 Hz, 1 kHz, and
   10 kHz in the right places.
10. From Phase 4: put LENS on a bass track, set the Side Chain menu to
    the kick track, load Kick and Bass Split, and press Learn. The bass's
    low end should dip only on kick hits.
11. From Phase 6: switch to Linear Phase and check that the track stays
    in time with the others (stop and restart playback if needed).
