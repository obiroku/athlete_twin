#pragma once
// Factory (BinaryData, read-only) + user presets, favourites and settings.
#include "PresetData.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace prism
{
struct PresetEntry
{
    juce::String name, category, author;
    juce::StringArray tags;
    bool factory = true;
    juce::String resourceName; // BinaryData name for factory presets
    juce::File file;           // user presets
    juce::String key() const { return (factory ? "factory:" : "user:") + name; }
};

class Settings
{
public:
    Settings();
    juce::File file() const;
    void load();
    void save() const;
    juce::StringArray favourites;
    double guiScale = 1.0;
    bool tooltips = true;
    juce::File presetFolder, tableFolder;
};

class PresetLibrary
{
public:
    static const juce::StringArray& categories();
    static juce::File defaultUserPresetFolder();
    static juce::File defaultUserTableFolder();

    PresetLibrary();
    void rescan();
    const std::vector<PresetEntry>& entries() const { return list; }
    int indexOf (const juce::String& name) const;
    bool load (int index, PresetData& out, juce::String& err) const;
    bool saveUser (const PresetData& p, juce::String& err); // ~/Music/PRISM/Presets/<category>/<name>.prismpreset
    Settings settings;

    // Factory presets as raw strings (tests use this)
    static std::vector<std::pair<juce::String, juce::String>> factoryPresetSources();

private:
    std::vector<PresetEntry> list;
};
} // namespace prism
