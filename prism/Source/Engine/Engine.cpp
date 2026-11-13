#include "Engine.h"
#include "../DSP/Common.h"
#include "../DSP/Halfband.h"
#include "../DSP/Filters.h"

namespace prism
{
using namespace dsp;

namespace
{
constexpr int kMidiSrc[] = { (int) Src::ModWheel, (int) Src::PitchBend, (int) Src::ChanAT, (int) Src::CCA, (int) Src::CCB };
} // namespace

Engine::Engine()
{
    voices.resize (PoolSize);
    for (int i = 0; i < NumFx; ++i) fxEnable[(size_t) i] = fxEnableParam (i);
    for (auto& a : liveNorm) a.store (0.0f);
    for (auto& a : liveValid) a.store (false);
    for (auto& a : liveSrc) a.store (0.0f);
    const auto& d = paramDefs();
    for (int p = 0; p < P::NumParams; ++p) fallbackRaw[(size_t) p] = d[(size_t) p].def;
    routes.build (extras);
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    // warm up statics that would otherwise initialise on the audio thread
    (void) paramDefs(); (void) syncDivNames(); (void) steepCoefs(); (void) relaxedCoefs(); (void) diodeConstants();

    rows.assign ((size_t) (kMaxChunk / kTick + 3) * P::NumParams, 0.0f);
    for (auto& g : gsrc) g.assign ((size_t) kMaxChunk, 0.0f);
    for (auto& b : baseFast) b.assign ((size_t) kMaxChunk, 0.0f);
    mixL.assign ((size_t) kMaxChunk, 0.0f);
    mixR.assign ((size_t) kMaxChunk, 0.0f);
    for (auto& v : voices) v.prepare (sr);
    for (auto& l : gLfo) l.prepare (sr);
    arp.prepare (sr);
    chord.prepare (sr);
    rack.prepare (sr, kMaxChunk);
    master.prepare (sr, kMaxChunk);
    smoothTicks = std::max (1, (int) std::lround (0.02 * sr / kTick));
    midiSmCoef = std::exp (-1.0f / (0.004f * (float) sr));
    firstTick = true;
    absSample = 0;
    gLastNorm.fill (-1.0f);
    for (int s : kMidiSrc) { midiTarget[s] = midiSm[s] = 0.0f; }
    midiTarget[(int) Src::PitchBend] = midiSm[(int) Src::PitchBend] = 0.0f;
    monoCount = 0; monoVoice = -1; sustain = false;
    sustained.fill (false); keyDown.fill (false);
    lastNotePitch = -1.0f;
    resetSeeds();
    resetTails();
}

void Engine::setExtras (const PatchExtras& x) noexcept
{
    extras = x;
    for (auto& s : extras.mseg.pts) (void) s;
    extras.mseg.sanitize();
    for (auto& s : extras.lfoCustom) s.sanitize();
    for (int o = 0; o < 2; ++o)
    {
        auto sh = extras.remap[(size_t) o];
        sh.sanitize();
        // a default (2-point identity) remap shape keeps the built-in S-curve
        const bool identity = sh.count == 2 && sh.pts[0].y == 0.0f && sh.pts[1].y == 1.0f && sh.pts[1].curve == 0.0f;
        if (! identity)
        {
            for (int i = 0; i <= 32; ++i) remap[o].y[(size_t) i] = sh.eval ((float) i / 32.0f);
            remap[o].computeSlope();
        }
        else remap[o] = RemapCurve();
    }
    routes.build (extras);
    rack.setOrder (extras.fxOrder);
}

void Engine::resetSeeds() noexcept
{
    noteRng.reseed (0x9E57);
    alternateState = 1.0f;
    for (int l = 0; l < 4; ++l) { gLfo[l].reseed (mixSeed (0x6F0, (uint64_t) l)); gLfo[l].trigger(); }
    arp.restart();
    chord.reset();
}

void Engine::resetTails() noexcept
{
    rack.reset();
    master.reset();
}

void Engine::panic() noexcept
{
    for (auto& v : voices) v.kill();
    monoCount = 0; monoVoice = -1; sustained.fill (false); keyDown.fill (false);
    arp.reset();
    chord.reset();
}

void Engine::releaseAll() noexcept
{
    for (auto& v : voices) v.release (0.5f);
    monoCount = 0; sustained.fill (false); keyDown.fill (false);
}

// ======================================================================= parameters

void Engine::computeTick (float* row) noexcept
{
    const auto& defs = paramDefs();
    for (int p = 0; p < P::NumParams; ++p)
    {
        const float raw = rawParams != nullptr ? rawParams[p]->load (std::memory_order_relaxed) : fallbackRaw[(size_t) p];
        if (raw != lastRaw[(size_t) p] || firstTick)
        {
            lastRaw[(size_t) p] = raw;
            const auto& d = defs[(size_t) p];
            const float t = d.toNorm (d.snap (raw));
            if (firstTick || d.type != PType::Float)
            {
                cur[(size_t) p] = target[(size_t) p] = t;
                stepsLeft[(size_t) p] = 0;
            }
            else if (t != target[(size_t) p])
            {
                target[(size_t) p] = t;
                stepsLeft[(size_t) p] = smoothTicks;
                delta[(size_t) p] = (t - cur[(size_t) p]) / (float) smoothTicks;
            }
        }
        if (stepsLeft[(size_t) p] > 0)
        {
            cur[(size_t) p] += delta[(size_t) p];
            if (--stepsLeft[(size_t) p] == 0) cur[(size_t) p] = target[(size_t) p];
        }
        row[p] = cur[(size_t) p];
    }
    firstTick = false;
}

// ======================================================================= process

void Engine::process (float* L, float* R, int n, const juce::MidiBuffer& midi, const Transport& tr) noexcept
{
    // Convert MIDI once for the whole host block
    auto& be = blockEv;
    be.count = 0;
    const bool mpe = paramDefs()[P::mpe_on].fromNorm (lastRow[P::mpe_on]) >= 0.5f;
    for (const auto meta : midi)
    {
        if (be.count >= (int) be.ev.size()) break;
        const auto m = meta.getMessage();
        Event e;
        e.pos = std::max (0, std::min (n - 1, meta.samplePosition));
        e.channel = (uint8_t) std::max (1, m.getChannel());
        if (m.isNoteOn()) { e.type = Event::NoteOn; e.num = (int16_t) m.getNoteNumber(); e.value = m.getFloatVelocity(); }
        else if (m.isNoteOff()) { e.type = Event::NoteOff; e.num = (int16_t) m.getNoteNumber(); e.value = m.getFloatVelocity(); }
        else if (m.isPitchWheel()) { e.type = Event::PitchBend; e.value = (float) (m.getPitchWheelValue() - 8192) / 8192.0f; }
        else if (m.isChannelPressure()) { e.type = Event::ChanPressure; e.value = (float) m.getChannelPressureValue() / 127.0f; }
        else if (m.isAftertouch()) { e.type = Event::PolyPressure; e.num = (int16_t) m.getNoteNumber(); e.value = (float) m.getAfterTouchValue() / 127.0f; }
        else if (m.isAllNotesOff()) { e.type = Event::AllNotesOff; }
        else if (m.isAllSoundOff()) { e.type = Event::AllSoundOff; }
        else if (m.isController()) { e.type = Event::CC; e.num = (int16_t) m.getControllerNumber(); e.value = (float) m.getControllerValue() / 127.0f; }
        else continue;
        be.ev[(size_t) be.count++] = e;
    }
    (void) mpe;

    // Transport start: reset seeds so the same MIDI renders identically
    if (tr.playing && ! wasPlaying) resetSeeds();
    wasPlaying = tr.playing;

    int done = 0, ei = 0;
    while (done < n)
    {
        const int len = std::min (n - done, kMaxChunk);
        rawEv.clear();
        while (ei < be.count && be.ev[(size_t) ei].pos < done + len)
        {
            Event e = be.ev[(size_t) ei++];
            e.pos -= done;
            rawEv.add (e);
        }
        const double ppq = tr.ppq + (double) done / sr * tr.bpm / 60.0;
        processChunk (L + done, R + done, len, tr, ppq);
        done += len;
    }
    updateTelemetry();
}

void Engine::processChunk (float* L, float* R, int n, const Transport& tr, double ppqAtStart) noexcept
{
    const auto& defs = paramDefs();
    const int tickPhase = (int) (absSample % kTick);
    const size_t NP = P::NumParams;

    // ---- 1. control-rate rows for this chunk
    float* r0 = rows.data();
    float* r1 = rows.data() + NP;
    if (tickPhase == 0)
    {
        std::copy (lastRow.begin(), lastRow.end(), r0);
        computeTick (r1);
        if (absSample == 0 || firstTick) std::copy (r1, r1 + NP, r0);
    }
    else
    {
        std::copy (prevRow.begin(), prevRow.end(), r0);
        std::copy (lastRow.begin(), lastRow.end(), r1);
    }
    int lastIdx = 1;
    for (int t = 1;; ++t)
    {
        const int p = t * kTick - tickPhase;
        if (p >= n) break;
        computeTick (rows.data() + (size_t) (t + 1) * NP);
        lastIdx = t + 1;
    }
    std::copy (rows.data() + (size_t) lastIdx * NP, rows.data() + (size_t) (lastIdx + 1) * NP, lastRow.begin());
    std::copy (rows.data() + (size_t) (lastIdx - 1) * NP, rows.data() + (size_t) lastIdx * NP, prevRow.begin());

    const float* row1 = r1;
    const bool mpe = realOf (row1, P::mpe_on) >= 0.5f;
    const double bpm = tr.bpm > 0.0 ? tr.bpm : 120.0;

    // ---- 2. chord mode + arp
    chord.process (rawEv, chordEv, n, realOf (row1, P::chord (P::chord_on)) >= 0.5f, realOf (row1, P::chord (P::chord_strum)),
                   (int) std::lround (realOf (row1, P::chord (P::chord_dir))), extras.chord);
    ArpSettings as;
    as.on = realOf (row1, P::arp (P::arp_on)) >= 0.5f;
    as.mode = (int) std::lround (realOf (row1, P::arp (P::arp_mode)));
    as.octaves = (int) std::lround (realOf (row1, P::arp (P::arp_octaves)));
    as.div = (int) std::lround (realOf (row1, P::arp (P::arp_div)));
    as.gate = realOf (row1, P::arp (P::arp_gate));
    as.swing = realOf (row1, P::arp (P::arp_swing));
    as.latch = realOf (row1, P::arp (P::arp_latch)) >= 0.5f;
    as.velMode = (int) std::lround (realOf (row1, P::arp (P::arp_vel_mode)));
    as.fixedVel = realOf (row1, P::arp (P::arp_vel_fixed));
    arp.process (chordEv, arpEv, n, as, extras, tr.playing, tr.ppqValid, ppqAtStart, bpm);

    // ---- 3. global per-sample sources
    for (int l = 0; l < 4; ++l)
    {
        const int mode = (int) std::lround (realOf (row1, P::lfo (l, P::l_mode)));
        gLfoFree[l] = mode == (int) LfoMode::Free && (routes.usedSrc & (1u << ((int) Src::Lfo1 + l))) != 0;
        if (gLfoFree[l] && tr.playing && tr.ppqValid && realOf (row1, P::lfo (l, P::l_sync)) >= 0.5f)
            gLfo[l].syncPhase (ppqAtStart / syncDivBeats ((int) std::lround (realOf (row1, P::lfo (l, P::l_div)))));
    }
    {
        int ei = 0;
        const float a = 1.0f - midiSmCoef;
        for (int pos = 0; pos < n; ++pos)
        {
            while (ei < arpEv.count && arpEv[ei].pos <= pos)
            {
                const Event& e = arpEv[ei++];
                const bool master = ! mpe || e.channel == 1;
                if (e.type == Event::CC)
                {
                    if (e.num == 1) midiTarget[(int) Src::ModWheel] = e.value;
                    if (e.num == extras.ccA) midiTarget[(int) Src::CCA] = e.value;
                    if (e.num == extras.ccB) midiTarget[(int) Src::CCB] = e.value;
                }
                else if (e.type == Event::PitchBend && master) midiTarget[(int) Src::PitchBend] = e.value;
                else if (e.type == Event::ChanPressure && master) midiTarget[(int) Src::ChanAT] = e.value;
            }
            for (int s : kMidiSrc)
            {
                midiSm[s] += (midiTarget[s] - midiSm[s]) * a;
                gsrc[s][(size_t) pos] = midiSm[s];
            }
            const int ph = (pos + tickPhase) % kTick;
            const float* rr = rowAt (pos, tickPhase);
            const float* rp = rr - NP;
            const float f = (float) ph / (float) kTick;
            for (int m = 0; m < 4; ++m)
            {
                const int p = P::macro (m);
                gsrc[(int) Src::Macro1 + m][(size_t) pos] = rp[p] + (rr[p] - rp[p]) * f;
            }
            for (int l = 0; l < 4; ++l)
            {
                if (! gLfoFree[l]) continue;
                if (ph == 0 || pos == 0)
                {
                    Lfo::Settings s;
                    const bool sync = defs[(size_t) P::lfo (l, P::l_sync)].fromNorm (rr[P::lfo (l, P::l_sync)]) >= 0.5f;
                    s.rateHz = sync ? (float) (bpm / 60.0 / syncDivBeats ((int) std::lround (defs[(size_t) P::lfo (l, P::l_div)].fromNorm (rr[P::lfo (l, P::l_div)]))))
                                    : defs[(size_t) P::lfo (l, P::l_rate)].fromNorm (rr[P::lfo (l, P::l_rate)]);
                    s.shape = (LfoShape) (int) std::lround (defs[(size_t) P::lfo (l, P::l_shape)].fromNorm (rr[P::lfo (l, P::l_shape)]));
                    s.bipolar = std::lround (rr[P::lfo (l, P::l_polarity)]) >= 1;
                    s.phaseOffset = defs[(size_t) P::lfo (l, P::l_phase)].fromNorm (rr[P::lfo (l, P::l_phase)]);
                    s.smooth = defs[(size_t) P::lfo (l, P::l_smooth)].fromNorm (rr[P::lfo (l, P::l_smooth)]);
                    gLfo[l].set (s);
                }
                gsrc[(int) Src::Lfo1 + l][(size_t) pos] = gLfo[l].next (&extras.lfoCustom[(size_t) l]);
            }
            for (int k = 0; k < NumFast; ++k)
            {
                const int p = paramForFastIndex (k);
                baseFast[k][(size_t) pos] = rp[p] + (rr[p] - rp[p]) * f;
            }
        }
    }

    // ---- 4. render context
    ctx.sr = sr;
    {
        const int q = (int) std::lround (realOf (row1, P::quality));
        ctx.osFactor = q == 0 ? 1 : (q == 1 ? 2 : 4);
    }
    ctx.tickNorm = rows.data();
    ctx.tickPhase = tickPhase;
    for (int k = 0; k < NumFast; ++k) ctx.baseFast[k] = baseFast[k].data();
    for (int s = 0; s < NumSrc; ++s) ctx.globalSrc[s] = nullptr;
    for (int s : kMidiSrc) ctx.globalSrc[s] = gsrc[s].data();
    for (int m = 0; m < 4; ++m) ctx.globalSrc[(int) Src::Macro1 + m] = gsrc[(int) Src::Macro1 + m].data();
    for (int l = 0; l < 4; ++l) if (gLfoFree[l]) ctx.globalSrc[(int) Src::Lfo1 + l] = gsrc[(int) Src::Lfo1 + l].data();
    ctx.extras = &extras;
    ctx.routes = &routes;
    ctx.table[0] = tables[0];
    ctx.table[1] = tables[1];
    ctx.remap[0] = &remap[0];
    ctx.remap[1] = &remap[1];
    ctx.bpm = bpm;
    ctx.pbRange = realOf (row1, P::pb_range);

    // ---- 5. voices, event by event
    std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
    std::fill (mixR.begin(), mixR.begin() + n, 0.0f);
    {
        int pos = 0, ei = 0;
        const float mpeRange = realOf (row1, P::mpe_pb_range);
        for (;;)
        {
            const int next = ei < arpEv.count ? arpEv[ei].pos : n;
            renderVoices (pos, next);
            pos = next;
            if (ei >= arpEv.count) break;
            const Event& e = arpEv[ei++];
            switch (e.type)
            {
                case Event::NoteOn:
                    if (e.value <= 0.0f) handleNoteOff (pos, e.num, 0.5f, e.channel);
                    else handleNoteOn (pos, e.num, e.value, e.channel);
                    break;
                case Event::NoteOff: handleNoteOff (pos, e.num, e.value, e.channel); break;
                case Event::CC:
                    if (e.num == 64)
                    {
                        const bool down = e.value >= 0.5f;
                        if (sustain && ! down)
                            for (int k = 0; k < 128 * 16; ++k)
                                if (sustained[(size_t) k]) { sustained[(size_t) k] = false; handleNoteOff (pos, k % 128, 0.5f, k / 128 + 1); }
                        sustain = down;
                    }
                    else if (e.num == 74 && mpe && e.channel != 1)
                    { for (auto& v : voices) if (v.isActive() && v.getChannel() == e.channel) v.setSlide (e.value); }
                    else if (e.num == 74 && ! mpe)
                    { for (auto& v : voices) if (v.isActive()) v.setSlide (e.value); }
                    break;
                case Event::PitchBend:
                    if (mpe && e.channel != 1)
                        for (auto& v : voices) if (v.isActive() && v.getChannel() == e.channel) v.setNoteBend (e.value * mpeRange);
                    break;
                case Event::ChanPressure:
                    if (mpe && e.channel != 1)
                        for (auto& v : voices) if (v.isActive() && v.getChannel() == e.channel) v.setPolyPressure (e.value);
                    break;
                case Event::PolyPressure:
                    for (auto& v : voices) if (v.isActive() && v.getNote() == e.num) v.setPolyPressure (e.value);
                    break;
                case Event::AllNotesOff: releaseAll(); break;
                case Event::AllSoundOff: panic(); break;
            }
        }
    }

    // newest voice snapshot (per-voice sources drive FX modulation)
    {
        int newest = -1; uint64_t best = 0;
        for (int i = 0; i < PoolSize; ++i)
            if (voices[(size_t) i].isActive() && voices[(size_t) i].getOrder() >= best) { best = voices[(size_t) i].getOrder(); newest = i; }
        if (newest >= 0) std::copy (voices[(size_t) newest].sourceSnapshot(), voices[(size_t) newest].sourceSnapshot() + NumSrc, newestSnap);
    }

    // ---- 6. FX + master in tick-aligned pieces
    {
        FxContext fc;
        fc.sr = sr; fc.bpm = bpm;
        int pos = 0;
        while (pos < n)
        {
            const int ph = (pos + tickPhase) % kTick;
            const int end = std::min (n, pos + (kTick - ph));
            const float* rr = rowAt (pos, tickPhase);
            for (int i = 0; i < routes.nGlobal; ++i) gNorm[(size_t) routes.global[i].dest] = rr[routes.global[i].dest];
            for (int i = 0; i < routes.nGlobal; ++i)
            {
                const auto& r = routes.global[i];
                float v = ctx.globalSrc[r.src] != nullptr ? ctx.globalSrc[r.src][pos] : newestSnap[r.src];
                bool nb = srcIsBipolar (r.src);
                if (r.src >= (int) Src::Lfo1 && r.src <= (int) Src::Lfo4) nb = std::lround (rr[P::lfo (r.src - (int) Src::Lfo1, P::l_polarity)]) >= 1;
                if (r.polarity == 1 && ! nb) v = 2.0f * v - 1.0f; else if (r.polarity == 0 && nb) v = 0.5f * (v + 1.0f);
                if (r.curve != 0.0f) { const float e = std::pow (4.0f, r.curve); v = v < 0 ? -std::pow (-v, e) : std::pow (v, e); }
                if (r.aux >= 0) v *= ctx.globalSrc[r.aux] != nullptr ? ctx.globalSrc[r.aux][pos] : newestSnap[r.aux];
                gNorm[(size_t) r.dest] += (rr[P::modDepth (r.slot)] * 2.0f - 1.0f) * v;
            }
            for (int p = 0; p < P::NumParams; ++p)
            {
                if (defs[(size_t) p].scope != Scope::Global) continue;
                bool routed = false;
                for (int i = 0; i < routes.nGlobal; ++i) if (routes.global[i].dest == p) { routed = true; break; }
                const float nv = routed ? clampf (gNorm[(size_t) p], 0.0f, 1.0f) : rr[p];
                if (nv != gLastNorm[(size_t) p])
                {
                    gLastNorm[(size_t) p] = nv;
                    gReal[(size_t) p] = defs[(size_t) p].fromNorm (nv);
                    if (routed) { liveNorm[p].store (nv, std::memory_order_relaxed); }
                }
            }
            fc.modWheel = gsrc[(int) Src::ModWheel][(size_t) pos];
            rack.update (gReal.data(), fc, fxEnable.data());
            Master::Settings ms;
            ms.volDb = gReal[P::master_vol];
            ms.width = gReal[P::master_width];
            ms.bassMonoHz = gReal[P::bass_mono] >= 60.0f ? gReal[P::bass_mono] : 0.0f;
            ms.softclip = gReal[P::softclip] >= 0.5f;
            master.set (ms);
            rack.process (mixL.data() + pos, mixR.data() + pos, end - pos);
            master.process (mixL.data() + pos, mixR.data() + pos, end - pos);
            preLimiterPeak.store (std::max (preLimiterPeak.load (std::memory_order_relaxed), master.lastPreLimiterPeak), std::memory_order_relaxed);
            meterL.store (std::max (meterL.load (std::memory_order_relaxed), master.peakL), std::memory_order_relaxed);
            meterR.store (std::max (meterR.load (std::memory_order_relaxed), master.peakR), std::memory_order_relaxed);
            pos = end;
        }
    }
    std::copy (mixL.begin(), mixL.begin() + n, L);
    std::copy (mixR.begin(), mixR.begin() + n, R);
    absSample += n;
}

// ======================================================================= voices

void Engine::renderVoices (int a, int b) noexcept
{
    if (a >= b) return;
    for (auto& v : voices)
        if (v.getState() != Voice::State::Free) v.render (ctx, a, b, mixL.data(), mixR.data());
}

int Engine::activeCount() const noexcept
{
    int c = 0;
    for (auto& v : voices) if (v.isActive()) ++c;
    return c;
}

int Engine::allocVoice() noexcept
{
    for (int i = 0; i < PoolSize; ++i) if (voices[(size_t) i].getState() == Voice::State::Free) return i;
    // every slot busy (32 active + 4 fading): drop the oldest fading voice
    int best = -1; uint64_t oldest = ~0ull;
    for (int i = 0; i < PoolSize; ++i)
        if (voices[(size_t) i].getState() == Voice::State::Fading && voices[(size_t) i].getOrder() < oldest)
        { oldest = voices[(size_t) i].getOrder(); best = i; }
    if (best < 0) best = 0;
    voices[(size_t) best].kill();
    return best;
}

void Engine::startVoice (int vi, int pos, int note, float vel, int ch, bool glide, float glideFrom) noexcept
{
    alternateState = -alternateState;
    voices[(size_t) vi].start (ctx, pos, note, vel, ch, ++orderCounter, noteRng, alternateState, glideFrom, glide);
}

int Engine::monoSelect() const noexcept
{
    if (monoCount == 0) return -1;
    const int pr = (int) std::lround (realOf (lastRow.data(), P::mono_priority));
    int best = monoCount - 1;
    for (int i = 0; i < monoCount; ++i)
    {
        if (pr == (int) Priority::Low && monoStack[(size_t) i].note < monoStack[(size_t) best].note) best = i;
        if (pr == (int) Priority::High && monoStack[(size_t) i].note > monoStack[(size_t) best].note) best = i;
    }
    return best;
}

void Engine::handleNoteOn (int pos, int note, float vel, int ch) noexcept
{
    const float* row = rowAt (pos, ctx.tickPhase);
    const float curve = realOf (row, P::vel_curve);
    vel = std::pow (clampf (vel, 0.0f, 1.0f), std::pow (2.0f, -2.0f * curve));
    const int mode = (int) std::lround (realOf (row, P::voice_mode));
    const float glideTime = realOf (row, P::glide_time);
    const bool legatoOnly = realOf (row, P::glide_legato) >= 0.5f;
    if (note >= 0 && note < 128)
    {
        keyDown[(size_t) note] = true;
        heldMask[note / 64].fetch_or (1ull << (note % 64), std::memory_order_relaxed);
        sustained[(size_t) ((ch - 1) * 128 + note)] = false;
    }

    if (mode == (int) VoiceMode::Poly)
    {
        const bool anyHeld = [&] { for (auto& v : voices) if (v.isHeldByKey()) return true; return false; }();
        // 1. a repeated key reuses its own voice
        for (auto& v : voices)
            if (v.isActive() && v.getNote() == note && v.getChannel() == ch)
            {
                v.retrigger (vel, ++orderCounter);
                lastNotePitch = (float) note;
                return;
            }
        const int poly = (int) std::lround (realOf (row, P::poly));
        bool stole = false;
        if (activeCount() >= poly)
        {
            // 2. quietest releasing voice, 3. quietest overall, 4. oldest
            int victim = -1;
            float q = 1e9f;
            for (int i = 0; i < PoolSize; ++i)
            {
                auto& v = voices[(size_t) i];
                if (! v.isReleasing()) continue;
                if (v.loudness() < q || (v.loudness() == q && v.getOrder() < voices[(size_t) victim].getOrder())) { q = v.loudness(); victim = i; }
            }
            if (victim < 0)
                for (int i = 0; i < PoolSize; ++i)
                {
                    auto& v = voices[(size_t) i];
                    if (! v.isActive()) continue;
                    if (victim < 0 || v.loudness() < q - 1e-6f || (std::abs (v.loudness() - q) <= 1e-6f && v.getOrder() < voices[(size_t) victim].getOrder()))
                    { q = v.loudness(); victim = i; }
                }
            if (victim >= 0) { voices[(size_t) victim].beginStealFade(); stole = true; }
        }
        const int vi = allocVoice();
        const bool glide = glideTime > 0.0f && lastNotePitch >= 0.0f && (! legatoOnly || anyHeld);
        startVoice (vi, pos, note, vel, ch, glide, lastNotePitch);
        if (stole) voices[(size_t) vi].delayStart();
        lastNotePitch = (float) note;
        return;
    }

    // ---- mono / legato
    const bool hadKeys = monoCount > 0;
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[(size_t) i].note == note)
        {
            for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1];
            --monoCount;
            break;
        }
    if (monoCount < 128) monoStack[(size_t) monoCount++] = { note, vel, (uint8_t) ch };
    const int sel = monoSelect();
    const int selNote = monoStack[(size_t) sel].note;
    const bool glide = glideTime > 0.0f && (! legatoOnly || hadKeys);
    Voice* mv = monoVoice >= 0 ? &voices[(size_t) monoVoice] : nullptr;
    if (mv != nullptr && mv->isActive())
    {
        if (mode == (int) VoiceMode::Legato && hadKeys && ! mv->isReleasing())
        {
            if (mv->getNote() != selNote) mv->legatoTo (selNote, mv->currentPitch(), glide);
        }
        else
        {
            mv->legatoTo (selNote, mv->currentPitch(), glide && lastNotePitch >= 0.0f);
            mv->retrigger (vel, ++orderCounter);
        }
    }
    else
    {
        const int vi = allocVoice();
        monoVoice = vi;
        startVoice (vi, pos, selNote, vel, ch, glide && lastNotePitch >= 0.0f, lastNotePitch);
    }
    lastNotePitch = (float) selNote;
}

