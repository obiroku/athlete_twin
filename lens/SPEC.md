# LENS — Specification

## 1. Product
LENS is an original visual dynamic EQ for Logic Pro, with a built-in
teacher. You see the spectrum, grab a band and drag it. Dynamic bands
move only when the signal needs it. The Coach explains every move while
you make it, so using LENS teaches you to mix and master with EQ.

Built for: upright and electric bass, kick, neo-soul keys, gospel
vocals and choirs, folk-gaze and shoegaze guitars, synths, the mix bus,
and mastering my own releases.

A personal tool for one Intel Mac running Logic Pro.

### Targets (MUST)
- JUCE 8 (latest 8.x tag, as a git submodule), C++17, CMake.
- Formats: AU effect (aufx) and Standalone (for testing). No VST3.
- Architecture: x86_64 only. CMAKE_OSX_ARCHITECTURES="x86_64".
  Deployment target macOS 11.0.
- COMPANY_NAME "Asher", BUNDLE_ID com.asher.lens,
  PLUGIN_MANUFACTURER_CODE Ashr, PLUGIN_CODE Lens. These codes are
  permanent: Logic identifies the plugin by them.
- Buses: main input and output, mono→mono or stereo→stereo, plus an
  optional side-chain input (mono or stereo). Logic shows it as the
  Side Chain menu in the plugin header.
- COPY_PLUGIN_AFTER_BUILD TRUE.
- Sample rates 44.1–192 kHz (must work at all; tuned for 44.1–96).
  Any block size 1–4096, including variable sizes.
- Latency reported exactly with setLatencySamples in every mode.
  getTailLengthSeconds returns 1 s, or the linear-phase kernel's
  ring-out if longer.
- Note names follow Logic: middle C (MIDI 60) is C3.

### Scope
- MUST: Phases 1–6 (engine, display and analyzer, dynamic EQ, Coach and
  presets, Linear Phase and loudness).
- SHOULD: Phase 7 (spectral dynamics, reference and EQ Match, ear
  trainer, Warm character, brickwall cuts), accessibility names.
- LATER: draw-a-curve mode, a list of every LENS instance in the
  session. (Logic doesn't let a plugin open another track's plugin
  window, and track names are unreliable inside an AU, so the instance
  list would be half a feature.)
- Not doing: a third "in-between" phase mode. Zero Latency and Linear
  Phase cover tracking, mixing, and mastering.

## 2. Workflow (the easy path)
Everything in LENS follows one loop, shown as four step buttons in the
top bar: CLEAN → CONTROL → SHAPE → CHECK. Clicking a step points the
Coach at that step and highlights the tools for it. Steps are guidance
only; every tool is always available.

1. CLEAN: remove what doesn't belong. Rumble, mud, ringing
   resonances. Low cuts and narrow cuts. The peak finder marks
   resonances for you.
2. CONTROL: tame what only happens sometimes. Boomy notes, harsh
   hits, sibilance. Dynamic bands with Learn.
3. SHAPE: gentle, broad boosts for tone. Body, presence, air.
4. CHECK: compare honestly. Auto-gain, A/B, bypass, delta, and the
   loudness readout. Judge in the full mix, not soloed.

### Track type
A selector in the top bar: Bass, Kick & Drums, Vocal, Choir & Vocal
Bus, Keys, Guitar, Synth & Pad, Bus, Master. It changes labels,
analyzer zones, Coach tips, and recipe suggestions. It never changes
the sound. Saved in state and presets.

### First run
A welcome card offers the "Your first EQ move" recipe. After that,
the Coach opens to the current step.

## 3. EQ engine

### Bands
- 24 band slots. Each slot: on, shape, frequency 10 Hz–30 kHz,
  gain −30…+30 dB, Q 0.025–40, slope (cuts), stereo mode, dynamics.
- Shapes: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch,
  Band Pass, Tilt, All Pass.
  - Tilt: a first-order tilt around its frequency. Positive gain
    brightens (lows down by gain/2, highs up by gain/2).
- Cut slopes: 6, 12, 18, 24, 36, 48, 72, 96 dB/oct (Butterworth).
  Brickwall is SHOULD and exists only in Linear Phase.
