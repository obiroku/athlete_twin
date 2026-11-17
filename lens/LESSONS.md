# LENS — Coach content

This file is the only source of Coach text. The builder converts each
section to JSON in /Resources/coach and keeps every ID. Text in
{braces} is filled from live values. Frequencies display as "250 Hz"
or "2.5 kHz"; gains with one decimal and a sign ("−3.0 dB").

It's also written to be read straight through, as a guide to EQ and
dynamic EQ.

---

## 1. Explain templates

The Explain line describes the selected band in one sentence.

### Width phrases (from Q)
- Q < 0.7: "Very broad: a tone move."
- 0.7 ≤ Q < 2: "Broad: musical shaping."
- 2 ≤ Q < 6: "Focused."
- Q ≥ 6: "Very narrow: surgical, for one ringing frequency."

### Static bands
- bell_boost: "Boosts {gain} around {freq} ({zone}). {width}"
- bell_cut: "Cuts {gain} around {freq} ({zone}). {width}"
- bell_zero: "Sits at 0 dB around {freq} ({zone}). Drag it up or down to hear what lives here."
- lowshelf: "{Raises|Lowers} everything below about {freq} by {gain}."
- highshelf: "{Raises|Lowers} everything above about {freq} by {gain}."
- lowcut: "Removes everything below {freq}, at {slope} dB per octave."
- highcut: "Removes everything above {freq}, at {slope} dB per octave."
- notch: "Removes a thin slice at {freq}. Use it for hum or one ringing note."
- bandpass: "Keeps only the region around {freq}. Mostly for effects or listening."
- tilt: "Tilts the whole sound {brighter|darker} around {freq}: highs {up|down} {half} dB, lows {down|up} {half} dB."
- allpass: "Shifts phase around {freq} without changing the level. An advanced tool; you usually won't need it."
- stereo suffix (when not Stereo): " Only on the {Mid (center)|Side (edges)|Left|Right} channel."

### Dynamic bands
- dyn_above_cut: "Cuts up to {range} around {freq} ({zone}) when {detector} gets louder than {thresh} dB. Moving {activity}% of the time."
- dyn_above_boost: "Boosts up to {range} around {freq} ({zone}) when {detector} gets louder than {thresh} dB. Moving {activity}% of the time."
- dyn_below_boost: "Boosts up to {range} around {freq} ({zone}) when {detector} drops below {thresh} dB. Moving {activity}% of the time."
- dyn_below_cut: "Cuts up to {range} around {freq} ({zone}) when {detector} drops below {thresh} dB. Moving {activity}% of the time."
- {detector}: "this frequency area" (Internal, following the band), "{hp}–{lp} on this track" (Internal, Custom), "the side-chain track" (External).
- shelf dynamic: replace "around {freq}" with "below {freq}" or "above {freq}".
- activity suffix: 0% → " Not moving yet: the threshold may be too high." ≥ 90% → " Almost always moving: it's acting like a normal EQ."

---

## 2. Zones

Rough guides, not rules. Every instrument is different; your ears
decide. Ranges are where the zone label starts and ends on the strip.

| Track type | Zones |
|---|---|
| Default (Bus, Synth & Pad) | Sub 20–60 · Bass 60–200 · Mud 200–500 · Body & honk 500–1.5k · Presence 1.5–4k · Edge 4–8k · Air 8k+ |
| Bass | Sub 30–60 · Weight 60–120 · Boom 120–300 · Growl & wood 300–1k · Attack & fingers 1–3k · String noise 3k+ |
| Kick & Drums | Thump 40–80 · Boom 80–200 · Box 200–500 · Body 500–2k · Click 2–5k · Cymbals & air 5k+ |
| Vocal | Rumble 20–100 · Chest 100–250 · Mud 250–500 · Nasal 500–1.5k · Clarity 1.5–4k · Sibilance & edge 4–9k · Air 9k+ |
| Choir & Vocal Bus | Rumble 20–120 · Warmth 120–250 · Mud 250–500 · Honk 500–1.5k · Clarity 1.5–4k · Sibilance 4–9k · Air 9k+ |
| Keys | Rumble 20–60 · Body 60–200 · Mud 200–500 · Honk 500–1.5k · Bark & presence 1.5–4k · Shimmer 4k+ |
| Guitar | Rumble 20–80 · Body 80–200 · Mud 200–500 · Boxy 500–1.5k · Bite 1.5–4k · Fizz 4–8k · Air 8k+ |
| Master | Sub 20–60 · Low end 60–200 · Low mids 200–600 · Mids 600–2k · Upper mids 2–5k · Presence 5–10k · Air 10k+ |