void Engine::handleNoteOff (int pos, int note, float relVel, int ch) noexcept
{
    if (note < 0 || note > 127) return;
    keyDown[(size_t) note] = false;
    heldMask[note / 64].fetch_and (~(1ull << (note % 64)), std::memory_order_relaxed);
    if (sustain)
    {
        sustained[(size_t) ((ch - 1) * 128 + note)] = true;
        // mono stack still drops the key so that legato returns work while the pedal is down
    }
    const float* row = rowAt (pos, ctx.tickPhase);
    const int mode = (int) std::lround (realOf (row, P::voice_mode));
    if (mode == (int) VoiceMode::Poly)
    {
        if (sustain) return;
        for (auto& v : voices)
            if (v.isHeldByKey() && v.getNote() == note && v.getChannel() == ch) v.release (relVel);
        return;
    }
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[(size_t) i].note == note)
        {
            for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1];
            --monoCount;
            break;
        }
    Voice* mv = monoVoice >= 0 ? &voices[(size_t) monoVoice] : nullptr;
    if (mv == nullptr || ! mv->isActive()) return;
    if (monoCount > 0)
    {
        const int sel = monoSelect();
        const int selNote = monoStack[(size_t) sel].note;
        if (selNote != mv->getNote())
        {
            const float glideTime = realOf (row, P::glide_time);
            mv->legatoTo (selNote, mv->currentPitch(), glideTime > 0.0f);
            if (mode == (int) VoiceMode::Mono) mv->retrigger (monoStack[(size_t) sel].vel, ++orderCounter);
            lastNotePitch = (float) selNote;
        }
    }
    else if (! sustain && mv->getNote() == note)
        mv->release (relVel);
    else if (! sustain)
        mv->release (relVel);
}

