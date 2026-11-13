#pragma once
#include "Pages.h"

namespace prism::gui
{
// WAVETABLE tab (Phase 7): draw frames with keyframes, formula editor,
// import with preview, morph between two tables, export with a "clm " chunk.
class WavetablePage : public Page, public LiveComponent
{
public:
    explicit WavetablePage (PrismProcessor& p);
    ~WavetablePage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void collectLive (std::vector<LiveComponent*>& out) override { out.push_back (this); }
    void refreshLive() override;

    // used by tests: apply a formula synchronously (no thread)
    static bool buildFromFormula (const juce::String& text, int frames, std::vector<std::vector<float>>& out, juce::String& error);

private:
    class FrameEditor;
    std::unique_ptr<FrameEditor> frameEditor;
    juce::ComboBox oscSel, importMode, framesSel, morphA, morphB;
    juce::Slider frameSlider;
    juce::TextButton loadCurrent { "Load from osc" }, keyBtn { "Toggle keyframe" }, interp { "Interpolate keyframes" },
                     apply { "Apply to osc" }, formulaApply { "Run formula" }, importBtn { "Import file..." },
                     morphBtn { "Morph A -> B" }, exportBtn { "Export WAV..." };
    juce::TextEditor formula;
    juce::Label status;
    std::vector<std::vector<float>> work;   // working frames (2048 each)
    std::vector<bool> keys;
    int currentFrame = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    void setWork (std::vector<std::vector<float>> f, const juce::String& msg);
    void applyToOsc (const juce::String& name);
    std::vector<std::vector<float>> tableFrames (int comboId);
};
} // namespace prism::gui
