#pragma once
#include "Views.h"

namespace prism::gui
{
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (PrismProcessor& p);
    void setFilter (const juce::String& search, const juce::String& category, bool favouritesOnly);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;
    void refresh();
private:
    PrismProcessor& proc;
    juce::ListBox list { "presets", this };
    std::vector<int> shown;
    juce::String search, category;
    bool favOnly = false;
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override { loadRow (row); }
    void loadRow (int row);
};

class TopBar : public juce::Component, public LiveComponent
{
public:
    TopBar (PrismProcessor& p, PresetBrowser& browser);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refreshLive() override;
    void loadIndex (int index);
private:
    PrismProcessor& proc;
    PresetBrowser& browser;
    juce::TextButton name, prev { "<" }, next { ">" }, fav { "*" }, save { "Save" }, saveAs { "Save As" },
                     ab { "A/B" }, undo { "Undo" }, redo { "Redo" }, dice { "Random" }, locks { "Locks" }, panic { "Panic" };
    juce::TextEditor searchBox;
    juce::ComboBox category;
    ParamCombo quality;
    Knob volume;
    juce::Label cpu, voices;
    RandomLocks lockState;
    float mutateAmount = 0.3f;
    int currentIndex = -1;
    int frame = 0;
    juce::String shownName;
    void openBrowser();
    void doSave (bool as);
};
} // namespace prism::gui