---

## 3. Workflow step hints

- step_clean: "CLEAN: take out what doesn't belong. Rumble below the music, muddy buildup, ringing frequencies. Peak markers show likely problems. Cut, don't boost, in this step."
- step_control: "CONTROL: fix things that only happen sometimes. A boomy note, a harsh hit, sharp S's. Make the band dynamic and press Learn."
- step_shape: "SHAPE: now add tone. Broad, gentle boosts (Q under 1.5, +1 to +3 dB). If a boost needs more than +4 dB, something else is probably in the way."
- step_check: "CHECK: keep auto-gain on, then flip bypass and A/B. Listen with the whole mix playing. If you can't hear a band doing anything useful, delete it."

---

## 4. Tips

Each tip: ID, when it fires, text. Thresholds are exact; the builder
tests each one.

| ID | Fires when | Text |
|---|---|---|
| tip_big_boost | any static boost > +8 dB | "That's a big boost. Big boosts usually mean something else is covering this sound. Try +2 to +4 dB, or cut the competing track instead." |
| tip_narrow_boost | a Bell boost with Q > 6 | "Narrow boosts tend to whistle. Rule of thumb: cut narrow to fix problems, boost wide to shape tone." |
| tip_louder_trap | auto-gain off and output short-term loudness differs from input by more than 1.5 LU for 3 s | "LENS is making this {louder|quieter} by {x} dB. Louder almost always sounds better to our ears, which fools us. Turn on auto-gain to judge honestly." |
| tip_solo_long | any band solo or detector Listen on for more than 15 s | "Solo is for finding a problem. Judge the fix with solo off, with the whole mix playing." |
| tip_dyn_never | a dynamic band at 0% activity for 10 s while input is above −50 dBFS | "Band {n} isn't moving. The threshold is above anything it hears. Lower the threshold, or press Learn while the track plays." |
| tip_dyn_always | a dynamic band above 90% activity for 10 s | "Band {n} is moving almost all the time, so it's acting like a normal EQ. Raise the threshold so it only catches the loud moments, or make it a static band." |
| tip_lowcut_bass | track type Bass or Kick & Drums, and a Low Cut above 45 Hz | "That low cut is removing real bass. On bass and kick, cut only below the lowest note. A low E on bass is about 41 Hz; a five-string's low B is about 31 Hz." |
| tip_lowcut_master | track type Master, and a Low Cut above 35 Hz or steeper than 24 dB/oct | "On the master, low cuts above about 30 Hz thin out the whole song. Keep master cuts low and gentle." |
| tip_master_big | track type Master, and any static band beyond ±3 dB | "Mastering moves are usually small: 0.5 to 2 dB. If you need more, the fix probably belongs in the mix." |
| tip_clip | output peak above 0 dBFS | "The output is clipping. Turn output gain down, or turn on auto-gain." |
| tip_lp_punch | Linear Phase on with track type Bass or Kick & Drums | "Linear Phase can soften punch with pre-ringing: a faint swell just before each hit. Zero Latency usually sounds punchier on bass and drums." |
| tip_lp_latency | the first time Linear Phase is switched on in a session | "Linear Phase delays the audio by {ms} ms. Logic lines it up during playback, but don't record through it. If timing seems off, stop and restart playback." |
| tip_low_release | a dynamic band below 150 Hz with release under 50 ms | "A fast release on low frequencies can flutter or distort, because one bass wave lasts longer than that. Try 80–200 ms." |
| tip_sc_missing | a band uses External detection and no side-chain signal arrives for 5 s while playing | "This band listens to a side-chain, but nothing is arriving. In Logic, pick a track in the Side Chain menu at the top of the plugin window." |
| tip_many_bands | more than 10 bands on | "That's a lot of bands. Try bypassing each one. If you can't hear the difference in the mix, delete it." |
| tip_stacked | two boosting bands within 1/3 octave of each other | "Two bands are boosting the same area. One band can do the job." |
| tip_sibilance_attack | track type Vocal or Choir & Vocal Bus, a dynamic band between 4 and 10 kHz with attack over 10 ms | "S sounds are over in a few milliseconds. For de-essing, an attack of 0.5–3 ms catches them." |
| tip_mono_side | a Side band on, and side level more than 40 dB below mid for 5 s | "This track is basically mono, so Side bands have nothing to work on." |
| tip_shelf_q | a boosting shelf with Q above 1.5 | "A high-Q shelf adds a bump near its corner. For smooth air or warmth, keep shelf Q around 0.5–0.8." |
| tip_try_delta | the first dynamic band of a session | "Try Delta: you'll hear only what LENS is taking out. If you hear the music itself instead of the problem, you're cutting too much." |
| tip_master_order | track type Master, the first time in a session | "LENS goes before your limiter on the master. EQ first, then limit." |

