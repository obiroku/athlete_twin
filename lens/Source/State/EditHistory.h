#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace lens::state
{
// Undo/redo of band edits (SPEC §3 Output). Each entry stores the full
// band setup before and after an edit. Message thread only.
class EditHistory
{
public:
    explicit EditHistory (int maxEntries = 200) : limit (maxEntries) {}

    // Records an edit. Ignored when before and after are equal.
    void push (const juce::var& before, const juce::var& after, const juce::String& name = {});

    bool canUndo() const noexcept { return index > 0; }
    bool canRedo() const noexcept { return index < (int) entries.size(); }

    // Return the setup to apply, or void if nothing to do.
    juce::var undo();
    juce::var redo();

    juce::String undoName() const;
    juce::String redoName() const;
    void clear();
    int size() const noexcept { return (int) entries.size(); }

private:
    struct Entry
    {
        juce::var before, after;
        juce::String name;
    };
    std::vector<Entry> entries;
    int index = 0;
    int limit;
};
} // namespace lens::state