- Bands run in series in slot order.
- Stereo mode per band: Stereo, Mid, Side, Left, Right. Mid/Side bands
  encode M = (L+R)/2, S = (L−R)/2, filter, then decode. On a mono
  track these options are greyed out.
- Solo (UI state, not saved): you hear only the region the band
  touches, as a band-pass at the band's frequency and Q (Q 0.7 for
  shelves and cuts).
- Changing a band's shape: 10 ms crossfade between old and new filter.

### Zero Latency mode (default)
- Bells, shelves, notch, band pass, all pass: TPT state-variable
  filters (Simper / Zavalishin), with pre-warped frequency.
  Coefficients are recomputed every 8 samples from per-sample-smoothed
  values. Dynamic gain updates every sample.
- Cuts: cascaded TPT SVF sections with Butterworth Q values; odd
  orders add a one-pole TPT section.
- Above about a quarter of the sample rate, a digital filter's shape
  bunches up ("cramping"). That's accepted in this mode, and the display
  draws the real digital response. Linear Phase has no cramping.
- A band set above 0.45 × sample rate is clamped there; the display
  shows the true curve.

### Linear Phase mode (MUST, Phase 6)
- Static bands: the combined response of all static bands is computed
  per FFT bin from the analog prototype magnitudes (no cramping), with
  zero phase. Because Mid/Side and Left/Right bands mix channels, the
  combined response is a 2×2 matrix per bin (L/R in → L/R out). It
  becomes up to four linear-phase FIR kernels: LL and RR always, and
  LR and RL only when Mid/Side bands differ between Mid and Side.
- Resolution: Low (4,096 taps), Medium (16,384, default), High
  (65,536). Taps scale with sample rate. Latency is half the kernel
  plus the convolution engine's own latency, reported exactly. The UI
  shows the latency in milliseconds next to the resolution.
- Kernels are rebuilt on a background thread when static bands change
  (debounced 30 ms while dragging) and loaded through
  juce::dsp::Convolution's thread-safe loading, which crossfades.
- Dynamic bands in Linear Phase: each dynamic band adds a parallel
  path. A band with static gain Gs and dynamic gain Gd(t) has total
  response 1 + (Gs·Gd − 1)·S, where S is the band's zero-phase
  prototype (2nd-order band-pass for bells, 2nd-order low- or high-pass
  for shelves). So output = static kernels(x) + Gs·(Gd(t) − 1)·P(x),
  where P = S × the other bands' response, as its own kernel. The
  gain signal is delayed to line up with the kernel latency, so attack
  and release feel the same as in Zero Latency. Overlapping dynamic
  bands ignore their cross terms; accepted.
- Switching mode: 20 ms fade-out, switch, 20 ms fade-in, then report
  the new latency.

### Character (SHOULD, Phase 7)
Clean (default) or Warm: gentle saturation on the output stage, 2×
oversampled with polyphase IIR halfbands, level-matched so it never
just sounds louder.

### Output
- Output gain −30…+30 dB.
- Auto-gain (default on): estimates the EQ curve's loudness change
  against a pink-noise spectrum, from the curve itself rather than the
  audio, so it never pumps. Dynamic bands count with their average gain
  over the last 3 s. Smoothed over 300 ms.
- Delta: outputs (processed − dry), with the dry signal delayed to
  match latency. You hear only what LENS adds or removes.
- Bypass: the host bypass parameter. 10 ms crossfade; latency stays
  the same, so timing doesn't jump.
- A/B: two complete band setups. Switching crossfades over 20 ms.
  Copy A → B and B → A.
- Undo/redo for band edits.
- Loudness readout (MUST): short-term LUFS (3 s, ITU-R BS.1770
  K-weighting) of input and output, plus the difference. Shown in the
  CHECK step.

## 4. Dynamic EQ (MUST, Phase 4)
- Bell, Low Shelf, and High Shelf bands can be dynamic.
- Controls:
  - Mode: Above (acts when the detector goes above threshold) or Below
    (acts when it falls below).
  - Range −30…+30 dB (default −6). Negative cuts, positive boosts.
  - Threshold −60…0 dB (default −20).
  - Ratio 1.5–10 (default 3). How quickly the band reaches its range.
  - Attack 0.1–500 ms (default 10). Release 10 ms–5 s (default 120).
  - Knee: fixed 6 dB soft knee.
  - Global lookahead 0–10 ms (default 0). In Zero Latency it adds that
    much latency, reported.
