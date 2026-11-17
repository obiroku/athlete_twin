// Renders the editor to a PNG, headless, with audio flowing so the
// analyzer has something to show. Usage: lens_snapshot out.png [scale]
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/CoachPanel.h"
#include "GUI/Editor.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

using namespace lens::test;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "lens.png"));
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const juce::String setup = argc > 3 ? juce::String (argv[3]) : juce::String ("default");

    lens::LensProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    setBand (proc, 1, { 3, 35.0f, 0.0f, 1.0f, 3 });
    setBand (proc, 2, { 0, 180.0f, 0.0f, 2.5f });
    setDyn (proc, 2, { 0, -5.0f, -40.0f, 3.0f, 8.0f, 120.0f });
    setBand (proc, 3, { 0, 700.0f, 1.5f, 1.2f });
    setBand (proc, 4, { 2, 6000.0f, -2.0f, 0.7f });
    auto s = proc.getSession();
    s.trackType = "Bass";
    s.presetName = "Upright Bass Body";
    s.note = "Evens out boomy notes and brings out the wood.";
    proc.setSession (s);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<lens::gui::LensEditor*> (ed.get());
    editor->setSize ((int) (1000 * scale), (int) (650 * scale));
    editor->context().selection.selectOnly (1);
    if (setup == "coach" || setup == "recipes" || setup == "glossary")
        editor->view().setCoachVisible (true);
    if (setup == "browser" && editor->view().onPresetNameClicked)
        editor->view().onPresetNameClicked();

    // Pink noise with a resonance, 4 s, in real-time-ish chunks.
    auto pink = makePink (2, 48000 * 4, 3, -20.0);
    lens::dsp::EqBand res;
    res.prepare (48000.0, 2);
    lens::dsp::BandParams rp;
    rp.on = true;
    rp.freq = 220.0f;
    rp.gain = 10.0f;
    rp.q = 8.0f;
    res.setParams (rp, true);
    std::vector<double> l (512), r (512);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + 512 <= pink.getNumSamples(); pos += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            l[(size_t) i] = pink.getSample (0, pos + i);
            r[(size_t) i] = pink.getSample (1, pos + i);
        }
        res.process (l.data(), r.data(), 512, nullptr);
        juce::AudioBuffer<float> block (4, 512);
        block.clear();
        for (int i = 0; i < 512; ++i)
        {
            block.setSample (0, i, (float) l[(size_t) i]);
            block.setSample (1, i, (float) r[(size_t) i]);
        }
        proc.processBlock (block, midi);
        proc.serviceMessageThread();
        if (pos % 4096 == 0)
            juce::Thread::sleep (2);
    }
    juce::Thread::sleep (300);
    if (auto* coach = dynamic_cast<lens::gui::CoachPanel*> (editor->view().coachComponent))
    {
        if (setup == "recipes")
            coach->showTab (1);
        if (setup == "glossary")
            coach->showGlossaryTerm ("Threshold");
    }
    editor->view().refresh (juce::Time::getMillisecondCounterHiRes());
    editor->view().refresh (juce::Time::getMillisecondCounterHiRes() + 200);

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os (out);
    png.writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
