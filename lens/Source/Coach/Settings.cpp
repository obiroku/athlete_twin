#include "Settings.h"

namespace lens::coach
{
namespace
{
juce::var toVar (const juce::StringArray& a)
{
    juce::Array<juce::var> v;
    for (const auto& s : a)
        v.add (s);
    return v;
}

void fromVar (const juce::var& v, juce::StringArray& dest)
{
    if (auto* arr = v.getArray())
    {
        dest.clear();
        for (const auto& x : *arr)
            if (x.isString())
                dest.addIfNotAlreadyThere (x.toString());
    }
}

void readBool (const juce::var& o, const char* k, bool& d)
{
    if (o[k].isBool())
        d = (bool) o[k];
}

void readInt (const juce::var& o, const char* k, int& d)
{
    if (o[k].isInt() || o[k].isInt64() || o[k].isDouble())
        d = (int) o[k];
}
} // namespace

Settings::Settings (juce::File f) : path (std::move (f))
{
    load();
}

juce::File Settings::defaultFile()
{
    // Tests point this at a temporary file.
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("LENS_SETTINGS_FILE", {});
    if (overridePath.isNotEmpty())
        return juce::File (overridePath);
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/LENS/settings.json");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LENS/settings.json");
#endif
}

void Settings::load()
{
    if (! path.existsAsFile())
        return;
    const auto v = juce::JSON::parse (path);
    if (! v.isObject())
        return;
    readBool (v, "first_run_done", firstRunDone);
    readBool (v, "coach_visible", coachVisible);
    readBool (v, "quiet", quiet);
    fromVar (v["tips_never"], tipsNever);
    fromVar (v["recipes_done"], recipesDone);
    fromVar (v["lessons_read"], lessonsRead);
    fromVar (v["favorites"], favorites);
    const auto& e = v["ear_trainer"];
    readInt (e, "level", earLevel);
    readInt (e, "right", earRight);
    readInt (e, "total", earTotal);
    readInt (e, "streak", earStreak);
    readInt (e, "best_streak", earBestStreak);
    earLevel = juce::jlimit (1, 5, earLevel);
}

bool Settings::save() const
{
    auto* o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("format_version", 1);
    o->setProperty ("first_run_done", firstRunDone);
    o->setProperty ("coach_visible", coachVisible);
    o->setProperty ("quiet", quiet);
    o->setProperty ("tips_never", toVar (tipsNever));
    o->setProperty ("recipes_done", toVar (recipesDone));
    o->setProperty ("lessons_read", toVar (lessonsRead));
    o->setProperty ("favorites", toVar (favorites));
    auto* e = new juce::DynamicObject();
    e->setProperty ("level", earLevel);
    e->setProperty ("right", earRight);
    e->setProperty ("total", earTotal);
    e->setProperty ("streak", earStreak);
    e->setProperty ("best_streak", earBestStreak);
    o->setProperty ("ear_trainer", juce::var (e));
    path.getParentDirectory().createDirectory();
    return path.replaceWithText (juce::JSON::toString (root));
}
} // namespace lens::coach
