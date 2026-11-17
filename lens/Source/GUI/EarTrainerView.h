#pragma once

#include "EditorContext.h"
#include "Theme.h"

#include "Coach/EarTrainer.h"

namespace lens::gui
{
class SpectrumDisplay;

// The ear trainer inside the Coach panel. Owns the game; drives the
// hidden band through the processor's ear-trainer atomics.
class EarTrainerView final : public juce::Component
{
public:
    EarTrainerView (EditorContext&, SpectrumDisplay&);
    ~EarTrainerView() override;

    void setActive (bool);
    bool isActive() const noexcept { return game.isActive(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void applyBand();
    void onGuess (double freq);

    EditorContext& ctx;
    SpectrumDisplay& display;
    coach::EarTrainer game;
    juce::ToggleButton hidden { "Hidden band" };
    juce::String message;
};
} // namespace lens::gui
