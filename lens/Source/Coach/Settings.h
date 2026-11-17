#pragma once

#include <juce_core/juce_core.h>

namespace lens::coach
{
// Coach progress and browser favourites (SPEC §7 Storage, §11). Lives in
// settings.json, never in presets or plugin state.
class Settings
{
public:
    explicit Settings (juce::File file = defaultFile());

    static juce::File defaultFile(); // ~/Library/Application Support/LENS/settings.json
    void load();
    bool save() const;
    const juce::File& file() const noexcept { return path; }

    bool firstRunDone = false;
    bool coachVisible = true;
    bool quiet = false;
    juce::StringArray tipsNever, recipesDone, lessonsRead, favorites;

    // Ear trainer.
    int earLevel = 1, earRight = 0, earTotal = 0, earStreak = 0, earBestStreak = 0;

private:
    juce::File path;
};
} // namespace lens::coach
