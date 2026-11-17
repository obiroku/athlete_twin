#include "PresetManager.h"

#include <BinaryData.h>

namespace lens::presets
{
namespace
{
PresetEntry entryFrom (const juce::var& v)
{
    PresetEntry e;
    e.name = v["name"].toString();
    e.category = v["category"].toString();
    e.note = v["note"].toString();
    e.trackType = v["track_type"].toString();
    return e;
}
} // namespace

PresetManager::PresetManager (coach::Settings& s, juce::File dir) : settings (s), userDir (std::move (dir))
{
    rescan();
}

juce::File PresetManager::defaultUserDir()
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable ("LENS_PRESET_DIR", {});
    if (overridePath.isNotEmpty())
        return juce::File (overridePath);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LENS/Presets");
}

juce::StringArray PresetManager::categories()
{
    return { "Init", "Bass", "Drums", "Vocal", "Keys", "Guitar", "Synth", "Bus", "Master" };
}

void PresetManager::rescan()
{
    entries.clear();

    // Factory bank, in file-name order (f00_…, f01_…).
    juce::StringArray names;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String (BinaryData::namedResourceList[i]).endsWith ("_lenspreset"))
            names.add (BinaryData::namedResourceList[i]);
    names.sort (false);
    for (const auto& n : names)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (n.toRawUTF8(), size);
        auto e = entryFrom (juce::JSON::parse (juce::String::fromUTF8 (data, size)));
        e.factory = true;
        e.resource = n;
        entries.push_back (e);
    }

    // User presets.
    if (userDir.isDirectory())
    {
        auto files = userDir.findChildFiles (juce::File::findFiles, false, "*.lenspreset");
        files.sort();
        for (const auto& f : files)
        {
            const auto v = juce::JSON::parse (f);
            if (! v.isObject())
                continue;
            auto e = entryFrom (v);
            if (e.name.isEmpty())
                e.name = f.getFileNameWithoutExtension();
            e.file = f;
            entries.push_back (e);
        }
    }
}

juce::String PresetManager::jsonFor (const PresetEntry& e) const
{
    if (e.factory)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (e.resource.toRawUTF8(), size);
        return juce::String::fromUTF8 (data, size);
    }
    return e.file.loadFileAsString();
}

std::vector<int> PresetManager::filtered (const juce::String& search, const juce::String& category, bool favoritesOnly) const
{
    std::vector<int> out;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (category.isNotEmpty() && e.category != category)
            continue;
        if (favoritesOnly && ! isFavorite (e))
            continue;
        if (search.isNotEmpty() && ! e.name.containsIgnoreCase (search) && ! e.note.containsIgnoreCase (search) && ! e.category.containsIgnoreCase (search))
            continue;
        out.push_back (i);
    }
    return out;
}

int PresetManager::indexOfName (const juce::String& name) const
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == name)
            return i;
    return -1;
}

bool PresetManager::isFavorite (const PresetEntry& e) const
{
    return settings.favorites.contains (e.key());
}

void PresetManager::toggleFavorite (const PresetEntry& e)
{
    if (isFavorite (e))
        settings.favorites.removeString (e.key());
    else
        settings.favorites.add (e.key());
    settings.save();
}

int PresetManager::saveAs (const juce::String& name, const juce::String& json)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return -1;
    userDir.createDirectory();
    auto v = juce::JSON::parse (json);
    if (auto* o = v.getDynamicObject())
        o->setProperty ("name", name.trim());
    const auto f = userDir.getChildFile (clean + ".lenspreset");
    if (! f.replaceWithText (juce::JSON::toString (v)))
        return -1;
    rescan();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (! entries[(size_t) i].factory && entries[(size_t) i].file == f)
            return i;
    return -1;
}

bool PresetManager::save (int index, const juce::String& json)
{
    if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].factory)
        return false;
    auto v = juce::JSON::parse (json);
    if (auto* o = v.getDynamicObject())
        o->setProperty ("name", entries[(size_t) index].name);
    return entries[(size_t) index].file.replaceWithText (juce::JSON::toString (v));
}
} // namespace lens::presets
