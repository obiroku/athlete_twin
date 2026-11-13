#include "Arp.h"
#include "../Params/Params.h"
#include <cmath>
#include <algorithm>

namespace prism
{
void Arp::reset() noexcept
{
    numHeld = 0;
    physicalDown = 0;
    for (auto& o : offs) o.active = false;
    restart();
}

void Arp::restart() noexcept
{
    step = 0;
    lastStepEmitted = -1;
    freeBeat = 0.0;
    freeRunning = false;
    upDownRising = true;
    rng.reseed (0xA4B);
}

void Arp::addHeld (int note, float vel, uint8_t ch) noexcept
{
    for (int i = 0; i < numHeld; ++i) if (held[i].note == note) { held[i].vel = vel; return; }
    if (numHeld < 64) held[numHeld++] = { note, vel, ch };
}

void Arp::removeHeld (int note) noexcept
{
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note)
        {
            for (int j = i; j < numHeld - 1; ++j) held[j] = held[j + 1];
            --numHeld;
            return;
        }
}

void Arp::killAll (ChunkEvents& out, int pos) noexcept
{
    for (auto& o : offs)
        if (o.active)
        {
            Event e; e.pos = pos; e.type = Event::NoteOff; e.num = (int16_t) o.note; e.channel = o.ch; e.value = 0.5f;
            out.add (e);
            o.active = false;
        }
}

void Arp::process (const ChunkEvents& in, ChunkEvents& out, int n, const ArpSettings& s, const PatchExtras& x,
                   bool playing, bool ppqValid, double ppqAtStart, double bpm) noexcept
{
    out.clear();
    if (! s.on)
    {
        if (wasOn) { killAll (out, 0); numHeld = 0; physicalDown = 0; }
        wasOn = false;
        for (int i = 0; i < in.count; ++i) out.add (in[i]);
        return;
    }
    if (! wasOn) { restart(); numHeld = 0; physicalDown = 0; }
    wasOn = true;

    const double spb = sr * 60.0 / std::max (1.0, bpm); // samples per beat
    const double stepBeats = syncDivBeats (s.div) / (double) std::max (0.25f, s.speed);
    const bool hostClock = playing && ppqValid;
    if (! hostClock && ! freeRunning && numHeld == 0)
    {
        // start the internal clock so that step 0 lands on the first key press
        for (int i = 0; i < in.count; ++i)
            if (in[i].type == Event::NoteOn)
            {
                freeRunning = true;
                freeBeat = -(double) in[i].pos / spb;
                step = 0; lastStepEmitted = -1;
                break;
            }
    }

    const double beat0 = hostClock ? ppqAtStart : freeBeat;
    const double beat1 = beat0 + (double) n / spb;

    // pass non-note events through; collect note events
    int ei = 0;
    auto flushEventsUpTo = [&] (int pos) {
        while (ei < in.count && in[ei].pos <= pos)
        {
            const Event& e = in[ei++];
            if (e.type == Event::NoteOn)
            {
                if (physicalDown == 0 && s.latch) numHeld = 0; // new chord replaces the latched one
                ++physicalDown;
                addHeld (e.num, e.value, e.channel);
            }
            else if (e.type == Event::NoteOff)
            {
                physicalDown = std::max (0, physicalDown - 1);
                if (! s.latch) removeHeld (e.num);
            }
            else out.add (e);
        }
    };

    // pending note-offs inside this chunk
    auto emitOffs = [&] (double fromBeat, double toBeat) {
        for (auto& o : offs)
        {
            if (! o.active || o.beatOff >= toBeat) continue;
            const int pos = std::max (0, std::min (n - 1, (int) std::ceil ((o.beatOff - fromBeat) * spb)));
            Event e; e.pos = pos; e.type = Event::NoteOff; e.num = (int16_t) o.note; e.channel = o.ch; e.value = 0.5f;
            out.add (e);
            o.active = false;
        }
    };

    // Iterate steps whose start falls in [beat0, beat1)
    const double swingOff = s.swing * 0.5 * stepBeats;
    int64_t kFirst = (int64_t) std::floor (beat0 / stepBeats) - 1;
    if (kFirst < 0) kFirst = 0;
    if (hostClock && lastStepEmitted > kFirst + 2) lastStepEmitted = kFirst - 1; // host jumped backwards (loop)
    flushEventsUpTo (-1);
    for (int64_t k = kFirst;; ++k)
    {
        const double absBeat = (double) k * stepBeats + ((k & 1) ? swingOff : 0.0);
        if (absBeat >= beat1) break;
        if (absBeat < beat0 || k <= lastStepEmitted) continue;
        const int pos = std::max (0, std::min (n - 1, (int) std::ceil ((absBeat - beat0) * spb)));
        emitOffs (beat0, absBeat + 1e-9);
        flushEventsUpTo (pos);
        lastStepEmitted = k;
        if (numHeld == 0) continue;

        // build the pattern
        int notes[256]; float vels[256]; int cnt = 0;
        Held sorted[64];
        for (int i = 0; i < numHeld; ++i) sorted[i] = held[i];
        const bool asPlayed = s.mode == (int) ArpMode::AsPlayed;
        if (! asPlayed) std::stable_sort (sorted, sorted + numHeld, [] (const Held& a, const Held& b) { return a.note < b.note; });
        for (int o = 0; o < s.octaves; ++o)
            for (int i = 0; i < numHeld && cnt < 256; ++i)
            {
                notes[cnt] = std::min (127, sorted[i].note + 12 * o);
                vels[cnt] = sorted[i].vel;
                ++cnt;
            }
        int idxs[16]; int nsel = 0;
        const int64_t st = step++;
        switch ((ArpMode) s.mode)
        {
            case ArpMode::Up: case ArpMode::AsPlayed: idxs[nsel++] = (int) (st % cnt); break;
            case ArpMode::Down: idxs[nsel++] = cnt - 1 - (int) (st % cnt); break;
            case ArpMode::UpDown:
            {
                const int period = std::max (1, 2 * cnt - 2);
                const int p = (int) (st % period);
                idxs[nsel++] = cnt == 1 ? 0 : (p < cnt ? p : period - p);
                break;
            }
            case ArpMode::Random: idxs[nsel++] = (int) (rng.uniform() * (float) cnt) % cnt; break;
            case ArpMode::Chord:
            {
                const int oct = (int) (st % s.octaves);
                for (int i = 0; i < numHeld && nsel < 16; ++i) idxs[nsel++] = std::min (cnt - 1, oct * numHeld + i);
                break;
            }
        }
        const double gateBeats = stepBeats * s.gate;
        for (int j = 0; j < nsel; ++j)
        {
            const int nn = notes[idxs[j]];
            float v = vels[idxs[j]];
            if (s.velMode == (int) ArpVel::Fixed) v = s.fixedVel;
            else if (s.velMode == (int) ArpVel::Accent) v = x.arpAccents[(size_t) (st % 16)];
            // close a still-sounding instance of the same note first (gate > 100%)
            for (auto& o : offs)
                if (o.active && o.note == nn)
                {
                    Event e; e.pos = pos; e.type = Event::NoteOff; e.num = (int16_t) nn; e.channel = o.ch; e.value = 0.5f;
                    out.add (e);
                    o.active = false;
                }
            Event e; e.pos = pos; e.type = Event::NoteOn; e.num = (int16_t) nn; e.channel = 1; e.value = std::max (0.01f, v);
            out.add (e);
            for (auto& o : offs)
                if (! o.active) { o = { absBeat + gateBeats, nn, (uint8_t) 1, true }; break; }
        }
    }
    flushEventsUpTo (n);
    emitOffs (beat0, beat1);
    if (! hostClock) freeBeat = beat1;
    if (numHeld == 0 && ! s.latch && ! hostClock) freeRunning = false;
    out.sort();
}