---

## 5. Recipes

Each step: text, then [highlight] and (completion check). Recipes
suggested for a track type are listed in brackets after the title.

### rec_first_eq — Your first EQ move [all]
1. "Play the track. Double-click anywhere on the display to add a band." (a band exists)
2. "Drag the dot up. You're boosting that frequency. Drag it left and right while you listen: hear how the sound changes?" (the band's gain or frequency changed)
3. "Scroll on the dot to change Q, the width. Narrow is focused, wide is gentle." (Q changed)
4. [bypass] "Click Bypass a few times to compare with and without." (bypass toggled twice)
5. [auto-gain] "Leave auto-gain on. It keeps the level the same, so you hear the tone change and not just a volume change." (auto-gain on)
6. "Done. Remember: small moves, judged in the full mix. Next, try a recipe for this track type."

### rec_boomy_notes — Even out boomy bass notes [Bass]
1. [track type] "Set the track type to Bass." (track type is Bass)
2. "Play a part where some notes boom louder than others. Watch for a peak marker between 60 and 300 Hz." (playing for 5 s)
3. "Click the marker. No marker? Double-click near the boomy area to add a band, then drag it up a lot and sweep left and right until the boom jumps out. Then bring it back to 0." (a band exists between 60 and 300 Hz)
4. [dynamic] "Right-click the band and choose Make Dynamic. Set range to about −5 dB." (that band is dynamic with negative range)
5. [learn] "Press Learn and keep playing for 8 seconds." (Learn finished)
6. [attack/release] "Set attack around 10 ms and release around 120 ms. Low notes need slower times." (attack 3–30 ms and release 60–300 ms)
7. "Watch the activity number. Between 10 and 40% means it's catching only the boomy notes." (activity between 10 and 40% for 5 s)
8. [delta] "Turn on Delta for a moment. You should hear just the boom, not the whole bass. Turn it off." (delta toggled on then off)
9. "Now listen with the full mix and flip bypass. The bass should feel steadier, not thinner."

### rec_deess — Soften sharp S sounds [Vocal, Choir & Vocal Bus]
1. [track type] "Set the track type to Vocal." (track type is Vocal or Choir & Vocal Bus)
2. "Add a band around 6 to 7 kHz. Make it dynamic, Q around 3, range −6 dB." (a dynamic band between 4 and 10 kHz)
3. [custom detector] "Turn on Custom detector and set it to 5 kHz–10 kHz, so it listens only to the S range." (Custom on, HP 4–6 kHz, LP 8–12 kHz)
4. [listen] "Press Listen. You'll hear what the detector hears: mostly S's. Move the band's frequency until the S's sound sharpest, then turn Listen off." (Listen toggled on then off)
5. [learn] "Press Learn while the singer sings." (Learn finished)
6. [attack/release] "Set attack 1 ms, release about 50 ms. S's are fast." (attack ≤ 3 ms, release 30–100 ms)
7. [delta] "Check with Delta: you should hear only S's and T's. If you hear words, reduce the range." (delta toggled on then off)
8. "Listen in the mix. The vocal should still sound clear, just less sharp. If it sounds lispy, you went too far."

### rec_kick_bass — Make room for the kick [Bass]
1. "Put LENS on the bass track. In Logic, set the Side Chain menu at the top of the plugin window to the kick track." (side-chain signal present)
2. "Add a band where the kick thumps, around 50 to 80 Hz. Make it dynamic, Q about 1.5, range −5 dB." (a dynamic band between 40 and 120 Hz)
3. [detector source] "Set the detector to External. Now the kick controls this band." (det source External)
4. [custom detector] "Turn on Custom and set it to 40–120 Hz, so only the kick's thump triggers it." (Custom on)
5. [learn] "Press Learn while the song plays." (Learn finished)
6. [attack/release] "Attack 1 ms, release about 100–150 ms. Longer release for slower songs." (attack ≤ 3 ms, release 60–250 ms)
7. "Listen to kick and bass together and flip bypass. The kick should punch through without the bass getting smaller."

### rec_vocal_pocket — Clear a pocket for the vocal [Guitar, Keys, Synth & Pad, Bus]
1. "Put LENS on the guitars, keys, or their bus. Set the Side Chain menu to the lead vocal." (side-chain signal present)
2. "Add a broad band around 2 to 3 kHz, where vocals are clearest. Make it dynamic, Q about 1, range −3 dB." (a dynamic band between 1.5 and 4 kHz)
3. [detector source] "Set the detector to External." (det source External)
4. [learn] "Press Learn while the vocal sings." (Learn finished)
5. [attack/release] "Attack around 10 ms, release around 200 ms, so it moves smoothly with phrases." (release 120–400 ms)
6. "Listen: when the singer comes in, the vocal should read clearer while the guitars still sound full. If you notice the guitars ducking, reduce the range."

### rec_harsh_hits — Tame harsh hits [Keys, Guitar, Vocal]
1. "Play a loud section. Harshness usually lives between 2 and 5 kHz." (playing for 5 s)
2. "Add a band, drag it up about 10 dB with a narrow Q, and sweep left and right. Stop where it sounds most painful." (a band between 1.5 and 6 kHz with gain > +6 dB)
3. "Now flip it: drag the gain to −4 dB and widen the Q to about 2." (that band's gain < 0, Q 1–4)
4. [dynamic] "Make it dynamic so it only cuts when the harshness happens." (that band is dynamic)
5. [learn] "Press Learn during a loud part." (Learn finished)
6. "Attack 2–5 ms, release 60–100 ms. Check activity: 10–40% is the sweet spot." (activity between 10 and 40% for 5 s)
7. "Flip bypass in the mix. Hard hits should feel less painful while soft parts sound the same."

### rec_master_pass — A master EQ pass [Master]
1. [track type] "Put LENS on the stereo output, before the limiter. Set the track type to Master." (track type is Master)
2. "Optional: switch to Linear Phase. For mastering it keeps everything in time across frequencies." (none)
3. "Add a low cut at 20–25 Hz, 12 dB/oct, to clear sub rumble." (a Low Cut between 15 and 35 Hz)
4. "If the low end swells on some notes, add a dynamic low shelf at about 120 Hz, range −2 dB, and press Learn." (none)
5. "For more air, add a high shelf at 10–12 kHz, +1 to +1.5 dB, Q 0.7. Then add a dynamic bell at 3–5 kHz, range −2 dB, to catch harshness the air might bring out." (none)
6. [auto-gain] "Keep auto-gain on. Check the loudness difference: it should be near 0." (auto-gain on)
7. "A/B against bypass, then play a song you love that's similar. Does yours sit in the same neighborhood?"
8. "Finally, listen on something else: earbuds, a car, a phone. If a move only sounds good on one system, make it smaller."

---

## 6. Lessons

### les_band — What an EQ band does
An EQ band turns up or down one part of the sound. Frequency is
where: low numbers are deep (bass), high numbers are bright (cymbals,
S's). Gain is how much, in decibels: +3 dB is a clear change, +1 dB is
subtle. Q is how wide: low Q is a gentle hill over a big area, high Q
is a needle. Most good EQ is a few small moves, not many big ones.
Try it: adds a Bell at 1 kHz.

### les_zones — Where sounds live
Every sound spreads across many frequencies, but each area has a
character. Lows give weight, low mids can get muddy when many
instruments pile up there, upper mids carry clarity and harshness, and
the very top is air. The zone strip under the display labels these
for your track type. They're rough guides: always confirm with your
ears.

### les_cut_boost — Cut narrow, boost wide
Problems are usually narrow: one ringing frequency, one boomy note.
Fix those with narrower cuts. Tone is broad: warmth, presence, air.
Shape it with wide, gentle boosts. Cutting what's in the way often
works better than boosting what you want: cut mud on the guitars
before you boost the vocal.

### les_sweep — Finding a problem by sweeping
Boost a narrow band a lot (+10 dB), sweep it slowly, and the problem
frequency will jump out. Then turn the boost into a cut. One warning:
almost everything sounds bad with a huge narrow boost, so don't cut
every spot that sounds bad while sweeping. Only cut what bothered you
before you started sweeping. Peak markers can save you the sweep.
Try it: adds a +10 dB narrow Bell to sweep.

### les_static_dynamic — Static or dynamic?
A static band does the same thing all the time. Use it when a problem
is always there, like constant mud. A dynamic band only moves when the
sound crosses a threshold. Use it when a problem comes and goes: one
note that booms, a singer who gets harsh only when belting, S's. A
dynamic band leaves the good moments alone, which is why it sounds
more natural.

### les_four_moves — The four dynamic moves
Dynamic bands can do four things:
1. Cut when loud: tames harshness, boomy notes, S's. The most useful.
2. Boost when loud: adds snap to hits that are already strong.
3. Boost when quiet: lifts detail when a part gets soft.
4. Cut when quiet: cleans up mud or noise between notes.
Mode picks loud (Above) or quiet (Below). Range picks cut (negative)
or boost (positive).

### les_threshold — Threshold and Learn
The threshold is the level where the band starts moving. Too high
and it never moves; too low and it moves all the time, like a normal
EQ. Press Learn while the track plays and LENS sets it to act about 20%
of the time. Then watch activity: 10–40% usually means it's catching
the problem moments and leaving the rest alone.

### les_timing — Attack and release
Attack is how fast the band reacts; release is how fast it lets go.
Fast sounds need fast attack; low frequencies need slower release.
Starting points:
- S's and T's: attack 0.5–3 ms, release 30–80 ms
- Harsh hits at 2–5 kHz: attack 2–10 ms, release 50–150 ms
- Vocal chest boom: attack 5–15 ms, release 80–150 ms
- Boomy bass notes: attack 5–20 ms, release 80–250 ms
- Kick ducking the bass: attack 0.5–3 ms, release 80–150 ms
- Vocal ducking guitars or keys: attack 5–20 ms, release 150–300 ms
- Master low end: attack 10–30 ms, release 100–300 ms
If it pumps or flutters, slow the release.

### les_detector — What the band listens to
The detector decides when a dynamic band moves. By default it listens
to the same area the band changes. With Custom, you can make it listen
somewhere else: for example, cut at 7 kHz but only listen to 5–10 kHz,
so a loud low note doesn't set it off. Press Listen to hear exactly
what the detector hears. If Listen sounds like the problem, you've
aimed it right.

### les_sidechain — One track controls another
With the detector set to External, a band reacts to a different
track: the one you pick in Logic's Side Chain menu. Two classics: the
kick dips the bass's low end on each hit, so they stop fighting; the
vocal dips the guitars' 2–3 kHz while the singer sings, so the words
read clearly. Small ranges (−2 to −5 dB) sound natural; big ones
sound like ducking.

### les_lowcut — Low cuts: when and how much
Many tracks carry rumble below the music: handling noise, room noise,
low hum. A low cut clears it and frees headroom. Set it just below the
lowest note you want: vocals around 80–120 Hz, guitars around 80–120
Hz, keys depend on the part. Bass and kick need their lows, so cut
only below the lowest note. On the master, stay at 20–30 Hz. Always
check in the full mix: a track can sound thin alone and right in
context.

### les_louder — Louder isn't better (but it sounds like it)
Our ears hear a slightly louder version as better, every time. So any
boost seems like an improvement, even when it isn't. Auto-gain keeps
the level steady so you hear only the tone change. Keep it on while
deciding, and check the loudness difference in CHECK. A/B lets you
compare two whole setups.

### les_midside — Mid and Side
Mid is what's in the center: usually vocal, kick, bass, snare. Side
is what's different between left and right: width, room, wide
guitars, reverb. EQing them separately lets you, for example, cut low
end only from the sides so bass stays centered and solid, or add air
only to the sides for width without making the vocal sharper.

### les_phase_modes — Zero Latency or Linear Phase?
Every normal EQ slightly shifts the timing of the frequencies it
changes. Usually you can't hear it. Zero Latency is the normal kind:
no delay, punchy, right for recording and most mixing. Linear Phase
keeps all frequencies in time, but delays the audio and can add
pre-ringing: a soft swell just before sharp hits. It's best for
mastering and for big moves on full mixes or buses.

### les_master — EQ on the master
Mastering EQ is about balance, not fixing single instruments. Moves
are small (0.5–2 dB) and broad. If you need big moves, go back to the
mix. Put LENS before the limiter. Compare to a song you love in the
same style at matched loudness. Most streaming services turn loud
songs down to around −14 to −16 LUFS, so crushing a master louder
won't make it sound louder there. Check on several systems before you
call it done.

### les_order — A mixing order that works
1. Set fader levels and panning first, with no EQ.
2. Clean each track: low cuts, obvious mud and ringing.
3. Control: dynamic bands on problems that come and go.
4. Make room: cut the things fighting each other, or side-chain them.
5. Shape: gentle boosts for tone.
6. Check everything in the full mix, at a moderate volume, with
   breaks. Tired ears love treble.

### les_bass — Upright and electric bass
Upright bass booms on certain notes, often from the room or the body
(around 100–200 Hz). A dynamic cut there evens it out without thinning
the instrument. The wood and finger sound lives around 600 Hz–1.5 kHz;
a little boost helps it speak in a mix. Electric bass growl is often
around 700 Hz–1 kHz and clank around 1.5–3 kHz. Notes that jump out are
the classic dynamic-EQ job. Keep the lows: cut only below the lowest
note.

### les_vocals — Gospel vocals and choirs
Gospel singers move from soft to full-voice belting. Harshness often
appears only on the belts (around 2–4 kHz), so a dynamic cut fixes the
loud moments and leaves the soft ones warm. Chest boom (100–250 Hz)
comes and goes with distance from the mic. S's live around 5–9 kHz.
Stacked choirs pile up mud (200–500 Hz) and harshness: low-cut each
voice a little higher than you would a lead, and control the stack
with gentle dynamic bands.

### les_keys — Neo-soul keys
Electric piano barks when played hard (roughly 1–3 kHz). That's part
of the sound, but a dynamic cut keeps hard hits from poking out. Body
lives around 150–300 Hz; mud builds right above it, especially with
bass playing. A gentle shelf above about 4 kHz adds shimmer.

### les_guitars — Folk-gaze and shoegaze guitars
Big guitar walls fill everything. The usual moves: a low cut so
they stay out of the bass, a cut at 200–400 Hz for mud, control fizz
at 4–8 kHz, and a vocal pocket at 2–3 kHz, side-chained to the vocal
so the wall stays big when no one is singing. Acoustic guitars often
boom at 100–200 Hz and get boxy around 300–600 Hz.

### les_reference — Using a reference
A reference is a finished song you love in a similar style. Match its
loudness to yours, then compare the overall balance: is yours darker,
thinner, muddier? EQ Match shows the difference as a curve and
suggests broad bands. Take them as hints, not orders: a different
arrangement should sound different.

### les_ear — Training your ears
The fastest way to get good at EQ is to name frequencies by ear. The
ear trainer boosts or cuts a hidden band on your own music and you
guess where it is. A few minutes a day for a few weeks makes a real
difference.

---

## 7. Ear trainer levels

| Level | Hidden band | Choices | Pass |
|---|---|---|---|
| 1 | Bell +12 dB, Q 1.5 | 125 Hz, 500 Hz, 2 kHz, 8 kHz | 8 of 10 |
| 2 | Bell +9 dB, Q 1.5 | octaves 63 Hz–8 kHz (8 choices) | 8 of 10 |
| 3 | Bell +6 dB, Q 2 | octaves 31 Hz–16 kHz (10 choices) | 8 of 10 |
| 4 | Bell −9 dB, Q 2 | octaves 63 Hz–8 kHz (cuts) | 8 of 10 |
| 5 | Bell +6 dB, Q 4 | third-octaves 63 Hz–10 kHz | within 1/3 octave, 7 of 10 |

- Instructions: "Toggle the hidden band on and off, then click where you think it is."
- Right: "Right: {freq}." Wrong: "It was {freq}. You picked {guess}. Toggle again and listen for the difference."
- Level up: "Level {n} unlocked."

---

## 8. Glossary

| Term | Definition |
|---|---|
| Frequency | How low or high a sound is, in hertz (Hz). 1 kHz = 1,000 Hz. Humans hear about 20 Hz to 20 kHz. |
| Fundamental | The lowest frequency of a note, which sets its pitch. A low E on bass is about 41 Hz. |
| Harmonics | Higher frequencies above the fundamental that give a sound its character. |
| Gain | How much a band turns up (+) or down (−), in decibels. |
| Decibel (dB) | A unit of loudness change. About 1 dB is the smallest change most people notice; 6 dB is a big one. |
| Q | How wide a band is. Low Q is broad, high Q is narrow. |
| Bell | A band shaped like a hill or valley around one frequency. |
| Shelf | Raises or lowers everything above (high shelf) or below (low shelf) a frequency. |
| Low cut / High cut | Removes everything below or above a frequency. Also called high-pass / low-pass. |
| Slope | How steeply a cut falls off, in dB per octave. 12 is gentle, 48 is steep. |
| Octave | A doubling of frequency: 100 Hz to 200 Hz is one octave. |
| Notch | A very narrow, deep cut for one frequency. |
| Tilt | Brightens or darkens the whole sound around one point. |
| Resonance | A frequency that rings louder than its neighbors. |
| Mud | Too much energy in the low mids (about 200–500 Hz), making a mix cloudy. |
| Sibilance | Sharp S, T, and SH sounds in a voice, around 5–9 kHz. |
| Dynamic EQ | An EQ band that only moves when the sound crosses a threshold. |
| Threshold | The level where a dynamic band starts acting. |
| Range | The most a dynamic band can cut (−) or boost (+). |
| Ratio | How quickly a dynamic band reaches its range once past threshold. |
| Knee | How gently the band starts moving near the threshold. |
| Attack | How fast a dynamic band reacts. |
| Release | How fast a dynamic band lets go. |
| Activity | The share of the last 10 seconds a dynamic band spent moving. |
| Detector | The part of a dynamic band that listens and decides when to move. |
| Side-chain | Letting another track control this one. Set it in Logic's Side Chain menu. |
| Lookahead | Lets dynamic bands react a moment early, by delaying the audio slightly. |
| Mid / Side | Mid is the center of a stereo sound; Side is the difference between left and right. |
| Phase | The timing of a wave. EQs shift it slightly near the frequencies they change. |
| Linear phase | An EQ mode that keeps all frequencies in time, at the cost of delay. |
| Pre-ringing | A faint swell before sharp hits, caused by linear-phase EQ. |
| Latency | Delay added by processing. Logic compensates for it during playback. |
| LUFS | A loudness measurement close to how loud people hear a song. |
| Auto-gain | Keeps the output level matched to the input so EQ changes are judged fairly. |
| Delta | Plays only what the EQ is adding or removing. |
| Headroom | The space between your loudest peak and 0 dBFS, where clipping starts. |
| Spectrum analyzer | The moving graph that shows how much energy is at each frequency. |
| Reference | A finished song you compare your mix against. |

---

## 9. Controls (tooltips)

| Control | Tooltip |
|---|---|
| Frequency | "Where this band works. Drag left and right, or type a value." |
| Gain | "How much this band turns up or down. Small moves first." |
| Q | "Width. Low is broad and gentle; high is narrow and surgical." |
| Shape | "Bell, shelf, cut, and more. Right-click a band to change it." |
| Slope | "How steep the cut is. Steeper sounds more obvious." |
| Stereo mode | "Work on both channels, only the center (Mid), only the edges (Side), or one side." |
| Solo | "Hear only what this band touches. Use it to find problems, not to judge them." |
| Make dynamic | "The band moves only when the sound crosses the threshold." |
| Mode | "Above: act when it gets loud. Below: act when it gets quiet." |
| Range | "The most this band will cut (−) or boost (+)." |
| Threshold | "Where the band starts moving. Or press Learn." |
| Learn | "Play the track and press Learn. LENS sets the threshold so the band acts about 20% of the time." |
| Ratio | "How fast the band reaches its range past the threshold." |
| Attack | "How fast the band reacts. Fast for S's and hits; slower for bass." |
| Release | "How fast it lets go. Slower on low frequencies to avoid flutter." |
| Detector source | "Internal: listen to this track. External: listen to the side-chain track." |
| Custom detector | "Make the band listen to a different range than it changes." |
| Listen | "Hear what the detector hears." |
| Activity | "Share of the last 10 seconds this band was moving. 10–40% is a good target." |
| Lookahead | "Lets dynamic bands react a moment early. Adds a little delay." |
| Phase mode | "Zero Latency for recording and mixing. Linear Phase for mastering." |
| Resolution | "Linear Phase accuracy in the lows. Higher adds more delay." |
| Character | "Clean is transparent. Warm adds gentle saturation." |
| Output gain | "Overall level out of LENS." |
| Auto-gain | "Keeps the level steady so you hear tone, not volume. Leave it on while deciding." |
| Delta | "Play only what LENS is adding or removing." |
| Bypass | "Turn LENS off to compare." |
| A/B | "Two complete setups to compare. Copy one to the other to start a variation." |
| Track type | "Changes labels and Coach advice for this kind of track. Never changes the sound." |
| Workflow steps | "Clean, Control, Shape, Check: a dependable order for any track." |
| Peak marker | "A likely resonance. Click for a dynamic cut; Shift-click for a static cut." |
| Loudness | "Short-term loudness in and out. Near 0 difference means a fair comparison." |
| Coach | "Tips, guided recipes, and lessons." |