void Engine::updateTelemetry() noexcept
{
    int count = 0, newest = -1;
    uint64_t best = 0;
    for (int i = 0; i < PoolSize; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (! v.isActive()) continue;
        ++count;
        if (v.getOrder() >= best) { best = v.getOrder(); newest = i; }
    }
    voiceCount.store (count, std::memory_order_relaxed);
    if (newest >= 0)
    {
        const auto& v = voices[(size_t) newest];
        playhead[0].store (v.oscPosition (0), std::memory_order_relaxed);
        playhead[1].store (v.oscPosition (1), std::memory_order_relaxed);
        for (int s = 0; s < NumSrc; ++s) liveSrc[s].store (v.sourceSnapshot()[s], std::memory_order_relaxed);
        auto mark = [&] (const RoutingRef& r) {
            liveNorm[r.dest].store (v.modulatedNorm (r.dest), std::memory_order_relaxed);
            liveValid[r.dest].store (true, std::memory_order_relaxed);
        };
        for (int i = 0; i < routes.nSlow; ++i) mark (routes.slow[i]);
        for (int i = 0; i < routes.nFast; ++i) mark (routes.fast[i]);
        for (int i = 0; i < routes.nDepth; ++i) mark (routes.depth[i]);
    }
    else
    {
        for (int s = 0; s < NumSrc; ++s)
            if (s >= (int) Src::Macro1 && s <= (int) Src::Macro4) liveSrc[s].store (gsrc[s][0], std::memory_order_relaxed);
    }
    for (int i = 0; i < routes.nGlobal; ++i) liveValid[routes.global[i].dest].store (true, std::memory_order_relaxed);
    tailSecs.store (rack.tailSeconds(), std::memory_order_relaxed);
    modWheelValue.store (midiSm[(int) Src::ModWheel], std::memory_order_relaxed);
}
} // namespace prism
