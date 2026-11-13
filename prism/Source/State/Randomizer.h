#pragma once
#include "PresetData.h"
#include "../DSP/Random.h"

namespace prism
{
struct RandomLocks { bool osc = false, filter = false, env = false, mod = false, fx = false, arp = true; };

// Musical randomisation with section locks (SPEC §9). Deterministic for a seed.
PresetData randomizePreset (const PresetData& current, const RandomLocks& locks, uint64_t seed);
// Small random variation of the current patch (amount 0..1)
PresetData mutatePreset (const PresetData& current, float amount, const RandomLocks& locks, uint64_t seed);
// Enforces the musical constraints in place (used by both)
void applyRandomConstraints (PresetData& p);
} // namespace prism
