#include "EditHistory.h"

namespace lens::state
{
void EditHistory::push (const juce::var& before, const juce::var& after, const juce::String& name)
{
    if (juce::JSON::toString (before, true) == juce::JSON::toString (after, true))
        return;
    entries.erase (entries.begin() + index, entries.end());
    entries.push_back ({ before, after, name });
    if ((int) entries.size() > limit)
        entries.erase (entries.begin());
    index = (int) entries.size();
}

juce::var EditHistory::undo()
{
    if (! canUndo())
        return {};
    return entries[(size_t) --index].before;
}

juce::var EditHistory::redo()
{
    if (! canRedo())
        return {};
    return entries[(size_t) index++].after;
}

juce::String EditHistory::undoName() const { return canUndo() ? entries[(size_t) index - 1].name : juce::String(); }
juce::String EditHistory::redoName() const { return canRedo() ? entries[(size_t) index].name : juce::String(); }

void EditHistory::clear()
{
    entries.clear();
    index = 0;
}
} // namespace lens::state
