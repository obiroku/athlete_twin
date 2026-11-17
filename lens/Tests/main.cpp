#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

#include <cstdlib>

int main (int argc, char* argv[])
{
    // Never touch the real settings or user presets.
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lens-tests-" + juce::String (juce::Time::currentTimeMillis()));
    tmp.createDirectory();
    setenv ("LENS_SETTINGS_FILE", tmp.getChildFile ("settings.json").getFullPathName().toRawUTF8(), 1);
    setenv ("LENS_PRESET_DIR", tmp.getChildFile ("Presets").getFullPathName().toRawUTF8(), 1);
    setenv ("LENS_REFERENCE_DIR", tmp.getChildFile ("References").getFullPathName().toRawUTF8(), 1);

    juce::ScopedJuceInitialiser_GUI juceInit;
    const int result = Catch::Session().run (argc, argv);
    tmp.deleteRecursively();
    return result;
}