// =================================================================== chord mode

void ChordMode::process (const ChunkEvents& in, ChunkEvents& out, int n, bool on, float strumMs, int dir,
                         const ChordShapeData& shape) noexcept
{
    out.clear();
    // release pending events that fall due in this chunk
    for (int i = 0; i < numPending;)
    {
        if (pending[i].delay < n)
        {
            Event e = pending[i].e;
            e.pos = (int) std::max<int64_t> (0, pending[i].delay);
            out.add (e);
            pending[i] = pending[--numPending];
        }
        else { pending[i].delay -= n; ++i; }
    }
    if (! on || shape.count <= 0)
    {
        for (int i = 0; i < in.count; ++i) out.add (in[i]);
        out.sort();
        return;
    }
    const int64_t strum = (int64_t) std::llround ((double) strumMs * 0.001 * sr);
    for (int i = 0; i < in.count; ++i)
    {
        const Event& e = in[i];
        if (e.type != Event::NoteOn && e.type != Event::NoteOff) { out.add (e); continue; }
        const int cnt = std::min (8, shape.count);
        bool down = dir == (int) StrumDir::Down || (dir == (int) StrumDir::Alternate && flip);
        if (e.type == Event::NoteOn && dir == (int) StrumDir::Alternate) flip = ! flip;
        for (int k = 0; k < cnt; ++k)
        {
            Event c = e;
            const int nn = e.num + shape.intervals[(size_t) k];
            if (nn < 0 || nn > 127) continue;
            c.num = (int16_t) nn;
            const int order = down ? cnt - 1 - k : k;
            const int64_t d = e.type == Event::NoteOn ? (int64_t) e.pos + strum * order : (int64_t) e.pos;
            if (d < n) { c.pos = (int) d; out.add (c); }
            else if (numPending < 256) { pending[numPending++] = { d - n, c }; }
        }
        // a note-off cancels any not-yet-started strum notes of the chord
        if (e.type == Event::NoteOff)
            for (int p = 0; p < numPending;)
            {
                const auto& pe = pending[p].e;
                bool mine = false;
                for (int k = 0; k < cnt; ++k) if (pe.num == e.num + shape.intervals[(size_t) k] && pe.type == Event::NoteOn) mine = true;
                if (mine) pending[p] = pending[--numPending]; else ++p;
            }
    }
    out.sort();
}
} // namespace prism
