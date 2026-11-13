# PRISM — Specification

## 1. Product
PRISM is an original wavetable synthesizer for macOS: visual, hands-on
sound design. You see the wavetable, see the modulators move, and drag
modulation onto any knob. Sound lane: neo-soul, gospel, folk-gaze,
shoegaze, indie electronic, lo-fi. Warm pads, sub bass, tape leads,
choirs, organs, keys, shimmer.

It is a personal tool for one Intel Mac running Logic Pro.

### Targets (MUST)
- JUCE 8 (latest 8.x tag, as a git submodule), C++17, CMake.
- Formats: AU (aumu) and Standalone (for testing). No VST3; Logic is
  the only host.
- Architecture: x86_64 only. CMAKE_OSX_ARCHITECTURES="x86_64".
  Deployment target macOS 11.0.
- COMPANY_NAME "Asher", BUNDLE_ID com.asher.prism,
  PLUGIN_MANUFACTURER_CODE Ashr, PLUGIN_CODE Prsm. IS_SYNTH,
  NEEDS_MIDI_INPUT, 0 inputs, stereo out.
  These codes are permanent: Logic identifies the plugin by them, and
  changing them later breaks saved projects.
- COPY_PLUGIN_AFTER_BUILD TRUE.
- Sample rates 44.1–192 kHz (must work at all; tuned for 44.1–96).
  Any block size 1–4096, including variable sizes.
- Report latency with setLatencySamples. getTailLengthSeconds returns
  the longest active FX tail, capped at 30 s (also when reverb freeze
  is on).
- Note names follow Logic: middle C (MIDI 60) is C3.

### Scope for v1.0
- MUST: Phases 1–6, plus Phase 8 presets and arp.
- SHOULD: Phase 7 wavetable editor, chord mode, MPE, multiband comp,
  scope, tooltips and accessibility names on every control.
- LATER: microtuning (MTS-ESP / Scala), 3D wavetable view, sample
  noise textures, filter FX slot.

## 2. Oscillators (2 wavetable oscillators: A, B)

### Table format
- Up to 256 frames × 2048 samples, float32.
- Every frame carries at most 512 harmonics. That keeps each table 4×
  oversampled, so interpolation error stays low.
- Factory tables are built from harmonic spectra via inverse FFT, so
  they are inherently bandlimited.
- Table hygiene, applied to every table, factory or imported:
  - DC removed from every frame.
  - Each frame FFT'd and truncated to 512 harmonics.
  - The whole table rotated by ONE offset, so frame 0 starts at an
    upward zero crossing. Never rotate frames individually: that
    scrambles phase between neighboring frames, and frame
    interpolation then cancels harmonics.
  - The whole table normalized to −1 dBFS peak with ONE gain for all
    frames.

### Bandlimiting (MUST)
- Precompute mip-maps per frame: one level per octave, 10 levels.
  Level k keeps harmonics 1 … floor(512 / 2^k). Every level is stored
  at 2048 samples (about 21 MB for a 256-frame table).
- Select the level from the voice's actual fundamental, including all
  pitch modulation, glide, drift, and unison detune (use the
  highest-pitched unison voice).
- Crossfade between the two adjacent levels so there are no timbre
  steps. The brighter level's top harmonic must never pass Nyquist:
  the crossfade reaches 100% of the darker level by the point where
  the brighter one would alias.
- Within-frame interpolation: 4-point Hermite on levels 0–1, linear on
  levels 2 and up.
- Interpolate between adjacent frames at the WT position.
- Tables are generated on a background thread. Only tables in use or
  being previewed stay resident.
- If a table isn't ready when a note plays, play a sine until it is.
  Never block in real time (offline exception: see CLAUDE.md).

### Warp anti-aliasing (MUST)
- Global Quality setting: Eco 1×, High 2× (default), Ultra 4×.
- Oscillator oversampling is active only when a warp mode or FM is on.
- Every oversampler in PRISM (oscillators, filter drive, distortion)
  uses polyphase IIR halfband filters, so reported latency never
  changes with Quality.

### Per-oscillator controls
- WT position (modulatable)
- Octave −3…+3, semi −12…+12, fine −100…+100 cents
- Pitch −24…+24 semitones, continuous (the modulation target for
  per-oscillator pitch)
- Level, pan
- Phase 0–360°, random phase 0–100%
- Destination: F1 / F2 / Both / Direct

### Global pitch
- voice_pitch −24…+24 semitones, continuous. Shifts both oscillators
  and the sub. This is the target for vibrato, drops, and bends in the
  presets.

