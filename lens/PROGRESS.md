# LENS — Progress

All seven phases are built and tagged (`phase-1` … `phase-7`). Everything
below was built and tested in a Linux cloud container (Intel Xeon 2.1 GHz,
GCC 13, JUCE 8.0.15). **Nothing has run on a Mac yet**: the AU build,
`lipo`, `auval`, pluginval and every Logic check are still to do (see
"Mac checklist" at the end).

## Results at a glance

| | |
|---|---|
| Catch2 | 67 test cases, 22,097 assertions, all pass (Release) |
| AddressSanitizer (Debug) | see "Sanitizer run" below |
| Benchmark, 48 kHz, 128-sample blocks, one core | see table below |
| auval / pluginval / Logic | not run (needs the Mac) |

### Benchmark (`lens_bench`, processBlock only)

CPU: Intel(R) Xeon(R) Processor @ 2.10GHz (cloud). Record the Mac's CPU
here after the first Mac run.

| Scenario | % of one core | Budget |
|---|---|---|
| All bands off | 0.38% | — |
| Zero Latency, 8 static bands | 0.65% | < 1.5% ✓ |
| Zero Latency, 8 bands incl. 4 dynamic | 1.44% | < 3% ✓ |
| Linear Phase Medium, 8 bands incl. 4 dynamic | 4.0% | < 15% ✓ |
| Analyzer, 3 traces (own thread) | 3.1% | reported separately |

## Phase 1 — Scaffold
JUCE 8.0.15 submodule, Catch2 v3.7.1, CMake (AU + Standalone, x86_64, macOS
11.0, Asher / com.asher.lens / Ashr / Lens). All 513 parameters (9 global +
24 × 21) with version hint 1, grouped Global, Band 01 … Band 24. JSON state
and presets with a migration chain. Offline render helper (any rate, fixed
or variable blocks, side-chain, WAV), benchmark, `tools/get_pluginval.sh`.

Tests: parameters/defaults/ranges/groups, bus layouts, state round-trip
(5 seeds, two generations), bad/unknown/future state, preset round-trip,
pass-through bit-identical at 1, 64, 517, 4096 and a variable pattern.

## Phase 2 — EQ engine (Zero Latency)
All shapes, slopes 6–96 dB/oct, Tilt, Notch, Band Pass, All Pass; Stereo /
Mid / Side / Left / Right; 10 ms crossfades on shape/slope/stereo/on; 20 ms
smoothing (frequency and Q in log, gain in dB) on an 8-sample tick grid;
output gain, curve-based auto-gain (K-weighted pink), delta,
latency-aligned bypass, A/B and preset crossfades (lock-free command
queue), undo/redo.

| Acceptance | Result |
|---|---|
| Null, all bands off / at 0 dB | bit-identical / < −120 dB ✓ |
| Response vs digital design (1,220 cases) | worst 0.00003 dB (limit 0.1) ✓ |
| Response vs analog prototype below fs/4 | worst 0.39 dB (limit 0.5) ✓ |
| Slopes two octaves past the corner | within 1 dB of Butterworth, −3.01 dB at corner ✓ |
| Side on mono / Left leaves Right / M-S transparency | < −120 dB / bit-identical / < −120 dB ✓ |
| Click/zipper: sweep, add/remove, shape changes | ≤ 1.5 × steady ✓ |
| Delta + aligned dry = processed | < −100 dB ✓ |
| Auto-gain, +6 dB shelf at 5 kHz on pink | within 1 dB of input LUFS ✓ |
| Block size, 8 mixed bands | bit-identical at every pattern ✓ |
| 44.1–192 kHz | no NaN/inf ✓ |

