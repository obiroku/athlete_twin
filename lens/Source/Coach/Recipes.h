#pragma once

#include "CoachContent.h"
#include "CoachState.h"

namespace lens::coach
{
// Guided recipes (LESSONS.md §5). A step advances when its completion
// check passes; Back and Skip always work. Read-only: recipes never
// change the sound, they only watch.
class RecipeRunner
{
public:
    explicit RecipeRunner (const Content& = Content::get());

    void start (const juce::String& recipeId, const CoachState&);
    void stop();
    bool isRunning() const noexcept { return current != nullptr; }
    const Recipe* recipe() const noexcept { return current; }
    int stepIndex() const noexcept { return step; }
    const RecipeStep* currentStep() const;
    bool isLastStep() const;
    bool stepHasCheck() const;

    // Message thread, 4 times a second.
    void update (const CoachState&);
    // UI events: "bypass toggled", "delta on", "delta off", "listen on",
    // "listen off", "learn finished", ...
    void event (const juce::String& name);

    void next (const CoachState&); // Skip / Next / Done
    void back (const CoachState&);

    // The completion check for one step (headless, for tests).
    bool check (const juce::String& recipeId, int stepNumber, const CoachState&);

    int targetBand = -1;
    std::function<void (const juce::String& recipeId)> onFinished;
    std::function<void()> onStepChanged;

private:
    void enterStep (int index, const CoachState&);
    int findBand (const CoachState&, double lo, double hi, bool dynamicOnly) const;
    int countEvents (const juce::String&) const;
    bool sequence (const juce::String& first, const juce::String& second) const;
    bool holdFor (bool condition, double seconds, const CoachState&);

    const Content& content;
    const Recipe* current = nullptr;
    int step = 0;

    // Per-step tracking.
    double stepStart = 0.0, lastTime = 0.0, playingTime = 0.0, holdSince = -1.0;
    dsp::BandParams startBand {};
    juce::StringArray events;
};
} // namespace lens::coach
