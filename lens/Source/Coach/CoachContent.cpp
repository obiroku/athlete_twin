#include "CoachContent.h"

#include <BinaryData.h>

namespace lens::coach
{
namespace
{
juce::var parseResource (const char* name)
{
    int size = 0;
    if (const char* data = BinaryData::getNamedResource (name, size))
        return juce::JSON::parse (juce::String::fromUTF8 (data, size));
    jassertfalse;
    return {};
}
} // namespace

const Content& Content::get()
{
    static const Content c = []
    {
        Content x;
        for (auto* n : { "explain", "zones", "steps", "tips", "recipes", "lessons", "eartrainer", "glossary", "controls" })
            x.parse (n, parseResource ((juce::String (n) + "_json").toRawUTF8()));
        return x;
    }();
    return c;
}

Content Content::fromFolder (const juce::File& dir)
{
    Content x;
    for (auto* n : { "explain", "zones", "steps", "tips", "recipes", "lessons", "eartrainer", "glossary", "controls" })
        x.parse (n, juce::JSON::parse (dir.getChildFile (juce::String (n) + ".json")));
    return x;
}

void Content::parse (const juce::String& name, const juce::var& j)
{
    if (name == "explain")
    {
        for (const auto& w : *j["width"].getArray())
            width.push_back ({ (double) w["min"], (double) w["max"], w["text"].toString() });
        if (auto* o = j["templates"].getDynamicObject())
            for (const auto& p : o->getProperties())
                templates.set (p.name.toString(), p.value.toString());
    }
    else if (name == "zones")
    {
        if (auto* o = j.getDynamicObject())
            for (const auto& p : o->getProperties())
            {
                std::vector<Zone> zl;
                for (const auto& z : *p.value.getArray())
                    zl.push_back ({ z["name"].toString(), (double) z["lo"], z["hi"].isVoid() ? 0.0 : (double) z["hi"] });
                zones.emplace_back (p.name.toString(), std::move (zl));
            }
    }
    else if (name == "steps" || name == "controls")
    {
        auto& dest = name == "steps" ? steps : controls;
        if (auto* o = j.getDynamicObject())
            for (const auto& p : o->getProperties())
                dest.set (p.name.toString(), p.value.toString());
    }
    else if (name == "tips")
    {
        for (const auto& t : *j.getArray())
            tips.push_back ({ t["id"].toString(), t["when"].toString(), t["text"].toString() });
    }
    else if (name == "recipes")
    {
        for (const auto& r : *j.getArray())
        {
            Recipe rec { r["id"].toString(), r["title"].toString(), {}, {} };
            for (const auto& t : *r["trackTypes"].getArray())
                rec.trackTypes.add (t.toString());
            for (const auto& s : *r["steps"].getArray())
                rec.steps.push_back ({ s["highlight"].toString(), s["text"].toString(), s["check"].toString() });
            recipes.push_back (std::move (rec));
        }
    }
    else if (name == "lessons")
    {
        for (const auto& l : *j.getArray())
            lessons.push_back ({ l["id"].toString(), l["title"].toString(), l["text"].toString(), l["tryIt"].toString() });
    }
    else if (name == "glossary")
    {
        for (const auto& g : *j.getArray())
            glossary.push_back ({ g["term"].toString(), g["definition"].toString() });
    }
    else if (name == "eartrainer")
    {
        for (const auto& l : *j["levels"].getArray())
            earLevels.push_back ({ (int) l["level"], l["band"].toString(), l["choices"].toString(), l["pass"].toString() });
        if (auto* o = j["messages"].getDynamicObject())
            for (const auto& p : o->getProperties())
                earMessages.set (p.name.toString(), p.value.toString());
    }
}

juce::String Content::tooltip (const juce::String& control) const
{
    return controls.getValue (control, {});
}

juce::String Content::template_ (const juce::String& key) const
{
    return templates.getValue (key, "TODO: " + key);
}

juce::String Content::stepHint (const juce::String& step) const
{
    return steps.getValue ("step_" + step.toLowerCase(), {});
}

const std::vector<Zone>& Content::zonesFor (const juce::String& trackType) const
{
    for (const auto& z : zones)
        if (z.first == trackType)
            return z.second;
    for (const auto& z : zones)
        if (z.first == "Default")
            return z.second;
    static const std::vector<Zone> none;
    return none;
}

juce::String Content::zoneAt (const juce::String& trackType, double freq) const
{
    const auto& zl = zonesFor (trackType);
    if (zl.empty())
        return {};
    if (freq < zl.front().lo)
        return zl.front().name;
    for (const auto& z : zl)
        if (freq >= z.lo && (z.hi <= 0.0 || freq < z.hi))
            return z.name;
    return zl.back().name;
}

const Tip* Content::tip (const juce::String& id) const
{
    for (const auto& t : tips)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Recipe* Content::recipe (const juce::String& id) const
{
    for (const auto& r : recipes)
        if (r.id == id)
            return &r;
    return nullptr;
}

const Lesson* Content::lesson (const juce::String& id) const
{
    for (const auto& l : lessons)
        if (l.id == id)
            return &l;
    return nullptr;
}

juce::StringArray Content::allIds() const
{
    juce::StringArray ids;
    ids.addArray (templates.getAllKeys());
    ids.addArray (steps.getAllKeys());
    for (const auto& t : tips)
        ids.add (t.id);
    for (const auto& r : recipes)
        ids.add (r.id);
    for (const auto& l : lessons)
        ids.add (l.id);
    return ids;
}
} // namespace lens::coach