## Phase 3 — Display and analyzer
Vector LookAndFeel (SPEC colours, Inter), 1000 × 650 editor scaling
75–200% with locked aspect and remembered size; top bar, output section,
loudness readout; spectrum display with pre/post/side-chain traces,
smoothing, speed, tilt, freeze, hover readout (frequency, Logic note +
cents, level), piano and zone strips, peak finder, click-to-add on a frozen
analyzer, band dots with ring meters and every interaction in SPEC §12
(drag, Cmd fine, Option solo, scroll Q, Shift multi-select, Delete,
right-click menu, double-click add). Analyzer on its own thread from
lock-free FIFOs; vblank repaints at most 60 fps, only when something
changed.

| Acceptance | Result |
|---|---|
| Sines at 100 Hz, 1 kHz, 10 kHz (44.1/48/96 kHz) | within 1/24 oct and 1 dB ✓ |
| Peak finder, 220 Hz resonance in pink | marked at 221 Hz after 2.1 s; no markers on plain pink ✓ |
| EBU 3341 1 kHz at −23 dBFS | −23.0 LUFS ±0.1 ✓ |
| Flooded FIFO, audio never waits | worst block 0.45 ms with 282,088 samples dropped ✓ |
| Editor created/destroyed 200× during rendering (ASan) | no errors or leaks; audio bit-identical to a run with no editor ✓ |
| Idle editor | 1.8% (Release, headless — re-measure on the Mac in a window: `LENS_TEST_ONSCREEN=1`) |
| pluginval incl. editor tests | not run |

## Phase 4 — Dynamic EQ
Dynamic Bell and shelves, Above/Below, range, threshold, ratio, 6 dB soft
knee, attack/release, band-following or Custom detector on the main input
or external side-chain, Listen, Learn (undoable), activity, ring meters,
lookahead with reported latency, side-chain trace.

| Acceptance | Result |
|---|---|
| Gain computer, all four moves | within 0.5 dB of the formula ✓ |
| Attack/release 63% timing | within ±10% ✓ |
| Ring meter vs applied gain | within 0.5 dB ✓ |
| Custom 5–10 kHz detector, 1 kHz vs 7 kHz | ignores / reacts ✓ |
| External side-chain only | ✓ (stereo and mono buses) |
| Learn | threshold on a 0.5 dB step, activity 15–25% afterwards ✓ |
| Lookahead 0/5/10 ms at 44.1/48/96 kHz | reported = measured ✓ |
| Click test, 0.1 ms attack / 10 ms release at 60 Hz | ✓ |
| Block size with dynamic bands + lookahead | bit-identical ✓ |
| Benchmark, 8 bands incl. 4 dynamic | 1.44% ✓ |

## Phase 5 — Coach and presets
LESSONS.md → JSON (`tools/lessons_to_json.py`) embedded with BinaryData;
Coach panel (Now, Recipes, Lessons, Glossary), Explain line, tip engine
with Got it / Don't show again / Quiet, recipes with completion checks and
control highlights, lessons with Try it, glossary links in Coach text,
tooltips from the Controls table, welcome card; factory bank (17 presets)
generated from the SPEC table (`tools/make_factory_presets.py`); preset
browser with search, categories, favourites, notes, save, save-as.

| Acceptance | Result |
|---|---|
| Every tip: fires on trigger, silent in a neutral state | 21/21 ✓ |
| Every recipe step completion check | 40/40 ✓ |
| Explain templates word for word | 26 cases (every shape, static/dynamic, both modes, cut/boost, detectors, stereo, activity) ✓ |
| Content check (IDs both ways, no TODO in JSON) | ✓ |
| Coach never changes the sound; actions undoable | ✓ |
| Every factory preset loads/saves/reloads identically | 17/17 ✓ |
| 1,000 rapid preset switches during playback | no crash, worst step within 1.5 × steady ✓ |

## Phase 6 — Linear Phase
2×2 zero-phase kernels from the analog prototypes (LL/RR, LR/RL only when
needed), Low/Medium/High scaled with sample rate, dynamic bands as parallel
kernel paths with delayed gain, mode switch with fade, latency shown in ms.

