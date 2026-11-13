#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"

namespace prism::gui
{
// Something refreshed from the editor's VBlank callback (cheap when nothing changed)
struct LiveComponent
{
    virtual ~LiveComponent() = default;
    virtual void refreshLive() = 0;
};

// Rotary knob bound to a parameter: hover/drag value, double-click reset,
// Cmd/Ctrl-drag fine control, click the value to type, modulation ring with a
// live dot, drop target for mod sources, right-click menu.
class Knob : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient, public LiveComponent
{
public:
    Knob (PrismProcessor& p, int paramIndex, const juce::String& label = {});
    ~Knob() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

    void refreshLive() override;
    int getParamIndex() const { return index; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    PrismProcessor& proc;
    int index;
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::ParameterAttachment attach;
    float norm = 0.0f;
    bool hover = false, dragging = false, dropHover = false, editingDepth = false;
    float dragStartNorm = 0, dragStartDepth = 0;
    int depthSlot = -1;             // routing slot edited by Alt-drag
    float ringLo = 0, ringHi = 0;   // modulation range (normalized)
    bool hasMod = false;
    std::vector<int> ringSlots;
    std::vector<float> ringDepths;
    float liveDot = -1.0f;
    int seenVersion = -1;
    std::unique_ptr<juce::TextEditor> editor;

    void updateRing();
    juce::String valueText() const;
    void showMenu();
    void beginTextEntry();
    std::vector<int> routingsHere() const;
};

class ParamCombo : public juce::ComboBox
{
public:
    ParamCombo (PrismProcessor& p, int paramIndex);
private:
    juce::ComboBoxParameterAttachment attach;
};

class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (PrismProcessor& p, int paramIndex, const juce::String& text);
private:
    juce::ButtonParameterAttachment attach;
};

// Drag handle for a modulation source ("drag onto any knob")
class ModHandle : public juce::Component, public juce::SettableTooltipClient, public LiveComponent
{
public:
    ModHandle (PrismProcessor& p, int src);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void refreshLive() override;
private:
    PrismProcessor& proc;
    int src;
    float level = 0.0f;
};

// A titled panel that lays its parameter controls out in a flow grid.
class ParamPanel : public juce::Component
{
public:
    ParamPanel (PrismProcessor& p, const juce::String& title);
    // Adds the right control for the parameter type. Returns the component.
    juce::Component* add (const std::string& id, const juce::String& label = {}, int span = 1);
    void addCustom (juce::Component* c, int widthCells, int heightRows = 1);
    void setHeaderComponent (juce::Component* c) { header = c; if (c) addAndMakeVisible (c); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void collectLive (std::vector<LiveComponent*>& out);
    juce::String title;
    int cellW = 58, cellH = 70;

private:
    PrismProcessor& proc;
    struct Item { juce::Component* c; int w, h; bool small; };
    std::vector<Item> items;
    std::vector<std::unique_ptr<juce::Component>> owned;
    juce::Component* header = nullptr;
};

juce::String shortName (const ParamDef& d);
} // namespace prism::gui