- The four moves (the Coach teaches these):
  - Above + negative range: cut when loud (harshness, boomy notes,
    sibilance). The default.
  - Above + positive range: boost when loud (more snap on hits).
  - Below + positive range: boost when quiet (bring up detail).
  - Below + negative range: cut when quiet (clean up between notes).
- Gain computer: over = level − threshold (Above) or threshold − level
  (Below), through the soft knee. Dynamic gain = sign(range) ×
  min(|range|, over × (1 − 1/ratio)) when over > 0, else 0. Band gain =
  static gain + dynamic gain, in dB.
- Detector: listens to the main input (default) or the external
  side-chain. Filtered by a band-pass that follows the band's frequency
  and Q (default), or with Custom on, by its own high-pass and
  low-pass (10 Hz–30 kHz, 12 dB/oct). Level is 2 ms RMS, then attack
  and release smoothing in dB.
- Listen (UI state, not saved): hear what the detector hears.
- Learn: press while the track plays. After 8 s (or a second press),
  set the threshold so the band acts about 20% of the time (the 80th
  percentile of detector level), rounded to 0.5 dB. Undoable.
- Activity: the percent of the last 10 s the band was moving, shown in
  the band panel. Each dynamic band's dot on the display has a ring
  meter showing its live gain change.
- External side-chain: when a band uses it and signal is arriving, the
  analyzer shows the side-chain trace.

## 5. Spectral dynamics (SHOULD, Phase 7)
- A Bell band can switch to Spectral. Inside the band's span (its
  −3 dB bandwidth), individual frequencies that stick out are pulled
  down, instead of the whole band moving.
- STFT 2048 samples (scaled with sample rate), hop 512, Hann,
  overlap-add. In Zero Latency it adds 2048 samples of latency
  (reported); in Linear Phase it fits inside the existing latency.
- Controls: threshold (how far a peak must stick out above the band's
  smoothed level, 0–24 dB, default 6), amount (max cut 0–24 dB,
  default 12), attack, release (per-bin smoothing).
- The analyzer draws pulled-down frequencies as a darker region.

## 6. Analyzer (MUST, Phase 3)
- Runs on a background thread, fed by an SPSC FIFO.
- Traces: pre-EQ (grey), post-EQ (amber), side-chain (thin outline,
  when active). Each toggles.
- FFT: Hann, 75% overlap, 8192 points at 44.1/48 kHz, scaled with
  sample rate. Smoothing: raw, 1/12, 1/6, 1/3 octave. Speed: slow,
  medium, fast, very fast. Display tilt: 0, 3, or 4.5 dB/oct (default
  4.5, so a balanced mix looks roughly flat).
- Range: 10 Hz to Nyquist (up to 30 kHz), −90…+30 dB.
- Freeze. Hovering shows frequency, note name and cents (Logic
  naming), and level.
- Piano strip along the bottom. Above it, a zone strip labeled for the
  current track type (LESSONS.md).
- Peak finder: marks up to 5 resonances in the pre-EQ spectrum: peaks
  at least 6 dB above the 1-octave-smoothed spectrum that last at least
  2 s. Clicking a marker creates a dynamic Bell (Above, range −6 dB, Q
  matched to the peak width and clamped 2–10) and starts Learn.
  Shift-click creates a static −3 dB cut instead.
- Click a point on the frozen analyzer to add a Bell there at 0 dB.

## 7. Coach (MUST, Phase 5)
The Coach teaches EQ and dynamic EQ while I work.

### Panel
- A right-side panel, toggled from the top bar. On by default until I
  turn it off. Tabs: Now, Recipes, Lessons, Glossary.