| Acceptance | Result |
|---|---|
| Static impulse symmetry | asymmetry −146 dB ✓ |
| Magnitude vs analog, Medium from 40 Hz / High from 20 Hz | worst 0.000006 dB (limits 0.2 / 0.5) ✓ |
| Reported = measured latency, every resolution × 6 sample rates | ✓ |
| Mid/Side and Left/Right tests | ✓ (Left leaves Right bit-identical; all-off is a bit-exact delay) |
| Dynamic band vs Zero Latency | 0.025 dB, timing identical ✓ |
| Mode switch, no click | ✓ |
| Block size | < −100 dB ✓ |
| Benchmark Medium, 8 bands incl. 4 dynamic | 4.0% ✓ |

## Phase 7 — Extras (SHOULD) — all five built
| Item | Acceptance | Result |
|---|---|---|
| Spectral dynamics | outside span < −90 dB; resonance cut by the amount ±1 dB; latency both modes | ✓ |
| Ear trainer | never active after load, off when the editor closes, never in state | ✓ |
| Reference and EQ Match | ≤ 6 bands, Q ≤ 2, ±6 dB, never applied without a click, file vs itself suggests nothing | ✓ |
| Warm | within 0.5 dB of Clean on pink at −18 dBFS | ✓ |
| Brickwall (Linear Phase) | ≥ 60 dB one-sixth octave past the corner (200 Hz, 1 kHz, 5 kHz) | ✓ |
| pluginval strictness 10 | | not run |

## Deviations from SPEC (and why)
1. **Zero Latency filter coefficients.** The filters are TPT state-variable
   filters as specified, but their coefficients come from a
   magnitude-matched design (impulse-invariant poles, numerator fitted at
   DC, Nyquist and the band frequency) instead of plain pre-warping. Plain
   bilinear filters miss the Phase 2 test (wide bells were 0.66 dB off at
   3 kHz; high-Q shelves 3.7 dB). The SVF structure, per-sample modulation
   and zero latency are unchanged, and there is almost no cramping.
2. **Attack/release** smooth the dynamic *gain* in dB (attack while the
   band moves away from 0 dB, release while it returns) rather than the
   detector level. This gives exact attack and release times.
3. **Activity** is the share of time the detector level is past the
   threshold, the same quantity Learn targets.
