#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

namespace prism
{
struct Event
{
    enum Type : uint8_t { NoteOn, NoteOff, CC, PitchBend, ChanPressure, PolyPressure, AllNotesOff, AllSoundOff };
    int pos = 0;        // sample position within the current chunk
    Type type = NoteOn;
    uint8_t channel = 1;
    int16_t num = 0;    // note or CC number
    float value = 0;    // velocity 0..1, CC 0..1, bend -1..1, pressure 0..1
};

// Fixed-capacity event list (no allocation on the audio thread)
template <int Cap>
struct EventList
{
    std::array<Event, Cap> ev {};
    int count = 0;
    void clear() noexcept { count = 0; }
    bool add (const Event& e) noexcept { if (count >= Cap) return false; ev[(std::size_t) count++] = e; return true; }
    // stable insertion sort by position (lists are small and mostly sorted)
    void sort() noexcept
    {
        for (int i = 1; i < count; ++i)
        {
            Event x = ev[(std::size_t) i];
            int j = i - 1;
            while (j >= 0 && ev[(std::size_t) j].pos > x.pos) { ev[(std::size_t) j + 1] = ev[(std::size_t) j]; --j; }
            ev[(std::size_t) j + 1] = x;
        }
    }
    const Event& operator[] (int i) const noexcept { return ev[(std::size_t) i]; }
};
} // namespace prism
