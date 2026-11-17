#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "Coach/Settings.h"

namespace lens::presets
{
struct PresetEntry
{
    juce::String name, category, note, trackType;
    bool factory = false;
    juce::String resource; // factory: BinaryData name
    juce::File file;       // user preset file
    juce::String key() const { return factory ? "factory:" + name : "user:" + file.getFileName(); }
};

// Factory presets (BinaryData, read-only) and user presets
// (~/Music/LENS/Presets). Message thread.
class PresetManager
{
public:
    explicit PresetManager (coach::Settings&, juce::File userDir = defaultUserDir());

    static juce::File defaultUserDir();
    static juce::StringArray categories();

    void rescan();
    const std::vector<PresetEntry>& all() const noexcept { return entries; }
    juce::String jsonFor (const PresetEntry&) const;

    // Indices into all(), filtered.
    std::vector<int> filtered (const juce::String& search, const juce::String& category, bool favoritesOnly) const;
    int indexOfName (const juce::String& name) const;

    bool isFavorite (const PresetEntry&) const;
    void toggleFavorite (const PresetEntry&);

    // Writes a user preset; returns its index (or -1). Factory presets are
    // never overwritten.
    int saveAs (const juce::String& name, const juce::String& json);
    bool save (int index, const juce::String& json);

private:
    coach::Settings& settings;
    juce::File userDir;
    std::vector<PresetEntry> entries;
};
} // namespace lens::presets
