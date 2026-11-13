#pragma once
#include <cstdint>

namespace prism::dsp
{
// Deterministic, seedable PRNG (SplitMix64 seeding + xoshiro128+ style output).
// Every random source in PRISM uses one of these so renders repeat bit-exactly.
class Rng
{
public:
    explicit Rng (uint64_t seed = 0x5eed) noexcept { reseed (seed); }

    void reseed (uint64_t seed) noexcept
    {
        uint64_t z = seed;
        for (int i = 0; i < 2; ++i)
        {
            z += 0x9E3779B97F4A7C15ull;
            uint64_t x = z;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
            s[i] = x ^ (x >> 31);
        }
        if (s[0] == 0 && s[1] == 0) s[0] = 1;
    }

    uint64_t next() noexcept
    {
        uint64_t s0 = s[0], s1 = s[1];
        const uint64_t r = s0 + s1;
        s1 ^= s0;
        s[0] = ((s0 << 24) | (s0 >> 40)) ^ s1 ^ (s1 << 16);
        s[1] = (s1 << 37) | (s1 >> 27);
        return r;
    }

    // [0,1)
    float uniform() noexcept { return (float) (next() >> 40) * (1.0f / 16777216.0f); }
    // [-1,1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
    double uniformD() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    uint64_t s[2] {};
};

// Stable hash for deriving sub-seeds
inline uint64_t mixSeed (uint64_t a, uint64_t b) noexcept
{
    uint64_t x = a * 0x9E3779B97F4A7C15ull + b + 0x632BE59BD9B4E019ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
} // namespace prism::dsp
