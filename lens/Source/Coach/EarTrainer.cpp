#include "EarTrainer.h"

#include "Util/Format.h"

namespace lens::coach
{
const std::vector<EarTrainer::Level>& EarTrainer::levels()
{
    // LESSONS.md §7.
    static const std::vector<Level> l = [] {
        auto octaves = [] (double lo, double hi)
        {
            std::vector<double> v;
            for (double f = lo; f <= hi * 1.01; f *= 2.0)
                v.push_back (f);
            return v;
        };
        const std::vector<double> thirds { 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
                                           1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000 };
        return std::vector<Level> {
            { 1, 12.0f, 1.5f, { 125, 500, 2000, 8000 }, false, 8 },
            { 2, 9.0f, 1.5f, octaves (62.5, 8000), false, 8 },
            { 3, 6.0f, 2.0f, octaves (31.25, 16000), false, 8 },
            { 4, -9.0f, 2.0f, octaves (62.5, 8000), false, 8 },
            { 5, 6.0f, 4.0f, thirds, true, 7 },
        };
    }();
    return l;
}

EarTrainer::EarTrainer (Settings& s, const Content& c, juce::int64 seed) : settings (s), content (c), rng (seed) {}

const EarTrainer::Level& EarTrainer::level() const
{
    return levels()[(size_t) juce::jlimit (1, 5, settings.earLevel) - 1];
}

void EarTrainer::start()
{
    active = true;
    questionIndex = 0;
    right = 0;
    newQuestion();
}

void EarTrainer::stop()
{
    active = false;
}

juce::String EarTrainer::instructions() const
{
    return content.earMessages.getValue ("instructions", {});
}

void EarTrainer::newQuestion()
{
    const auto& ch = level().choices;
    target = ch[(size_t) rng.nextInt ((int) ch.size())];
}

EarTrainer::Result EarTrainer::guess (double freq)
{
    Result r;
    if (! active)
        return r;
    const auto& lv = level();
    r.target = target;

    if (lv.withinThirdOctave)
    {
        r.guess = freq;
        r.right = std::abs (std::log2 (freq / target)) <= 1.0 / 3.0;
    }
    else
    {
        // Snap to the nearest choice.
        double best = lv.choices.front();
        for (double c : lv.choices)
            if (std::abs (std::log2 (freq / c)) < std::abs (std::log2 (freq / best)))
                best = c;
        r.guess = best;
        r.right = std::abs (best - target) < 1.0e-6;
    }

    if (r.right)
    {
        ++right;
        r.message = content.earMessages.getValue ("right", {}).replace ("{freq}", fmt::freq (target));
        settings.earRight++;
        settings.earStreak++;
        settings.earBestStreak = std::max (settings.earBestStreak, settings.earStreak);
    }
    else
    {
        r.message = content.earMessages.getValue ("wrong", {}).replace ("{freq}", fmt::freq (target)).replace ("{guess}", fmt::freq (r.guess));
        settings.earStreak = 0;
    }
    settings.earTotal++;

    if (++questionIndex >= 10)
    {
        if (right >= lv.passCount && settings.earLevel < 5)
        {
            settings.earLevel++;
            r.levelUp = true;
            r.message << " " << content.earMessages.getValue ("level_up", {}).replace ("{n}", juce::String (settings.earLevel));
        }
        questionIndex = 0;
        right = 0;
    }
    settings.save();
    newQuestion();
    return r;
}
} // namespace lens::coach
