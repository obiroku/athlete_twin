// Phase 3: editor lifecycle, threading and idle cost.
#include <catch2/catch_test_macros.hpp>

#include "GUI/Editor.h"
#include "helpers/OfflineRender.h"
#include "helpers/ProcHelpers.h"
#include "helpers/Signals.h"

#include <sys/resource.h>
#include <atomic>
#include <thread>

using namespace lens::test;

namespace
{
double cpuSeconds()
{
    rusage u {};
    getrusage (RUSAGE_SELF, &u);
    return (double) u.ru_utime.tv_sec + u.ru_utime.tv_usec * 1e-6 + (double) u.ru_stime.tv_sec + u.ru_stime.tv_usec * 1e-6;
}
} // namespace

TEST_CASE ("Creating and destroying the editor 200 times while audio renders never glitches", "[editor]")
{
    lens::LensProcessor proc;
    setEightMixedBands (proc);
    setDyn (proc, 3, {});
    proc.setPlayConfigDetails (2, 2, 48000.0, 256);
    proc.prepareToPlay (48000.0, 256);

    // Reference: the same input rendered with no editor ever created.
    const int blocks = 3000;
    const auto input = makePink (2, 256 * blocks, 5, -18.0);

    auto renderAll = [&] (lens::LensProcessor& p, std::atomic<bool>& done)
    {
        juce::AudioBuffer<float> out (2, 256 * blocks);
        juce::AudioBuffer<float> block (4, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            block.clear();
            block.copyFrom (0, 0, input, 0, b * 256, 256);
            block.copyFrom (1, 0, input, 1, b * 256, 256);
            p.processBlock (block, midi);
            out.copyFrom (0, b * 256, block, 0, 0, 256);
            out.copyFrom (1, b * 256, block, 1, 0, 256);
        }
        done = true;
        return out;
    };

    lens::LensProcessor ref;
    setEightMixedBands (ref);
    setDyn (ref, 3, {});
    ref.setPlayConfigDetails (2, 2, 48000.0, 256);
    ref.prepareToPlay (48000.0, 256);
    std::atomic<bool> refDone { false };
    const auto expected = renderAll (ref, refDone);

    std::atomic<bool> done { false };
    juce::AudioBuffer<float> got;
    std::thread audio ([&] { got = renderAll (proc, done); });

    int created = 0;
    while (created < 200)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1000, 650);
        if (auto* e = dynamic_cast<lens::gui::LensEditor*> (ed.get()))
            e->view().refresh (juce::Time::getMillisecondCounterHiRes());
        ed.reset();
        ++created;
    }
    audio.join();
    REQUIRE (created == 200);
    // Opening or closing the editor never affects audio: bit-identical.
    REQUIRE (bitIdentical (got, expected));
}

TEST_CASE ("An idle editor uses less than 5% of one core", "[editor][idle]")
{
    lens::LensProcessor proc;
    setEightMixedBands (proc);
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (1000, 650);
    // On a real desktop (the Mac), set LENS_TEST_ONSCREEN=1 to measure the
    // editor in a window with vblank repaints running.
    const bool onScreen = juce::SystemStats::getEnvironmentVariable ("LENS_TEST_ONSCREEN", {}).isNotEmpty();
    if (onScreen)
    {
        ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        ed->setVisible (true);
    }

    // Run the real message loop for 3 s (after 0.3 s to settle).
    struct Stopper : juce::Timer
    {
        double c0 = 0.0, t0 = 0.0, used = 0.0, wall = 0.0;
        int ticks = 0;
        void timerCallback() override
        {
            if (++ticks == 1)
            {
                c0 = cpuSeconds();
                t0 = juce::Time::getMillisecondCounterHiRes();
                startTimer (3000);
                return;
            }
            used = cpuSeconds() - c0;
            wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    } stopper;
    stopper.startTimer (300);
    juce::MessageManager::getInstance()->runDispatchLoop();
    const double used = stopper.used, wall = stopper.wall;
    const double pct = 100.0 * used / wall;
    WARN ("Idle editor (" << (onScreen ? "on screen" : "headless") << "): " << pct << "% of one core");
    REQUIRE (pct < 5.0);
    ed.reset();
}
