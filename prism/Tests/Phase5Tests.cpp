// Phase 5: GUI - editor lifetime vs audio, idle CPU, live modulation edits.
#include <catch2/catch_test_macros.hpp>
#include "TestHelpers.h"
#include "PluginEditor.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <ctime>
static double threadCpuMs() { timespec t; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
#include <ctime>

namespace prism { extern std::atomic<int> gEditorPaints; }
using namespace prism;
using namespace prism::test;

static bool haveDisplay()
{
#if JUCE_LINUX
    return std::getenv ("DISPLAY") != nullptr;
#else
    return true;
#endif
}

// Renders `blocks` blocks of 128 samples with a fixed chord, optionally from another thread
static juce::AudioBuffer<float> renderBlocks (PrismProcessor& p, int blocks, std::atomic<double>* maxBlockMs = nullptr)
{
    juce::AudioBuffer<float> out (2, blocks * 128);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0) for (int n : { 60, 64, 67, 71 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
        juce::AudioBuffer<float> blk (out.getArrayOfWritePointers(), 2, b * 128, 128);
        const double c0 = threadCpuMs();
        p.processBlock (blk, midi);
        const double ms = threadCpuMs() - c0; // CPU time of the audio thread (excludes OS preemption)
        if (maxBlockMs != nullptr && ms > maxBlockMs->load()) maxBlockMs->store (ms);
    }
    return out;
}

TEST_CASE ("Editor created and destroyed 200 times while audio renders: output unchanged", "[phase5][gui]")
{
    if (! haveDisplay()) { WARN ("no display - skipped"); return; }
    const int blocks = 3000;
    auto ref = makeProcessor (48000.0, 128);
    ref->setNonRealtime (false);
    const auto a = renderBlocks (*ref, blocks);

    auto p = makeProcessor (48000.0, 128);
    p->setNonRealtime (false);
    juce::AudioBuffer<float> b;
    std::atomic<bool> done { false };
    std::thread audio ([&] { b = renderBlocks (*p, blocks); done = true; });
    int created = 0;
    while (created < 200)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        if (auto* pe = dynamic_cast<PrismEditor*> (ed.get()))
        {
            pe->showTab (created % 6);
            pe->tickForTest();
        }
        ed.reset();
        ++created;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    audio.join();
    REQUIRE (created == 200);
    REQUIRE (maxAbsDiff (a, b) == 0.0);
}

TEST_CASE ("Idle editor uses less than 5% of one core", "[phase5][gui]")
{
    if (! haveDisplay()) { WARN ("no display - skipped"); return; }
    auto p = makeProcessor();
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->addToDesktop (0);
    ed->setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500); // settle first paint
    timespec c0, c1;
    clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &c0);
    const auto w0 = std::chrono::steady_clock::now();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (4000);
    clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &c1);
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - w0).count();
    const double cpu = (c1.tv_sec - c0.tv_sec) + (c1.tv_nsec - c0.tv_nsec) * 1e-9;
    std::printf ("  idle editor: %.2f %% of one core (%d editor paints)\n", 100.0 * cpu / wall, gEditorPaints.load());
    ed->removeFromDesktop();
    ed.reset();
    REQUIRE (cpu / wall < 0.05);
}

TEST_CASE ("Dragging modulation while playing causes no dropouts", "[phase5][gui]")
{
    auto p = makeProcessor (48000.0, 128);
    p->setNonRealtime (false);
    std::atomic<double> maxMs { 0.0 };
    juce::AudioBuffer<float> out;
    std::thread audio ([&] { out = renderBlocks (*p, 4000, &maxMs); });
    // the GUI side: drop sources on knobs, move depths, remove them again
    juce::Random r (99);
    for (int i = 0; i < 400; ++i)
    {
        const int slot = p->addRouting ((int) Src::Lfo1 + r.nextInt (4), P::osc (0, P::wt_pos) + r.nextInt (3), r.nextFloat() - 0.5f);
        p->param (P::modDepth (std::max (0, slot)))->setValueNotifyingHost (r.nextFloat());
        p->param (P::filt (0, P::f_cutoff))->setValueNotifyingHost (r.nextFloat());
        if (slot >= 2 && (i % 3) == 0) p->removeRouting (slot);
        std::this_thread::sleep_for (std::chrono::microseconds (500));
    }
    audio.join();
    bool finite = true;
    for (int c = 0; c < 2; ++c) for (int s = 0; s < out.getNumSamples(); ++s) finite &= std::isfinite (out.getSample (c, s));
    std::printf ("  worst block time during edits: %.3f ms (block = 2.667 ms)\n", maxMs.load());
    REQUIRE (finite);
    REQUIRE (maxMs.load() < 2.667);
}
