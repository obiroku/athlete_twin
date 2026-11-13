#pragma once
#include "Views.h"

namespace prism::gui
{
class Page : public juce::Component
{
public:
    explicit Page (PrismProcessor& p) : proc (p) {}
    virtual void collectLive (std::vector<LiveComponent*>& out);
protected:
    PrismProcessor& proc;
    std::vector<std::unique_ptr<ParamPanel>> panels;
    std::vector<std::unique_ptr<juce::Component>> extra;
    ParamPanel& panel (const juce::String& title);
    template <typename T, typename... A> T& make (A&&... a)
    {
        auto* c = new T (std::forward<A> (a)...);
        extra.emplace_back (c);
        return *c;
    }
};

class SynthPage : public Page
{
public:
    explicit SynthPage (PrismProcessor& p);
    void resized() override;
private:
    ParamPanel *oscA, *oscB, *sub, *noise, *f1, *f2, *env1, *macros;
    WavetableView *viewA, *viewB;
    juce::ComboBox tableA, tableB;
    juce::TabbedComponent sources { juce::TabbedButtonBar::TabsAtTop };
    juce::Label macroNames[4];
    void fillTables (juce::ComboBox& c, int osc);
};

class ModMatrixPage : public Page
{
public:
    explicit ModMatrixPage (PrismProcessor& p);
    void resized() override;
    void collectLive (std::vector<LiveComponent*>& out) override;
private:
    class Row;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<Row>> rows;
};

class FxPage : public Page
{
public:
    explicit FxPage (PrismProcessor& p);
    void resized() override;
    void collectLive (std::vector<LiveComponent*>& out) override;
    void select (int fx);
private:
    class RackList;
    std::unique_ptr<RackList> rack;
    std::array<std::unique_ptr<ParamPanel>, NumFx> modules;
    ParamPanel* master = nullptr;
    int selected = 8;
};

class ArpVoicingPage : public Page
{
public:
    explicit ArpVoicingPage (PrismProcessor& p);
    void resized() override;
private:
    ParamPanel *voicing, *arpPanel, *chordPanel;
    juce::TextButton learn { "Learn chord" };
    juce::Label chordLabel;
    class AccentEditor;
    std::unique_ptr<AccentEditor> accents;
};

class SettingsPage : public Page
{
public:
    SettingsPage (PrismProcessor& p, std::function<void (double)> setScale);
    void resized() override;
private:
    ParamPanel* perf;
    juce::ComboBox scale;
    juce::ToggleButton tooltips { "Show tooltips" };
    juce::TextButton presetFolder, tableFolder, ccA, ccB;
    juce::Label info;
    std::function<void (double)> setScaleFn;
    std::unique_ptr<juce::FileChooser> chooser;
    void updateLabels();
};
} // namespace prism::gui
