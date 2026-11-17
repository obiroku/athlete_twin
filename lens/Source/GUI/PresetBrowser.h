#pragma once

#include "EditorContext.h"
#include "Theme.h"

#include "Presets/PresetManager.h"

namespace lens::gui
{
class MainView;

// Preset browser overlay (SPEC §11): search, category, favourites, notes,
// save and save-as. Also drives prev/next in the top bar.
class PresetBrowser final : public juce::Component,
                            private juce::ListBoxModel
{
public:
    PresetBrowser (EditorContext&, MainView&);
    ~PresetBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void open();
    void close();
    void step (int delta);

private:
    int getNumRows() override { return (int) visible.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { load (row); }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void refilter();
    void load (int row);
    void saveCurrent (bool asNew);

    EditorContext& ctx;
    MainView& main;
    presets::PresetManager manager;
    std::vector<int> visible;

    juce::TextEditor search, saveName;
    juce::ComboBox category;
    juce::ToggleButton favoritesOnly { juce::String::fromUTF8 ("\xe2\x98\x85 Favorites") };
    juce::TextButton loadBtn { "Load" }, saveBtn { "Save" }, saveAsBtn { "Save As" }, closeBtn { "Close" };
    juce::ListBox listBox { "Presets", this };
};
} // namespace lens::gui
