#include "PresetLibrary.h"
#include "BinaryData.h"

namespace prism
{
// ------------------------------------------------------------------ settings

Settings::Settings()
{
    presetFolder = PresetLibrary::defaultUserPresetFolder();
    tableFolder = PresetLibrary::defaultUserTableFolder();
}

juce::File Settings::file() const
{
#if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/PRISM/settings.json");
#else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("PRISM/settings.json");
#endif
}

void Settings::load()
{
    const auto f = file();
    if (! f.existsAsFile()) return;
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return;
    favourites.clear();
    if (auto* a = v["favorites"].getArray()) for (auto& s : *a) favourites.addIfNotAlreadyThere (s.toString());
    guiScale = juce::jlimit (0.75, 2.0, (double) v.getProperty ("gui_scale", 1.0));
    tooltips = (bool) v.getProperty ("tooltips", true);
    if (v.hasProperty ("preset_folder")) presetFolder = juce::File (v["preset_folder"].toString());
    if (v.hasProperty ("table_folder")) tableFolder = juce::File (v["table_folder"].toString());
}

void Settings::save() const
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> fav;
    for (auto& s : favourites) fav.add (s);
    o->setProperty ("favorites", fav);
    o->setProperty ("gui_scale", guiScale);
    o->setProperty ("tooltips", tooltips);
    o->setProperty ("preset_folder", presetFolder.getFullPathName());
    o->setProperty ("table_folder", tableFolder.getFullPathName());
    const auto f = file();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

// ------------------------------------------------------------------ library

const juce::StringArray& PresetLibrary::categories()
{
    static const juce::StringArray c { "Init", "Keys", "Bass", "Pad", "Lead", "Pluck", "Organ", "Choir/Vox", "Arp", "Texture" };
    return c;
}

juce::File PresetLibrary::defaultUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PRISM/Presets");
}

juce::File PresetLibrary::defaultUserTableFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PRISM/Wavetables");
}

std::vector<std::pair<juce::String, juce::String>> PresetLibrary::factoryPresetSources()
{
    std::vector<std::pair<juce::String, juce::String>> out;
    for (int i = 0; i < PrismBinary::namedResourceListSize; ++i)
    {
        const juce::String orig = PrismBinary::originalFilenames[i];
        if (! orig.endsWithIgnoreCase (".prismpreset")) continue;
        int size = 0;
        const char* data = PrismBinary::getNamedResource (PrismBinary::namedResourceList[i], size);
        if (data == nullptr) continue;
        out.emplace_back (PrismBinary::namedResourceList[i], juce::String::fromUTF8 (data, size));
    }
    return out;
}

PresetLibrary::PresetLibrary()
{
    settings.load();
    rescan();
}

void PresetLibrary::rescan()
{
    list.clear();
    const auto& cats = categories();
    for (auto& [res, text] : factoryPresetSources())
    {
        const auto v = juce::JSON::parse (text);
        if (! v.isObject()) continue;
        PresetEntry e;
        e.factory = true;
        e.resourceName = res;
        e.name = v["name"].toString();
        e.category = v["category"].toString();
        e.author = v["author"].toString();
        if (auto* t = v["tags"].getArray()) for (auto& s : *t) e.tags.add (s.toString());
        list.push_back (e);
    }
    std::stable_sort (list.begin(), list.end(), [&] (const PresetEntry& a, const PresetEntry& b) {
        const int ca = cats.indexOf (a.category), cb = cats.indexOf (b.category);
        if (ca != cb) return ca < cb;
        return a.name.compareNatural (b.name) < 0;
    });
    // user presets
    const auto folder = settings.presetFolder;
    if (folder.isDirectory())
    {
        std::vector<PresetEntry> user;
        for (const auto& f : juce::RangedDirectoryIterator (folder, true, "*.prismpreset"))
        {
            const auto v = juce::JSON::parse (f.getFile().loadFileAsString());
            if (! v.isObject()) continue;
            PresetEntry e;
            e.factory = false;
            e.file = f.getFile();
            e.name = v["name"].toString();
            if (e.name.isEmpty()) e.name = f.getFile().getFileNameWithoutExtension();
            e.category = v["category"].toString();
            e.author = v["author"].toString();
            if (auto* t = v["tags"].getArray()) for (auto& s : *t) e.tags.add (s.toString());
            user.push_back (e);
        }
        std::sort (user.begin(), user.end(), [] (const PresetEntry& a, const PresetEntry& b) { return a.name.compareNatural (b.name) < 0; });
        list.insert (list.end(), user.begin(), user.end());
    }
}

int PresetLibrary::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < list.size(); ++i) if (list[i].name == name) return (int) i;
    return -1;
}

bool PresetLibrary::load (int index, PresetData& out, juce::String& err) const
{
    if (index < 0 || index >= (int) list.size()) { err = "no such preset"; return false; }
    const auto& e = list[(size_t) index];
    juce::String text;
    if (e.factory)
    {
        int size = 0;
        const char* data = PrismBinary::getNamedResource (e.resourceName.toRawUTF8(), size);
        if (data == nullptr) { err = "missing resource"; return false; }
        text = juce::String::fromUTF8 (data, size);
    }
    else text = e.file.loadFileAsString();
    return presetFromString (text, out, err);
}

bool PresetLibrary::saveUser (const PresetData& p, juce::String& err)
{
    auto folder = settings.presetFolder;
    if (p.category.size() > 0) folder = folder.getChildFile (juce::File::createLegalFileName (juce::String (p.category).replace ("/", "-")));
    if (! folder.createDirectory()) { err = "cannot create " + folder.getFullPathName(); return false; }
    const auto f = folder.getChildFile (juce::File::createLegalFileName (juce::String (p.name)) + ".prismpreset");
    if (! f.replaceWithText (presetToString (p, false))) { err = "cannot write " + f.getFullPathName(); return false; }
    rescan();
    return true;
}
} // namespace prism