### Warp modes (one per oscillator, with an amount knob, modulatable)
- None, Sync, Bend+, Bend−, Bend±, PWM, Mirror, Asym+, Asym−, Quantize
- Remap: user-drawn transfer curve
- FM from other oscillator / from Sub
- AM from other oscillator
- RM from other oscillator

### Unison
- 1–16 voices per oscillator.
- Detune 0–100% (exponential curve so small values are usable).
- Blend: center vs. side voice level.
- Stereo spread.
- Unison WT-position spread.
- Per-voice random or fixed start phase.
- Gain compensation: the unison sum is scaled by 1/√N.
- Process unison voices in SIMD lanes (juce::dsp::SIMDRegister,
  4-wide SSE on this machine).

### Analog drift (MUST)
Global Drift knob 0–100%. Each note gets (from seeded generators):
- a random pitch offset (up to ±8 cents)
- a slow per-voice pitch wander (about 0.3 Hz, up to ±3 cents)
- a random cutoff offset (up to ±0.15 octave)

### Sub oscillator
- Shapes: sine, triangle, square, saw. Square and saw are polyBLEP
  bandlimited.
- −1 or −2 octaves. Level, pan.
- Destination: F1 / F2 / Both / Direct.

### Noise
- White, pink (Paul Kellet filter), brown.
- One-pole tone tilt. Level, pan. Stereo or mono.
- Destination: F1 / F2 / Both / Direct.

## 3. Factory wavetables
24 tables, generated in code. All are deterministic: any randomness
uses a fixed seed. Frames morph from the first state to the last
across 256 frames unless noted. Every table obeys §2 hygiene,
including the 512-harmonic limit.

1. Basic Shapes: sine → triangle → saw → square, crossfaded in sections.
2. Saw Sweep: bandlimited saw, harmonic count rising from 1 to full.
3. Formant Stack: harmonics weighted by one formant peak sweeping
   300 Hz → 3 kHz (assuming a 110 Hz fundamental).
4. Vocal Ah: formants 730 / 1090 / 2440 Hz on a 110 Hz fundamental;
   breathiness (upper-harmonic jitter) rises across frames.
5. Vocal Ee: formants 270 / 2290 / 3010 Hz; same breath morph.
6. FM Bell: 2-operator FM, ratio 1:3, index 0 → 8.
7. FM Growl: ratio 1:2 with feedback, index 0 → 12.
8. Square Stack: odd harmonics; pulse width 50% → 5%.
9. Triangle Stack: triangle → wavefolded triangle (fold amount rises).
10. Noise Shaped: seeded random harmonic amplitudes;
    tilt −12 dB/oct → 0 dB/oct.
11. Rezo: saw with a resonant emphasis traveling up harmonics 2 → 64.
12. Hollow: even harmonics only → odd harmonics only.
13. PWM Sweep: pulse 50% → 2%.
14. Sync Sweep: hard-sync saw, ratio 1 → 8. Render naive at 64×
    resolution, FFT, truncate to 512 harmonics, inverse FFT.
15. Additive 8: harmonics 1–8, each swelling in sequence.
16. Additive 16: harmonics 1–16, each swelling in sequence.
17. Choirish: vowel morph Ah → Oo → Ee, with soft upper harmonics.
18. Organ-ish: drawbar-style harmonics (0.5, 1, 1.5, 2, 3, 4, 5, 6, 8)
    across 8 registrations, crossfaded.
19. Tape Saturate: sine → saw through tanh with rising drive
    (high-resolution render, then FFT-truncate to 512 harmonics).
20. Glass: sparse harmonics 1, 4, 9, 16, 25 … (up to 484), brightness
    rising.
21. Metallic: prime-number harmonics (2, 3, 5, 7, 11 …), weighting
    shifting upward.
22. Wobble: spectral ripple (comb-shaped amplitude curve) whose phase
    moves across frames.
23. Chord Stack: harmonics 4:5:6 (major triad) → 10:12:15 (minor triad).
24. Sub Stack: fundamental-heavy (1, 2, small 3) → sub-heavy square.

### WAV / AIFF import (backend in Phase 3, UI in Phase 7)
- Read with JUCE AudioFormatManager; sum to mono.
- Modes:
  - Fixed: split the file into N equal frames.
  - Pitch-detect: find the cycle length by autocorrelation, then
    resample each cycle to 2048 samples.
  - Single-cycle.
