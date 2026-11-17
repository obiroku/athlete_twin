#include "Recipes.h"

namespace lens::coach
{
RecipeRunner::RecipeRunner (const Content& c) : content (c) {}

void RecipeRunner::start (const juce::String& id, const CoachState& s)
{
    current = content.recipe (id);
    targetBand = -1;
    if (current != nullptr)
        enterStep (0, s);
}

void RecipeRunner::stop()
{
    current = nullptr;
    step = 0;
    if (onStepChanged)
        onStepChanged();
}

const RecipeStep* RecipeRunner::currentStep() const
{
    if (current == nullptr || step < 0 || step >= (int) current->steps.size())
        return nullptr;
    return &current->steps[(size_t) step];
}

bool RecipeRunner::isLastStep() const
{
    return current != nullptr && step == (int) current->steps.size() - 1;
}

bool RecipeRunner::stepHasCheck() const
{
    const auto* s = currentStep();
    return s != nullptr && s->check.isNotEmpty() && s->check != "none";
}

void RecipeRunner::enterStep (int index, const CoachState& s)
{
    step = index;
    stepStart = lastTime = s.time;
    playingTime = 0.0;
    holdSince = -1.0;
    events.clear();
    startBand = targetBand >= 0 ? s.params.bands[(size_t) targetBand] : dsp::BandParams {};
    if (onStepChanged)
        onStepChanged();
}

void RecipeRunner::event (const juce::String& name)
{
    if (current != nullptr)
        events.add (name);
}

int RecipeRunner::countEvents (const juce::String& e) const
{
    int n = 0;
    for (const auto& x : events)
        n += x == e ? 1 : 0;
    return n;
}

bool RecipeRunner::sequence (const juce::String& first, const juce::String& second) const
{
    const int i = events.indexOf (first);
    return i >= 0 && events.indexOf (second, false, i + 1) > i;
}

bool RecipeRunner::holdFor (bool condition, double seconds, const CoachState& s)
{
    if (! condition)
    {
        holdSince = -1.0;
        return false;
    }
    if (holdSince < 0.0)
        holdSince = s.time;
    return s.time - holdSince >= seconds;
}

int RecipeRunner::findBand (const CoachState& s, double lo, double hi, bool dynamicOnly) const
{
    auto ok = [&] (int b)
    {
        const auto& p = s.params.bands[(size_t) b];
        return p.on && p.freq >= lo && p.freq <= hi && (! dynamicOnly || p.isDynamic());
    };
    if (targetBand >= 0 && ok (targetBand))
        return targetBand;
    for (int b = 0; b < dsp::kNumBands; ++b)
        if (ok (b))
            return b;
    return -1;
}

bool RecipeRunner::check (const juce::String& id, int n, const CoachState& s)
{
    const auto& bands = s.params.bands;
    auto band = [&] (int b) -> const dsp::BandParams& { return bands[(size_t) b]; };
    auto target = [&] { return targetBand >= 0 && band (targetBand).on ? targetBand : -1; };
    auto anyOn = [&]
    {
        for (int b = 0; b < dsp::kNumBands; ++b)
            if (band (b).on)
                return b;
        return -1;
    };
    auto setTarget = [&] (int b)
    {
        if (b >= 0)
            targetBand = b;
        return b >= 0;
    };
    auto inRange = [] (double v, double lo, double hi) { return v >= lo && v <= hi; };
    auto playingFor = [&] (double seconds) { return playingTime >= seconds; };
    auto learnDone = [&] { return countEvents ("learn finished") > 0; };
    auto activityHold = [&]
    {
        const int b = target();
        return holdFor (b >= 0 && inRange (s.activity[(size_t) b], 10.0, 40.0), 5.0, s);
    };

    const juce::String key = id + ":" + juce::String (n);

    // rec_first_eq
    if (key == "rec_first_eq:1")
        return setTarget (anyOn());
    if (key == "rec_first_eq:2")
    {
        const int b = target() >= 0 ? target() : anyOn();
        return b >= 0 && (band (b).gain != startBand.gain || band (b).freq != startBand.freq); // NOLINT
    }
    if (key == "rec_first_eq:3")
    {
        const int b = target() >= 0 ? target() : anyOn();
        return b >= 0 && band (b).q != startBand.q; // NOLINT
    }
    if (key == "rec_first_eq:4")
        return countEvents ("bypass toggled") >= 2;
    if (key == "rec_first_eq:5")
        return s.params.global.autoGain;

    // rec_boomy_notes
    if (key == "rec_boomy_notes:1")
        return s.trackType == "Bass";
    if (key == "rec_boomy_notes:2" || key == "rec_harsh_hits:1")
        return playingFor (5.0);
    if (key == "rec_boomy_notes:3")
        return setTarget (findBand (s, 60.0, 300.0, false));
    if (key == "rec_boomy_notes:4")
    {
        const int b = target();
        return b >= 0 && band (b).isDynamic() && band (b).dynRange < 0.0f;
    }
    if (key == "rec_boomy_notes:5" || key == "rec_deess:5" || key == "rec_kick_bass:5" || key == "rec_vocal_pocket:4" || key == "rec_harsh_hits:5")
        return learnDone();
    if (key == "rec_boomy_notes:6")
    {
        const int b = target();
        return b >= 0 && inRange (band (b).dynAttack, 3.0, 30.0) && inRange (band (b).dynRelease, 60.0, 300.0);
    }
    if (key == "rec_boomy_notes:7" || key == "rec_harsh_hits:6")
        return activityHold();
    if (key == "rec_boomy_notes:8" || key == "rec_deess:7")
        return sequence ("delta on", "delta off");

    // rec_deess
    if (key == "rec_deess:1")
        return s.trackType == "Vocal" || s.trackType == "Choir & Vocal Bus";
    if (key == "rec_deess:2")
        return setTarget (findBand (s, 4000.0, 10000.0, true));
    if (key == "rec_deess:3")
    {
        const int b = target();
        return b >= 0 && band (b).detCustom && inRange (band (b).detHp, 4000.0, 6000.0) && inRange (band (b).detLp, 8000.0, 12000.0);
    }
    if (key == "rec_deess:4")
        return sequence ("listen on", "listen off");
    if (key == "rec_deess:6")
    {
        const int b = target();
        return b >= 0 && band (b).dynAttack <= 3.0f && inRange (band (b).dynRelease, 30.0, 100.0);
    }

    // rec_kick_bass
    if (key == "rec_kick_bass:1" || key == "rec_vocal_pocket:1")
        return s.sidechainPresent();
    if (key == "rec_kick_bass:2")
        return setTarget (findBand (s, 40.0, 120.0, true));
    if (key == "rec_kick_bass:3" || key == "rec_vocal_pocket:3")
    {
        const int b = target();
        return b >= 0 && band (b).detExternal;
    }
    if (key == "rec_kick_bass:4")
    {
        const int b = target();
        return b >= 0 && band (b).detCustom;
    }
    if (key == "rec_kick_bass:6")
    {
        const int b = target();
        return b >= 0 && band (b).dynAttack <= 3.0f && inRange (band (b).dynRelease, 60.0, 250.0);
    }

    // rec_vocal_pocket
    if (key == "rec_vocal_pocket:2")
        return setTarget (findBand (s, 1500.0, 4000.0, true));
    if (key == "rec_vocal_pocket:5")
    {
        const int b = target();
        return b >= 0 && inRange (band (b).dynRelease, 120.0, 400.0);
    }

    // rec_harsh_hits
    if (key == "rec_harsh_hits:2")
    {
        for (int b = 0; b < dsp::kNumBands; ++b)
            if (band (b).on && inRange (band (b).freq, 1500.0, 6000.0) && band (b).gain > 6.0f)
                return setTarget (b);
        return false;
    }
    if (key == "rec_harsh_hits:3")
    {
        const int b = target();
        return b >= 0 && band (b).gain < 0.0f && inRange (band (b).q, 1.0, 4.0);
    }
    if (key == "rec_harsh_hits:4")
    {
        const int b = target();
        return b >= 0 && band (b).isDynamic();
    }

    // rec_master_pass
    if (key == "rec_master_pass:1")
        return s.trackType == "Master";
    if (key == "rec_master_pass:3")
    {
        for (int b = 0; b < dsp::kNumBands; ++b)
            if (band (b).on && band (b).shape == dsp::Shape::LowCut && inRange (band (b).freq, 15.0, 35.0))
                return true;
        return false;
    }
    if (key == "rec_master_pass:6")
        return s.params.global.autoGain;

    return false; // steps without a check advance with Next
}

void RecipeRunner::update (const CoachState& s)
{
    if (current == nullptr)
        return;
    const double dt = juce::jlimit (0.0, 1.0, s.time - lastTime);
    lastTime = s.time;
    if (s.playing())
        playingTime += dt;

    if (stepHasCheck() && check (current->id, step + 1, s))
    {
        if (isLastStep())
            next (s);
        else
            enterStep (step + 1, s);
    }
}

void RecipeRunner::next (const CoachState& s)
{
    if (current == nullptr)
        return;
    if (isLastStep())
    {
        const auto id = current->id;
        stop();
        if (onFinished)
            onFinished (id);
        return;
    }
    enterStep (step + 1, s);
}

void RecipeRunner::back (const CoachState& s)
{
    if (current != nullptr && step > 0)
        enterStep (step - 1, s);
}
} // namespace lens::coach