4. **Linear Phase convolution** uses LENS's own partitioned convolver, not
   `juce::dsp::Convolution`. JUCE swaps in a fresh engine with no input
   history, which made centred kernels drop out for ~170 ms on every
   change. Latency is taps/2 + one partition (taps/32, at least 256), which
   SPEC §3 allows ("plus the convolution engine's own latency, reported
   exactly"). Medium at 48 kHz: 8,704 samples (181 ms).
5. **Linear Phase dynamic bands:** the first 8 dynamic bands get their own
   path; any more act as static bands in Linear Phase.
6. **Spectral dynamics** run at the end of the chain (all spectral bands
   share one STFT), and in Linear Phase they add their 2048 samples rather
   than fitting inside the kernel latency.
7. **Warm** keeps its level with slow RMS matching (±1 dB of makeup).
8. **Offline vs real-time bounces:** Zero Latency output is fully
   deterministic. In Linear Phase, kernel rebuilds after a parameter change
   happen on a background thread in real time (synchronously when Logic
   renders offline), so a bounce with *automated static bands in Linear
   Phase* may not null against a real-time bounce. Without such automation
   it does.

## Known issues
- AU, auval, pluginval and Logic are untested (no Mac in this session;
  running pluginval here was blocked by the environment's permissions).
- The GUI was rendered and screenshotted on Linux; fonts and layout on
  macOS should match but are unverified.
- Idle editor CPU was measured headless. Re-measure on the Mac with
  `LENS_TEST_ONSCREEN=1 ./build/lens_tests "[idle]"`.
- Undo restores band values to float precision (JUCE stores the real
  value; a restore can move it by one float step, ~0.000004%).
- Learn requested while no audio is running waits until audio starts.

## Coach text LESSONS.md doesn't have
1. **Explain line for Brickwall cuts.** The cut template says "at {slope}
   dB per octave"; Brickwall has no dB/oct. It currently shows
   "TODO: brickwall".
2. **Signed gains in shelf sentences.** Following "gains with a sign", a cut
   reads "Lowers everything below about 100 Hz by −3.0 dB." Consider
   unsigned gains for the Raises/Lowers templates.
3. **Factory preset notes** for presets with dynamic bands end with "Play
   the track and press Learn." (taken from the Learn tooltip).
4. **UI labels** (not advice) that aren't in LESSONS.md: band panel hint
   "Double-click the display to add a band."; buttons Start, Not now, Back,
   Skip, Next, Done, Stop, Got it, Don't show again, Try it, Hidden band,
   Accept, Discard; ear-trainer labels (Level, Streak); EQ Match status
   lines ("Load a reference first", "Capture your track first",
   "Analysing …", "Could not read that file", "N suggestions",
   "Capturing N s").
5. **Try it values** LESSONS.md leaves open: "adds a Bell at 1 kHz" uses
   0 dB, Q 1; "a +10 dB narrow Bell" uses 1 kHz, Q 8.

## Sanitizer run
(Filled in below after the Debug + AddressSanitizer run of the full suite.)

## Building on the Mac

Needs Xcode (or the Command Line Tools) and CMake 3.22+
(`brew install cmake ninja`).

```bash
git clone --recurse-submodules -b claude/gallant-euler-sx9qc7 https://github.com/obiroku/athlete_twin.git
cd athlete_twin/lens
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/lens_tests
./build/lens_bench
```

The AU is copied to `~/Library/Audio/Plug-Ins/Components/LENS.component`.

```bash
lipo -archs ~/Library/Audio/Plug-Ins/Components/LENS.component/Contents/MacOS/LENS
auval -v aufx Lens Ashr
tools/get_pluginval.sh
tools/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/Components/LENS.component
LENS_TEST_ONSCREEN=1 ./build/lens_tests "[idle]"
```

## Mac checklist (by ear, in Logic)
The PHASES.md checklist after every phase, plus:
1. LENS on an audio track; the **Side Chain** menu appears in the plugin
   header.
2. Drag bands while playing (also Cmd-drag, Option-drag to solo, scroll for
   Q): no clicks or zipper noise.
3. Automate three controls, save, quit, reopen: sounds identical.
4. Close and reopen the plugin window while playing.
5. Buffer 32 → 1024 and 44.1 → 96 kHz with the project open.
6. Bounce offline and in real time from the same bar; flip polarity on one:
   silence (Zero Latency; also Linear Phase without automated bands).
7. Switch presets rapidly while playing (top-bar arrows and the browser).
8. Test Oscillator sweep: the analyzer shows 100 Hz, 1 kHz and 10 kHz in
   the right places; hover shows Logic note names (middle C = C3).
9. Bass track, Side Chain = kick, load **Kick and Bass Split**, press Learn:
   the bass's low end dips only on kick hits.
10. **Linear Phase:** the track stays in time with the others (restart
    playback if needed); the top bar shows the latency in ms.
11. **Dynamic bands at 0 dB** (most factory presets): the ring meter moves
    and Delta lets you hear the change.
12. **Coach:** welcome card on first open, a recipe end to end (Recipes →
    Even out boomy bass notes), tips appear and can be dismissed, glossary
    links work, tooltips show on every control.
13. **Ear trainer** (Coach panel): the banner shows, the hidden band toggles,
    clicks score; closing the window turns it off.
14. **EQ Match:** load a reference, capture your track, Suggest, Accept one
    ghost band, Undo.
15. **Warm** sounds no louder than Clean; **Brickwall** is only selectable in
    Linear Phase.
