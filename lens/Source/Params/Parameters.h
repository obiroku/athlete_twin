#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs and layout (SPEC §10). IDs are permanent: never rename or
// remove one; deprecate it instead.
namespace lens::params
{
constexpr int numBands = 24;
constexpr int versionHint = 1;

// Global parameter IDs.
namespace id
{
inline constexpr const char* phaseMode = "phase_mode";
inline constexpr const char* lpResolution = "lp_resolution";
inline constexpr const char* character = "character";
inline constexpr const char* outputGain = "output_gain";
inline constexpr const char* autoGain = "auto_gain";
inline constexpr const char* delta = "delta";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* abSlot = "ab_slot";
inline constexpr const char* lookahead = "lookahead";
} // namespace id

// Per-band ID suffixes ("b07_" + suffix).
namespace band
{
inline constexpr const char* on = "on";
inline constexpr const char* shape = "shape";
inline constexpr const char* freq = "freq";
inline constexpr const char* gain = "gain";
inline constexpr const char* q = "q";
inline constexpr const char* slope = "slope";
inline constexpr const char* stereo = "stereo";
inline constexpr const char* dynOn = "dyn_on";
inline constexpr const char* dynMode = "dyn_mode";
inline constexpr const char* dynRange = "dyn_range";
inline constexpr const char* dynThresh = "dyn_thresh";
inline constexpr const char* dynRatio = "dyn_ratio";
inline constexpr const char* dynAttack = "dyn_attack";
inline constexpr const char* dynRelease = "dyn_release";
inline constexpr const char* detSource = "det_source";
inline constexpr const char* detCustom = "det_custom";
inline constexpr const char* detHp = "det_hp";
inline constexpr const char* detLp = "det_lp";
inline constexpr const char* specOn = "spec_on";
inline constexpr const char* specThresh = "spec_thresh";
inline constexpr const char* specAmount = "spec_amount";
} // namespace band

// Every per-band suffix, in declaration order.
const juce::StringArray& bandSuffixes();

// Every global parameter ID, in declaration order.
const juce::StringArray& globalIds();

// band is 1-based: bandParamId (7, band::freq) == "b07_freq".
juce::String bandParamId (int band, juce::StringRef suffix);

// Default frequency for a slot: 50·2^((NN−1)/3) Hz, capped at 20 kHz.
float defaultBandFrequency (int band);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// The stable token written to JSON for a choice parameter's current value
// (the names in the SPEC §10 tables: "LowShelf", "12", "Zero Latency").
juce::String choiceToken (const juce::AudioParameterChoice&, int index);

// Index of a token in a choice parameter, or -1.
int choiceIndexForToken (const juce::AudioParameterChoice&, const juce::String& token);
} // namespace lens::params
