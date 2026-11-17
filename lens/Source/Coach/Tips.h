#pragma once

#include <map>
#include <set>

#include "CoachContent.h"
#include "CoachState.h"

namespace lens::coach
{
// Tip rules from LESSONS.md §4, evaluated 4 times a second on the message
// thread. Read-only: tips never change the sound.
class TipEngine
{
public:
    explicit TipEngine (const Content& = Content::get());

    void update (const CoachState&);

    struct ActiveTip
    {
        juce::String id, text;
    };
    // Tips that fire now, minus hidden ones, in LESSONS.md order.
    std::vector<ActiveTip> visible (int max = 2) const;
    bool isFiring (const juce::String& id) const { return firing.count (id) > 0; }
    juce::String text (const juce::String& id) const;

    void gotIt (const juce::String& id) { hiddenThisSession.insert (id); }
    void setNeverShow (const juce::StringArray& ids);
    void setQuiet (bool q) noexcept { quiet = q; }
    bool isQuiet() const noexcept { return quiet; }

private:
    bool hold (const juce::String& key, bool condition, double seconds, double now);
    void fire (const juce::String& id, const juce::String& text) { firing[id] = text; }

    const Content& content;
    std::map<juce::String, juce::String> firing;
    std::map<juce::String, double> since;
    std::set<juce::String> hiddenThisSession, never, latched;
    bool quiet = false;

    bool seenLinearPhase = false, seenDynamic = false, seenMaster = false;
    bool prevLinearPhase = false;
    int prevDynamicCount = 0;
    double lastClip = -1.0e9;
};
} // namespace lens::coach
