#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Analyzer.h"
#include "PluginProcessor.h"

namespace lens::gui
{
// Selected bands (0-based slot indices). The first is the "primary" band
// shown in the band panel.
class Selection : public juce::ChangeBroadcaster
{
public:
    const juce::Array<int>& bands() const noexcept { return selected; }
    int primary() const noexcept { return selected.isEmpty() ? -1 : selected.getFirst(); }
    bool contains (int b) const noexcept { return selected.contains (b); }

    void selectOnly (int b)
    {
        juce::Array<int> s;
        if (b >= 0)
            s.add (b);
        set (s);
    }
    void toggle (int b)
    {
        auto s = selected;
        if (s.contains (b))
            s.removeFirstMatchingValue (b);
        else
            s.add (b);
        set (s);
    }
    void clear() { set ({}); }
    void set (const juce::Array<int>& s)
    {
        if (s == selected)
            return;
        selected = s;
        sendSynchronousChangeMessage();
    }

private:
    juce::Array<int> selected;
};

// Shared by every editor component. Owned by the editor.
struct EditorContext
{
    LensProcessor& proc;
    AnalyzerThread& analyzer;
    Selection selection;

    // Set by workflow steps and Coach recipes: control names to outline.
    juce::StringArray highlights;
    std::function<void()> onHighlightsChanged;
    // Glossary jumps and tooltips go through the Coach panel (Phase 5).
    std::function<void (const juce::String&)> showGlossaryTerm;
    // Coach events (completion checks).
    std::function<void (const juce::String& event)> onUiEvent;

    void uiEvent (const juce::String& e)
    {
        if (onUiEvent)
            onUiEvent (e);
    }
};
} // namespace lens::gui