- Now tab:
  - The Explain line for the selected band: one plain-English sentence
    built from the templates in LESSONS.md, updated live (it includes
    the band's activity %).
  - Up to 2 active tips.
  - The hint for the current workflow step.
- Recipes tab: guided step-by-step tasks. Each step has text, an
  optional highlight on a control, and a completion check (for example,
  "a dynamic band exists between 60 and 250 Hz", "Learn has finished").
  A step advances when its check passes. Back and Skip always work.
  Recipes suggested for the current track type are listed first.
- Lessons tab: short cards. Some have a "Try it" button that sets up a
  demo by adding new bands (undoable); it never edits existing bands.
- Glossary tab: every term; terms in Coach text link to it.

### Tips
- Rules from LESSONS.md, evaluated on the message thread 4 times a
  second from parameter values and audio stats published by the audio
  thread (activity, levels, peak, loudness, side-chain presence).
- Each tip has "Got it" (hides it this session) and "Don't show again"
  (saved in settings). Quiet mode shows recipes and lessons only.

### Tooltips
Every control's tooltip comes from the Controls table in LESSONS.md.

### Storage
Coach progress (recipes done, lessons read, tips dismissed, ear-trainer
scores) lives in settings.json, never in presets or plugin state.

## 8. Ear trainer (SHOULD, Phase 7)
- A mode in the Coach panel. While on, a large banner reads EAR
  TRAINER ON.
- It adds one hidden test band on top of my EQ. I toggle the test band
  on and off and click where I think it is on the display. Levels and
  rules are in LESSONS.md. Score and streak are saved in settings.
- Always off when the plugin loads. Turns off (with a fade) when the
  editor closes. Never saved in presets or state.

## 9. Reference and EQ Match (SHOULD, Phase 7)
- Load a reference song (WAV, AIFF, or MP3) on a background thread and
  compute its long-term average spectrum at 1/6 octave. References are
  remembered in ~/Music/LENS/References.
- Capture my track's long-term spectrum with Start/Stop while playing,
  or capture the side-chain input instead.
- Both are level-matched. Show the difference curve, smoothed to 1/3
  octave, from 30 Hz to 16 kHz.
- Suggest up to 6 broad bands (Q ≤ 2, gain within ±6 dB) that close
  the gap. They appear as ghost bands. I accept, edit, or discard each.
  Never applied automatically.

## 10. Parameters
- Every parameter uses juce::ParameterID with version hint 1.
- Groups: Global, then Band 01 … Band 24, so Logic's automation menu
  is organized.
- Smoothing: 20 ms. Frequency and Q smoothed in log space, gain in dB.
  Choice parameters are not smoothed; crossfade where they would click.

### Per band (bNN = b01 … b24)
| ID | Range | Default |
|---|---|---|
| bNN_on | bool | off |
| bNN_shape | Bell / LowShelf / HighShelf / LowCut / HighCut / Notch / BandPass / Tilt / AllPass | Bell |
| bNN_freq | 10–30000 Hz, log | spread: 50·2^((NN−1)/3) Hz, capped at 20 kHz |
| bNN_gain | −30…+30 dB | 0 |
| bNN_q | 0.025–40, log | 1 |
| bNN_slope | 6/12/18/24/36/48/72/96/Brickwall | 12 |
| bNN_stereo | Stereo / Mid / Side / Left / Right | Stereo |
| bNN_dyn_on | bool | off |
| bNN_dyn_mode | Above / Below | Above |
| bNN_dyn_range | −30…+30 dB | −6 |
| bNN_dyn_thresh | −60…0 dB | −20 |
| bNN_dyn_ratio | 1.5–10, skew midpoint 3 | 3 |
| bNN_dyn_attack | 0.1–500 ms, log | 10 |
| bNN_dyn_release | 10–5000 ms, log | 120 |
| bNN_det_source | Internal / External | Internal |
| bNN_det_custom | bool | off |
| bNN_det_hp | 10–30000 Hz, log | 20 |
| bNN_det_lp | 10–30000 Hz, log | 20000 |
| bNN_spec_on | bool | off |
| bNN_spec_thresh | 0–24 dB | 6 |
| bNN_spec_amount | 0–24 dB | 12 |

Solo and detector Listen are UI state, not parameters.

### Global
| ID | Range | Default |
|---|---|---|
| phase_mode | Zero Latency / Linear Phase | Zero Latency |
| lp_resolution | Low / Medium / High | Medium |
| character | Clean / Warm | Clean |
| output_gain | −30…+30 dB | 0 |
| auto_gain | bool | on |
| delta | bool | off |
| bypass | bool (host bypass) | off |
| ab_slot | A / B | A |
| lookahead | 0–10 ms | 0 |

Analyzer settings, track type, and the current workflow step live in
the state, not as parameters.

### State
- getStateInformation writes the same JSON as a preset (§11), plus
  both A/B slots, analyzer settings, and GUI scale.
- Top-level "format_version": 1, with a migration chain (v1 → v2 → …)
  so old Logic sessions always open.

## 11. Presets (Phase 5)

### Format
- .lenspreset (JSON): format_version, name, category, track_type,
  note (one line: the problem it solves), description,
  bands [{slot, shape, freq, gain, q, slope, stereo, dyn {...}, det
  {...}, spec {...}}], globals {...}.
- Factory presets: BinaryData, read-only. User presets:
  ~/Music/LENS/Presets. Settings: ~/Library/Application
  Support/LENS/settings.json.
- Browser: search, category filter, favorites, prev/next, save,
  save-as. The preset note shows under the name. A preset loads into
  the current A/B slot with a 20 ms crossfade.
- A preset that uses a feature cut in Phase 7 loads with that feature
  off.
- Categories: Init, Bass, Drums, Vocal, Keys, Guitar, Synth, Bus,
  Master.

### Factory bank
Values are starting points; keep them unless a test fails. "Dyn"
means a dynamic band: mode, range, attack/release, detector. Thresholds
ship at −24 dB and the note tells me to press Learn.

| # | Name (type) | Note | Bands |
|---|---|---|---|
| 0 | Init (Bus) | A clean start. | none |
| 1 | Upright Bass Body (Bass) | Evens out boomy notes and brings out the wood. | Low Cut 35 Hz 12 dB · Bell 90 Hz +2 Q1 · Dyn Bell 180 Hz Q2.5 Above −5, 8/120 ms · Bell 700 Hz +1.5 Q1.2 · High Shelf 6 kHz −2 Q0.7 |
| 2 | Electric Bass Tame (Bass) | Holds back notes that jump out and adds definition. | Low Cut 30 Hz 24 dB · Dyn Bell 110 Hz Q3 Above −6, 8/150 ms · Bell 250 Hz −2 Q1.5 · Bell 900 Hz +2 Q1 · High Cut 9 kHz 12 dB |
| 3 | Gospel Keys Warmth (Keys) | Full, warm keys that don't turn brittle when played hard. | Low Cut 60 Hz 12 dB · Bell 200 Hz +1.5 Q0.8 · Dyn Bell 2.8 kHz Q2 Above −4, 3/80 ms · High Shelf 10 kHz +1 Q0.7 |
| 4 | Neo-Soul Vocal Air (Vocal) | Air on top without sharp S's or chest boom. | Low Cut 90 Hz 18 dB · Dyn Bell 250 Hz Q1.5 Above −3, 10/120 ms · Dyn Bell 7 kHz Q4 Above −3, 1/50 ms, Custom det HP 5 kHz LP 10 kHz · High Shelf 12 kHz +3 Q0.7 |
| 5 | Folk-Gaze Guitar Wash (Guitar) | Clears room for the vocal and softens harsh strums. | Low Cut 110 Hz 12 dB · Bell 400 Hz −2 Q1 · Dyn Bell 3 kHz Q1.5 Above −4, 5/100 ms · High Shelf 8 kHz +2 Q0.7 |
| 6 | Harshness Tamer (Bus) | Pulls down 2–5 kHz only when it flares up. | Dyn Bell 3.2 kHz Q2 Above −6, 2/60 ms |
| 7 | Boomy Room Fix (Bus) | Tames room boom and low-mid buildup. | Low Cut 40 Hz 12 dB · Dyn Bell 150 Hz Q2 Above −6, 10/150 ms · Bell 300 Hz −2 Q1.4 |
| 8 | Sibilance Cut (Vocal) | Softens harsh S and T sounds. | Dyn Bell 6.5 kHz Q3 Above −8, 1/40 ms, Custom det HP 5 kHz LP 10 kHz |
| 9 | Sub Clean-Up (Bus) | Removes rumble below the music that eats headroom. | Low Cut 30 Hz 24 dB · Dyn Low Shelf 60 Hz Q0.7 Above −4, 20/200 ms |
| 10 | Tape Lead Polish (Synth) | Lets a lead sit on top without poking out. | Low Cut 150 Hz 12 dB · Bell 1.2 kHz −2 Q1.5 · Dyn Bell 3.5 kHz Q2 Above −3, 3/80 ms · High Shelf 9 kHz +1.5 Q0.7 |
| 11 | Choir Stack Glue (Vocal) | Keeps stacked vocals clear when everyone sings at once. | Low Cut 120 Hz 12 dB · Dyn Bell 400 Hz Q1 Above −3, 10/150 ms · Dyn Bell 3 kHz Q1.5 Above −3, 3/80 ms · High Shelf 10 kHz +1.5 Q0.7 |
| 12 | Kick and Bass Split (Bass) | Dips the bass's low end only when the kick hits. Set Side Chain to the kick. | Dyn Bell 60 Hz Q1.5 Above −5, 1/120 ms, det External, Custom HP 40 Hz LP 120 Hz |
| 13 | Mastering Gentle Tilt (Master) | A small brightness/warmth balance for a whole mix. | Low Cut 20 Hz 12 dB · Tilt 1 kHz +1 |
| 14 | Mastering Air Lift (Master) | Opens the top of a mix without harshness. | High Shelf 12 kHz +1.5 Q0.7 · Dyn Bell 4.5 kHz Q2 Above −2, 5/100 ms |
| 15 | Demo to Release (Master) | A gentle all-round master EQ for home mixes. | Low Cut 25 Hz 12 dB · Dyn Low Shelf 120 Hz Q0.7 Above −2, 20/200 ms · Bell 300 Hz −1 Q1 · Dyn Bell 3 kHz Q1.5 Above −2, 5/100 ms · High Shelf 10 kHz +1 Q0.7 |
| 16 | Bass Solo Clarity (Bass) | Helps a bass solo cut through without turning it up. | Low Cut 30 Hz 12 dB · Dyn Bell 200 Hz Q2 Above −4, 8/120 ms · Bell 800 Hz +2.5 Q1 · Bell 2.5 kHz +2 Q1.2 |

## 12. GUI (Phase 3, Coach panel in Phase 5)

### Window
- 1000 × 650 default. Resizable 75–200%, aspect locked; size is
  remembered.
- Custom LookAndFeel, all vector, matching PRISM: background #1C1D20,
  panels #26282C, text #D8D8D8, one amber accent #F2A33A. Inter font
  via BinaryData.
- Band dots: numbered, light grey; the selected band is amber. Dynamic
  bands have a ring meter.

### Layout
- Top bar: preset browser (with the preset note), track type, the four
  workflow steps, phase mode (and resolution and latency in Linear
  Phase), A/B with copy, undo/redo, Coach toggle, CPU meter.
- Top right: output gain, auto-gain, delta, bypass, loudness readout
  (input, output, difference).
- Center: the spectrum display with the EQ curve, band dots, peak
  markers, zone strip, and piano strip.
- Bottom: the selected band's panel (shape, frequency with note name,
  gain, Q, slope, stereo mode; type any value). When the band is
  dynamic, a dynamics panel beside it (mode, range, threshold with
  Learn, ratio, attack, release, detector source, Custom HP/LP, Listen,
  activity %).
- Right: Coach panel (toggle).

### Interaction
- Double-click empty display: add a Bell.
- Drag a band: frequency and gain. Scroll on a band: Q. Cmd-drag: fine
  control. Option-drag: solo while dragging.
- Shift-click: multi-select; dragging moves all selected bands
  together. Delete key removes selected bands.
- Right-click a band: shape, make dynamic, stereo mode, solo, delete.
- Double-click a knob: reset. Click a value: type it.
- Every control shows its value on hover and has a tooltip from
  LESSONS.md. SHOULD: accessibility name on every control.

### Threading
- Repaint through VBlankAttachment, at most 60 fps, dirty regions only.
- Idle editor < 5% of one core.
- Opening or closing the editor never affects audio.

## 13. CPU budget
Measured with the headless benchmark on the development Mac (Intel),
48 kHz, 128-sample blocks, one core. Record the CPU model in
PROGRESS.md. Analyzer cost runs on its own thread and is reported
separately.
- Zero Latency, 8 static bands: < 1.5%
- Zero Latency, 8 bands including 4 dynamic: < 3%
- Linear Phase Medium, 8 bands including 4 dynamic: < 15%
- If a target is missed after optimization, report the number and the
  top hotspots rather than cutting features.
