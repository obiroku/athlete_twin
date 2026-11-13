#pragma once
#include "Widgets.h"

namespace prism::gui
{
// Large animated waveform of the current frame with the live WT-position
// playhead, the warp drawn on the waveform, and drag-to-scrub.
class WavetableView : public juce::Component, public LiveComponent, public juce::SettableTooltipClient
{
public:
    WavetableView (PrismProcessor& p, int osc);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refreshLive() override;
private:
    PrismProcessor& proc;
    int osc;
    std::shared_ptr<const std::vector<std::vector<float>>> frames;
    float shownPos = -1, shownWarp = -1;
    int shownMode = -1;
    float dragStart = 0;
    juce::Path cached;
    void rebuildPath();
};

// Breakpoint editor for MSEG / custom LFO / remap shapes.
// Drag points, double-click to add, right-click to delete, Alt-drag a segment to bend it.
class ShapeEditor : public juce::Component, public LiveComponent
{
public:
    enum class Target { Mseg, Lfo, Remap };
    ShapeEditor (PrismProcessor& p, Target t, int index);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refreshLive() override;
private:
    PrismProcessor& proc;
    Target target;
    int index;
    dsp::Shape shape;
    int seen = -1, dragPt = -1, bendSeg = -1;
    float bendStart = 0, playPos = -1;
    dsp::Shape get() const;
    void set (const dsp::Shape& s);
    juce::Point<float> toScreen (float x, float y) const;
    int hit (juce::Point<float> p) const;
};

// Oscilloscope + spectrum (SHOULD) from the processor's scope ring
class ScopeView : public juce::Component, public LiveComponent
{
public:
    explicit ScopeView (PrismProcessor& p) : proc (p) { setTitle ("Output scope"); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { spectrum = ! spectrum; repaint(); }
    void refreshLive() override;
private:
    PrismProcessor& proc;
    std::array<float, 1024> snap {};
    bool spectrum = false;
    int lastW = -1;
};

class Meter : public juce::Component, public LiveComponent
{
public:
    explicit Meter (PrismProcessor& p) : proc (p) { setTitle ("Output meter"); }
    void paint (juce::Graphics&) override;
    void refreshLive() override;
private:
    PrismProcessor& proc;
    float l = 0, r = 0;
};
} // namespace prism::gui
