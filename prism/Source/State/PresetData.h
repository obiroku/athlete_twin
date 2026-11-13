#pragma once
// Preset / state model (SPEC §5 State, §9 Format). Message-thread only.
#include "../Engine/ModTypes.h"
#include "../Engine/TableManager.h"
#include <juce_core/juce_core.h>
#include <array>
#include <string>
#include <vector>

namespace prism
{
constexpr int kFormatVersion = 1;

struct PresetData
{
    std::string name = "Init", author = "Asher", category = "Init", description;
    std::vector<std::string> tags;
    std::array<std::string, 4> macroNames { "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    std::array<float, P::NumParams> params {}; // real units
    PatchExtras extras;
    TableSource tables[2];
    double guiScale = 1.0; // state only (not written to preset files)

    static PresetData init();
};

juce::var presetToJson (const PresetData& p, bool includeGuiScale);
juce::String presetToString (const PresetData& p, bool includeGuiScale);
// Parses, runs the migration chain and fills defaults for anything missing.
bool presetFromJson (const juce::var& v, PresetData& out, juce::String& error);
bool presetFromString (const juce::String& s, PresetData& out, juce::String& error);

// Migration chain: each step upgrades a JSON object from version N to N+1.
// v1 is the first format, so the chain is currently empty.
int migratePreset (juce::DynamicObject& obj); // returns the resulting version

juce::String encodeFrames (const std::vector<std::vector<float>>& frames);
std::vector<std::vector<float>> decodeFrames (const juce::String& b64, int numFrames);
} // namespace prism
