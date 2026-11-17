#pragma once

#include "EditorContext.h"
#include "Theme.h"

#include "Match/EqMatch.h"

namespace lens::gui
{
class MainView;

// Reference and EQ Match overlay (SPEC §9). Suggestions appear as ghost
// bands; nothing changes the EQ until Accept is clicked.
class MatchPanel final : public juce::Component,
                         private juce::Timer
{
public:
    MatchPanel (EditorContext&, MainView&);
    ~MatchPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void open();
    void close();

private:
    void timerCallback() override;
    void refreshReferences();
    void loadReferenceFile();
    void runSuggest();
    void rebuildRows();
    void updateGhosts();

    EditorContext& ctx;
    MainView& main;
    std::vector<match::Spectrum> references;
    match::Spectrum mine;
    match::Difference diff;
    std::vector<match::Suggestion> suggestions;

    juce::TextButton loadBtn { "Load reference..." }, captureBtn { "Start capture" }, suggestBtn { "Suggest" }, closeBtn { "Close" };
    juce::ComboBox referenceBox, sourceBox;
    juce::Label status;
    struct Row
    {
        std::unique_ptr<juce::Label> text;
        std::unique_ptr<juce::TextButton> accept, discard;
    };
    std::vector<Row> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> graphArea;
};
} // namespace lens::gui
