#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "CoachContent.h"
#include "Settings.h"

// Ear trainer (SPEC §8, levels from LESSONS.md §7). A hidden Bell on top
// of the EQ; the player toggles it and clicks where they think it is.
// Message thread. Never part of presets or plugin state.
namespace lens::coach
{
class EarTrainer
{
public:
    struct Level
    {
        int number;
        float gainDb, q;
        std::vector<double> choices;
        bool withinThirdOctave; // level 5: any click within 1/3 octave
        int passCount;          // out of 10
    };
    static const std::vector<Level>& levels();

    explicit EarTrainer (Settings&, const Content& = Content::get(), juce::int64 seed = juce::Time::currentTimeMillis());

    void start();
    void stop();
    bool isActive() const noexcept { return active; }

    const Level& level() const;
    int question() const noexcept { return questionIndex; } // 0..9
    int rightThisRound() const noexcept { return right; }
    double targetFreq() const noexcept { return target; }

    struct Result
    {
        bool right = false;
        bool levelUp = false;
        double guess = 0.0, target = 0.0;
        juce::String message;
    };
    // A click on the display at `freq`.
    Result guess (double freq);
    juce::String instructions() const;

private:
    void newQuestion();

    Settings& settings;
    const Content& content;
    juce::Random rng;
    bool active = false;
    int questionIndex = 0, right = 0;
    double target = 1000.0;
};
} // namespace lens::coach
