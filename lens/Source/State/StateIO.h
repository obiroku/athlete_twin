#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// JSON <-> parameters (SPEC §10 State, §11 Format).
namespace lens::state
{
constexpr int currentFormatVersion = 1;

using APVTS = juce::AudioProcessorValueTreeState;

// A preset "bands" array. With onlyEnabled, bands that are off are
// skipped (presets); otherwise all 24 slots are written (plugin state).
juce::var bandsToVar (const APVTS&, bool onlyEnabled);

// Resets all 24 bands to defaults, then applies the array. An entry
// without "on" is treated as on. Unknown keys and bad values are ignored.
void applyBands (APVTS&, const juce::var& bandsArray);

// forPreset leaves out bypass and ab_slot: a preset never changes them.
juce::var globalsToVar (const APVTS&, bool forPreset);
void applyGlobals (APVTS&, const juce::var& globals, bool forPreset);

// Runs the migration chain (v1 → v2 → …) in place until the document is
// at currentFormatVersion. Returns false if root is not a JSON object.
bool migrate (juce::var& root);
} // namespace lens::state