- If the file has a "clm " chunk, use its frame size.
- Maximum 256 frames. Apply table hygiene (§2).
- Imported tables are embedded in presets.

### Formula editor (Phase 7)
- Small in-house expression parser. Variables: x (phase 0–1),
  y (frame position 0–1), pi.
- Functions: sin, cos, tan, abs, sqrt, pow, exp, log, min, max, floor,
  sign, tri, saw, square, rand (seeded).
- Evaluate on a background thread. NaN or inf → 0. Then table hygiene,
  then atomic swap into the oscillator.

### Morph, draw, export (Phase 7)
- Morph: crossfade table A into table B to create a new table.
- Draw: draw individual frames; set keyframes and interpolate between
  them.
- Export: 32-bit float WAV with all frames concatenated, plus a
  "clm " chunk so other wavetable synths can read it.

## 4. Filters (2 per voice: F1, F2)

### Types
LP12, LP24, HP12, HP24, BP12, BP24, Notch, Peak, Comb+, Comb−,
Ladder, Diode, Formant.

### Implementation
- SVF types: TPT / ZDF state-variable filter (Zavalishin, "The Art of
  VA Filter Design"). 24 dB versions are cascaded.
- Ladder: 4-pole ZDF with tanh nonlinearity.
- Diode: diode-ladder topology.
- Comb: delay line tuned to cutoff, ± feedback.
- Formant: parallel bank of 3 bandpasses. The vowel parameter (0–1)
  morphs A-E-I-O-U, interpolated. Resonance sets formant bandwidth.
  Cutoff is ignored when the type is Formant.
- Key tracking pivots on C3 (MIDI 60):
  effective cutoff = cutoff × 2^((note − 60) / 12 × keytrack).
- At resonance 1.0 the filter self-oscillates. With keytrack 100% and
  cutoff 261.63 Hz, the self-oscillation pitch matches the played note
  within ±5 cents from C1 to C6 (MIDI 36–96).

### Per-filter controls (all modulatable)
- Cutoff, resonance, drive (0–24 dB, 2× oversampled, IIR), key tracking
  0–100%, vowel (Formant only), mix, enable.

### Changes and routing
- Changing filter type: 5 ms crossfade between old and new filter.
- Filter routing: Serial (F1 → F2) or Parallel. Each source picks its
  own filter (§2), which covers "split" setups.

## 5. Parameters

### Rules
- Every parameter uses juce::ParameterID with version hint 1. Version
  hints only increase.
- Group parameters with AudioProcessorParameterGroup so Logic's
  automation menu is organized.
- Smoothing: 20 ms for continuous parameters. Cutoff and pitch are
  smoothed in log/semitone space.
- Choice parameters are not smoothed. Crossfade them where they would
  click.
- Mod-matrix depths 1–64 and macros 1–4 are automatable parameters.
  Sources and destinations live in the state.

### Core table
Extend in the same style for every control in this spec.

| ID pattern | Range | Default | Skew / display |
|---|---|---|---|
| master_vol | −inf … +6 dB | −6 dB | dB |
| master_width | 0–200% | 100% | linear |
| bass_mono | off, 60–250 Hz | off | log |
| softclip | on/off | on | |
| quality | Eco/High/Ultra | High | |
| drift | 0–100% | 15% | linear |
| poly | 1–32 | 16 | |
| voice_mode | Poly/Mono/Legato | Poly | |
| mono_priority | Last/Low/High | Last | |
| glide_time | 0–5 s | 0 | skew, midpoint 0.2 s |
| glide_mode | Time/Rate | Time | |
| pb_range | 0–48 st | 2 | |
| vel_curve | −1…+1 | 0 | |
| voice_pitch | −24…+24 st | 0 | linear, continuous |
| oscA_/oscB_ wt_pos | 0–1 | 0 | |
| …oct / semi / fine | −3…3 / −12…12 / −100…100 ct | 0 | |
| …pitch | −24…+24 st | 0 | linear, continuous |
| …level | 0–1 | A 0.75, B 0 | shown in dB |
| …pan | −1…+1 | 0 | |
| …warp_mode / warp_amt | choice / 0–1 | None / 0 | |
| …unison | 1–16 | 1 | |
| …detune | 0–1 | 0.2 | skew, midpoint 0.15 |
| …blend / spread | 0–1 | 0.7 / 0.6 | |
| …phase / rand_phase | 0–360° / 0–100% | 0 / 100% | |
| …dest | F1/F2/Both/Direct | F1 | |
| sub_ shape/oct/level/pan/dest | | sine/−1/0/0/F1 | |
| noise_ type/tone/level/pan/dest | | white/0.5/0/0/F1 | |
| f1_/f2_ type | choice | LP24 | |
| …cutoff | 20–20000 Hz | 20000 | pure log (knob midpoint ≈ 632 Hz; 0.1 normalized ≈ 1 octave) |
| …reso | 0–1 | 0.1 | |
| …drive | 0–24 dB | 0 | |
| …keytrack | 0–100% | 0 | |
| …vowel | 0–1 (A-E-I-O-U) | 0 | |
| …mix | 0–1 | 1 | |
| filter_routing | Serial/Parallel | Serial | |
| env1–3_ delay/attack/hold | 0–10 s | 0/1 ms/0 | skew, midpoint 0.2 s |
| …decay | 0–20 s | 300 ms | skew, midpoint 0.5 s |
| …sustain | 0–1 | env1 1, env2/3 0 | |
| …release | 0–20 s | 50 ms | skew, midpoint 0.5 s |
| …curve_a/d/r | −1…+1 | 0 | |
| …vel_amt | 0–100% | env1 50% | |
| lfo1–4_ rate | 0.01–50 Hz | 1 Hz | log |
| …sync / div | on/off; 8 bars … 1/64, dotted, triplet | off / 1/4 | |
| …shape | choice + Custom | Sine | |
| …mode | Free/Retrig/Env | Retrig | |
| …polarity | Uni/Bi | Bi | |
| …phase / delay / fade / smooth | | 0 / 0 / 0 / 0 | |
| macro1–4 | 0–1 | 0 | |
| mod01–64_depth | −1…+1 | 0 | |

### State
- getStateInformation writes the same JSON as a preset (§9), plus GUI
  scale.
- Top-level "format_version": 1.
- Loading runs a migration chain (v1 → v2 → …) so old Logic sessions
  always open.

## 6. Modulation (Phase 4)

### Sources
- Env 1–3, LFO 1–4, MSEG, Macro 1–4
- Velocity, release velocity
- Note (key track, bipolar around C3 / MIDI 60)
- Mod wheel, pitch bend, channel aftertouch, poly aftertouch / MPE
  pressure, MPE slide (CC74)
- Random-per-note (bipolar, seeded), Alternate (flips sign each note)
- Unison voice index
- CC A and CC B (MIDI-learnable)

### Per-voice vs global
- Per-voice: envelopes, MSEG, and LFOs in Retrig or Env mode.
- Global: LFOs in Free mode, macros, MIDI sources.
- Free + sync LFOs lock their phase to the host ppqPosition while the
  transport plays.
- Free + unsynced LFOs reset their phase on transport start, so
  bounces repeat.

### Destinations
- Every continuous parameter, plus the depth of any other routing
  (mod-on-mod).
- Integer and choice parameters are not destinations. Pitch
  modulation goes to voice_pitch or oscA_/oscB_pitch.
- FX parameters can take per-voice sources; the newest active voice
  drives them.

### Routing fields
Source, destination, depth −1…+1, polarity (uni/bi), curve −1…+1
(power curve), aux source (scales depth, e.g. mod wheel), bypass.
Maximum 64 routings.

### Math
- Work in normalized 0–1 parameter space:
  final = base + Σ(depth × curve(src) × aux), then clamp.
- The knob ring draws exactly that range.

### Rates
- Per sample: oscillator pitch, WT position, warp amount, cutoff, amp,
  pan.
- Everything else: control rate, every 32 samples on a sample counter
  that carries across blocks, linearly interpolated.

### Envelopes
- DAHDSR with per-stage curves and velocity amount.
- Retrigger always starts from the current level, never resets to zero.
- Legato mode doesn't retrigger.
- Env 1 is hardwired to amp.
- Default routings in Init, both removable:
  - Env 2 → F1 cutoff, depth 0 (a ready slot; drag to taste).
  - Channel aftertouch → F1 cutoff, depth +0.1 (about one octave).

### LFOs
- Shapes: sine, triangle, saw up, saw down, square, S&H, smooth random,
  noise, Custom.
- Custom shapes use the MSEG editor: up to 32 points, per-segment
  curve.
- Delay 0–10 s, fade-in 0–10 s, smooth, phase offset, one-shot option.

### MSEG
- Up to 32 points with per-segment curves.
- Loop region (start and end points).
- Modes: one-shot, loop, sustain-loop.
- Length in time or BPM-synced (1/16 … 16 bars). Per-voice.

### Macros
- 4 macros, user-named. Each maps to many destinations through the
  matrix.
- Shown first on every preset.

## 7. Voicing, arp, chords

### Voices
- Poly 1–32 (default 16), plus 4 hidden tail voices used for steal
  fades.
- Modes: mono and legato, with note priority Last / Low / High.

### Glide
- Constant time or constant rate, 0–5 s.
- "Legato only" toggle.

### Voice stealing
1. A repeated key reuses its own voice.
2. Otherwise steal the quietest voice that's in release.
3. Then the quietest voice overall.
4. Then the oldest.
The stolen voice fades over 3 ms on a tail voice, so there's no click.

### Expression
- Velocity curve.
- Aftertouch → F1 cutoff as a removable default routing (§6).

### MPE (SHOULD)
- On/off. Lower zone. Per-note pitch-bend range defaults to 48.
- Pressure → amp/cutoff default routing. Slide → WT position default
  routing.

### Chord mode (SHOULD)
- Hold notes and press Learn; single keys then play that chord,
  transposed.
- Strum 0–200 ms, direction up / down / alternate.

### Arpeggiator
- Modes: Up, Down, Up-Down, Random (seeded), As-Played, Chord.
- Range 1–4 octaves.
- Rate: synced divisions, including dotted and triplet.
- Gate 1–200%. Swing 0–75%. Latch.
- Velocity: as played, fixed, or 16-step accent pattern.
- Restarts on host transport start.

## 8. FX rack and master (Phase 6)

### Rack behavior
- FX are global (after the voice sum), in drag-reorderable slots.
- Every slot has enable/bypass and mix (where meaningful).
- FX order is saved in presets.
- Bypass and reorder use a 10 ms crossfade.
- FX sleep when their input is silent and their tail has ended.

### Modules
- Distortion: tanh, hard clip, fold, asymmetric tube, bitcrush
  (bits + sample-rate reduce). Drive, tone, mix. 2× oversampled (IIR).
- Tape (MUST): wow (rate, depth), flutter, saturation, hiss, age
  (lowpass + highpass), mix.
- Dimension: multi-tap stereo widener built on fixed short delays.
  Size, mix.
- Chorus: rate, depth, feedback, mix.
- Phaser: 2–12 stages, rate, depth, feedback, mix.
- Flanger: rate, depth, feedback, mix.
- Rotary (MUST): slow/fast with acceleration ramp, horn/drum balance,
  drive, mic width. Mod wheel toggles speed by default.
- Delay: sync or ms, ping-pong, feedback, HP/LP filters in the feedback
  path, wow, ducking, mix.
- Reverb: 8- or 16-line FDN. Size, decay, damping, pre-delay,
  modulation, width, low cut, freeze, mix.
  - Shimmer amount (MUST): granular pitch shift +12 st inside the
    feedback loop.
- Compressor: threshold, ratio, attack, release, knee, makeup, mix.
- Multiband (SHOULD): 3-band upward + downward compression with depth
  and time controls.
- EQ: low shelf, peak, high shelf, HP, LP.

### Master section
- Bass mono, width, soft clip, master volume.
- Lookahead limiter (64 samples), always on, with the latency reported
  to the host.
- Output meter.

### CPU budget
Measured with the headless benchmark on the development Mac (Intel),
48 kHz, 128-sample blocks, one core. Record the CPU model and clock in
PROGRESS.md.
- Idle, FX on, no notes: < 1%
- Init patch, 16 voices held: < 4%
- Heavy patch (8 voices × 2 oscillators × 8 unison, both filters,
  drive, reverb + delay + chorus + tape): < 25%
- Silent voices are fully skipped.
- If the heavy target can't be met after optimization, report the
  number and the top hotspots rather than cutting features.

## 9. Presets (Phase 8; format in Phase 1)

### Format
- .prismpreset (JSON). Fields:
  - format_version, name, author, category, tags[], description
  - macros [{name, value}]
  - params {id: value in real units}
  - mod_routings [...], fx_order [...]
  - wavetables {oscA: {factory: "Choirish"} or {embedded: base64,
    frames, name}}
  - mseg, lfo_custom_shapes, arp, chord
- Favorites live in the user settings file, not in presets.

### Locations
- Factory presets: in BinaryData, read-only.
- User presets: ~/Music/PRISM/Presets
- User tables: ~/Music/PRISM/Wavetables
- Settings: ~/Library/Application Support/PRISM/settings.json

### Loading
- Parse on the message thread.
- 10 ms output fade-out, apply the preset, fade-in. Reset FX tails.
- Must never click and never crash under rapid switching.

### Browser
- Search, category filter, favorites, prev/next, save, save-as.
- A/B compare, undo/redo.

### Randomize
- Lock toggles per section: Osc, Filter, Env, Mod, FX, Arp.
- Musical constraints:
  - amp attack ≤ 2 s
  - release ≤ 6 s
  - cutoff ≥ 200 Hz unless an envelope opens it
  - at least one sound source audible
  - output stays under the limiter
- SHOULD: Mutate (small random variation, amount knob).

### Loudness
All factory presets, playing a C3–E3–G3 test chord (MIDI 60, 64, 67),
measure within ±3 dB RMS of each other. Peaks stay below −1 dBFS.

### Categories
Init, Keys, Bass, Pad, Lead, Pluck, Organ, Choir/Vox, Arp, Texture.

### Factory bank recipes
These are starting points. Tune by ear, but keep each preset's
character and its macros. Unlisted values are Init defaults.
"Pitch" means voice_pitch. "Vowel" means f1_vowel.

**Init** (Init)
- Osc: A Basic Shapes, pos 0 (sine).
- Filter: F1 open.
- Amp env: A 1 ms, D 0, S 1, R 50 ms.
- FX: rack empty (the master limiter is always on).

**Neo-Soul Keys** (Keys)
- Osc A: FM Bell pos 0.25. Osc B: Basic Shapes pos 0 (sine), level 0.6.
- Filter: F1 LP12 4 kHz, keytrack 50%.
- Amp env: A 1 ms, D 1.8 s, S 0.3, R 0.5 s.
- Mod:
  - velocity → A wt_pos +0.3 (harder hits are brighter)
  - LFO1 5 Hz → A/B pan, opposite signs (stereo tremolo)
  - drift 20%
- FX: Tape (light wow) → Chorus 25% → Reverb 20%.
- Macros: Bark (vel→pos depth + drive), Tremolo, Tape, Space.

**Gospel Pad** (Pad)
- Osc A: Choirish pos 0.3, unison 5, detune 0.15. Osc B: Organ-ish,
  −12 st, level 0.2.
- Filter: F1 LP24 2.2 kHz.
- Amp env: A 0.8 s, R 2.5 s.
- Mod: Env2 (A 1.5 s) → cutoff +0.25; LFO1 0.2 Hz → A wt_pos ±0.1.
- FX: Chorus → Reverb 60%.
- Macros: Air (reverb + cutoff), Movement (LFO depth), Width, Warmth
  (drive + tape).

**Folk-Gaze Wash** (Pad)
- Osc A: Saw Sweep pos 0.6, unison 7, detune 0.25. Osc B: Glass, +12 st,
  level 0.3.
- Noise: pink 0.1, dark.
- Filter: F1 LP24 1.5 kHz.
- Amp env: A 1.2 s, R 4 s.
- Mod: LFO1 0.1 Hz → cutoff.
- FX: Tape → Chorus → Delay 3/8 ping-pong 40% → Reverb (size 0.9,
  shimmer 0.4, mix 55%).
- Macros: Haze, Shimmer, Wobble, Wash.

**Upright Sub** (Bass)
- Voicing: mono legato, glide 30 ms.
- Osc A: Sub Stack pos 0.1. Sub: sine −1, 0.5, Direct.
- Noise: brown, with Env3 (D 40 ms) → noise level (finger thump).
- Filter: F1 Ladder 600 Hz, reso 0.15, keytrack 60%.
- Amp env: A 2 ms, D 600 ms, S 0.5, R 120 ms.
- Mod: Env2 → cutoff.
- Master: bass mono 150 Hz.
- FX: Compressor → EQ.
- Macros: Thump, Tone, Length, Grit.

**Tape Lead** (Lead)
- Voicing: mono legato, glide 80 ms.
- Osc A: Saw Sweep pos 0.9. Osc B: Square Stack pos 0.3, fine +7.
- Filter: F1 Ladder 2.5 kHz, reso 0.3, drive 6 dB.
- Mod: LFO2 5.5 Hz (delay 0.4 s, fade 0.6 s) → pitch ±15 ct, with mod
  wheel as aux.
- FX: Distortion tube 20% → Tape (wow 0.4, flutter 0.3) → Delay
  dotted-1/4 25% → Reverb 20%.
- Macros: Wobble, Grit, Echo, Vibrato.

**Choir Stack** (Choir/Vox)
- Osc A: Vocal Ah, unison 6. Osc B: Vocal Ee, unison 6.
- Filter: F1 Formant, vowel on Macro1.
- Mod: LFO1 0.08 Hz → both wt_pos.
- FX: Dimension → Reverb large 50%.
- Macros: Vowel, Breath (noise level), Size, Shimmer.

**Donut Shop Bass** (Bass)
- Voicing: mono, glide 40 ms.
- Osc A: Basic Shapes pos 0.4. Sub: sine −1.
- Filter: F1 LP24 400 Hz.
- Mod: Env2 pluck (D 180 ms) → cutoff; velocity → cutoff; drift 30%.
- FX: Distortion (tape-style tanh 15%) → Compressor → EQ low shelf +3.
- Master: bass mono.
- Macros: Thump, Warmth, Wobble (drift), Bite.

**Shoegaze Shimmer** (Pad)
- Osc A: Glass, unison 5. Osc B: Saw Sweep pos 0.4, level 0.4.
- Filter: F1 HP12 300 Hz.
- Amp env: A 2 s, R 6 s.
- FX: Chorus → Delay 1/4 ping-pong 35% → Reverb (shimmer 0.7, mix 70%).
- Macros: Bloom, Shimmer, Drift, Freeze (reverb freeze).

**Pew Organ** (Organ)
- Osc A: Organ-ish (registration on Macro1). Osc B: Organ-ish, +12 st,
  level 0.4.
- Amp env: A 5 ms, S 1, R 80 ms.
- Noise: Env3 (D 15 ms) → noise level (key click).
- FX: Distortion tube 10% → Rotary (slow; mod wheel → fast) → Reverb
  hall 25%.
- Macros: Drawbars, Rotor, Drive, Room.

**Nebraska Winter** (Pad)
- Osc A: Hollow pos 0.3. Osc B: Triangle Stack pos 0.1, −12 st.
- Amp env: A 1.5 s, R 5 s.
- Mod: LFO1 0.05 Hz → pitch ±4 ct.
- FX: Tape (age 0.6, hiss 0.3) → Reverb dark long 45%.
- Macros: Cold (HP + cutoff), Tape Age, Wind (noise), Distance.

**Voicemail Keys** (Keys)
- Built on Neo-Soul Keys.
- Filter: F1 BP12 1.2 kHz, reso 0.3.
- FX: Distortion bitcrush (10 bits, 8 kHz) → Tape age 0.8.
- Mod: LFO3 S&H 3 Hz → amp −0.2 (dropouts).
- Macros: Signal (bandwidth), Crush, Dropout, Room.

**Trail Pluck** (Pluck)
- Osc A: Additive 16, with Env2 (D 250 ms) → wt_pos −0.5. Osc B: Glass,
  +12 st, level 0.3.
- Filter: F1 LP12, Env2 → cutoff.
- Amp env: A 1 ms, D 400 ms, S 0, R 300 ms.
- FX: Delay dotted-1/8 ping-pong → Reverb.
- Macros: Length, Brightness, Echo, Space.

**Haze Pad** (Pad)
- Osc A: PWM Sweep, with LFO1 0.3 Hz → wt_pos. Osc B: Hollow.
  Unison 4.
- Filter: F1 LP24 900 Hz.
- Mod: Env2 swell → cutoff.
- FX: Tape → Chorus → Reverb.
- Macros: Motion, Haze, Width, Space.

**Basement Sermon Bass** (Bass)
- Voicing: mono.
- Osc A: Square Stack. Osc B: FM Growl, level 0.25.
- Filter: F1 Ladder 300 Hz, reso 0.4, drive 12 dB.
- Mod: Env2 punch → cutoff.
- FX: Distortion fold 20% → Compressor.
- Macros: Growl (B level + FM), Drive, Punch, Tone.

**Testimony Chop Keys** (Keys)
- Needs chord mode (SHOULD). If chord mode is cut, leave this preset
  out of the bank.
- Osc A: Organ-ish. Osc B: Additive 8.
- Amp env: D 250 ms, S 0.2.
- Chord mode: maj9 (0, 4, 7, 11, 14), strum 15 ms.
- FX: Distortion 10% → Delay 1/16 → Reverb plate-ish.
- Macros: Strum, Length, Crunch, Space.

**Sunday Morning** (Organ)
- Osc A: Choirish. Osc B: Organ-ish, level 0.5.
- Amp env: A 0.3 s, R 1.5 s.
- FX: Rotary slow → Reverb 40%.
- Macros: Light (cutoff + B level), Rotor, Choir (A level), Room.

**Glide Sub** (Bass)
- Voicing: mono, glide 120 ms, legato only.
- Osc A: Basic Shapes pos 0. Sub: sine −1.
- Mod: Env2 → pitch −12 st over 300 ms (drop).
- FX: Distortion 10%.
- Master: bass mono.
- Macros: Drop, Glide, Dirt, Length.

**Ladder Funk Bass** (Bass)
- Voicing: mono legato.
- Osc A: Saw Sweep pos 1.
- Filter: F1 Ladder 250 Hz, reso 0.6.
- Mod: Env2 (D 150 ms) → cutoff +0.5, with velocity as aux.
- Macros: Squelch, Decay, Drive, Sub.

**Reese Haze** (Bass)
- Osc A: Saw Sweep pos 1, unison 2, detune 0.08. Osc B: same, −12 st.
- Filter: F1 LP24 700 Hz.
- Mod: LFO1 0.15 Hz → cutoff.
- FX: Chorus → Distortion 15%.
- Macros: Width, Motion, Grit, Tone.

**Breathing Choir** (Choir/Vox)
- Osc A: Choirish, unison 6. Noise: pink 0.08.
- Filter: F1 Formant.
- Mod: LFO1 0.15 Hz → vowel.
- FX: Reverb 50%.
- Macros: Breath, Vowel Speed, Size, Shimmer.

**Brass Swell** (Lead)
- Osc A: Saw Sweep pos 0.8, unison 3.
- Filter: F1 LP24.
- Mod: Env2 (A 300 ms) → cutoff +0.4; aftertouch → cutoff.
- FX: Chorus → Reverb.
- Macros: Bloom, Bite, Width, Room.

**Harp Arp** (Arp)
- Built on Trail Pluck.
- Arp: Up, 2 octaves, 1/16, latch on.
- Macros: Rate, Length, Echo, Space.

**Dream Arp** (Arp)
- Osc A: Glass.
- Arp: Up-Down, 3 octaves, 1/8 triplet.
- FX: Delay ping-pong 40% → Reverb (shimmer 0.5).
- Macros: Rate, Shimmer, Echo, Gate.

**Kalimba Drift** (Pluck)
- Osc A: FM Bell pos 0.5.
- Amp env: D 350 ms, S 0. Drift 40%.
- FX: Tape → Reverb 25%.
- Macros: Tine, Length, Tape, Space.

**Frozen Drone** (Texture)
- Osc A: Wobble, with LFO1 0.03 Hz → wt_pos. Noise: pink 0.2.
- FX: Reverb freeze on Macro4.
- Macros: Motion, Air, Dark, Freeze.

## 10. GUI (Phase 5)

### Window
- 1000 × 700 default. Resizable 75–200%, aspect locked; size is
  remembered.
- Custom LookAndFeel, all vector.
- Colors: graphite background #1C1D20, panels #26282C, text #D8D8D8,
  one amber accent #F2A33A.
- Font: Inter (OFL license), bundled via BinaryData.

### Top bar
- Preset name, prev/next, search, category filter, favorite star.
- Save, A/B, undo/redo.
- Randomize with section locks.
- Quality selector, CPU meter, voice count, master volume, panic.

### Tabs
SYNTH, MOD (matrix), FX, WAVETABLE, ARP / VOICING, SETTINGS
(pitch-bend range, MPE, velocity curve, GUI scale, tooltips, folders).

### SYNTH tab
- Osc A and B panels, each with a wavetable display:
  - large animated waveform of the current frame
  - live WT-position playhead showing the newest voice's modulated
    position
  - warp drawn on the waveform
  - drag on the display to scrub position
- Sub, noise, both filters, envelopes.
- Bottom area: tabbed mod sources (LFO 1–4, Env 2–3, MSEG) with
  drawable editors. Each source has a drag handle.

### Knobs
- Value shown on hover and drag. Double-click resets. Cmd-drag gives
  fine control. Click the value to type one.
- Modulation ring shows the range, with a moving dot at the live value.
- Right-click menu: remove modulation, MIDI learn, reset, copy/paste
  value.
- SHOULD: tooltip and accessibility name on every control.

### Other views
- FX tab: drag slots to reorder.
- SHOULD: output scope and spectrum analyzer.

### Threading
- Repaint through VBlankAttachment, at most 60 fps, dirty regions only.
- Idle editor uses < 5% of one core.
- Opening or closing the editor never affects audio.
