// Phase 7: wavetable editor backend - formula parser, live edits, export/import.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "TestHelpers.h"
#include "Engine/Formula.h"
#include "Engine/TableIO.h"
#include "DSP/Wavetable.h"
#include "DSP/FactoryTables.h"
#include "DSP/Random.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <ctime>
static double threadCpuMs() { timespec t; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }

using namespace prism;
using namespace prism::test;

TEST_CASE ("Formula parser: values, functions, errors", "[phase7]")
{
    std::string err;
    auto f = Formula::parse ("sin(2*pi*x) + 0.5*y", err);
    REQUIRE (f != nullptr);
    REQUIRE_THAT (f->eval (0.25, 1.0), Catch::Matchers::WithinAbs (1.5, 1e-12));
    f = Formula::parse ("max(abs(-3), 2)^2 - floor(2.7) + sign(-4) + min(1,2)", err);
    REQUIRE (f != nullptr);
    REQUIRE_THAT (f->eval (0, 0), Catch::Matchers::WithinAbs (9.0 - 2.0 - 1.0 + 1.0, 1e-12));
    f = Formula::parse ("tri(0) + saw(0) + square(1)", err);
    REQUIRE (f != nullptr);
    REQUIRE_THAT (f->eval (0, 0), Catch::Matchers::WithinAbs (0.0 + 0.0 + 1.0, 1e-9));
    f = Formula::parse ("rand(3) - rand(3)", err);
    REQUIRE (f != nullptr);
    REQUIRE (f->eval (0, 0) == 0.0);
    f = Formula::parse ("1/0 + log(-1) + sqrt(-2) + pow(-8, 0.5)", err);
    REQUIRE (f != nullptr);
    REQUIRE (std::isfinite (f->eval (0.3, 0.3)));
    for (auto bad : { "sin(", "foo(x)", "2**3", "pow(1)", "(((x)", "x y", "", "rand(1,2)" })
    {
        INFO (bad);
        REQUIRE (Formula::parse (bad, err) == nullptr);
        REQUIRE_FALSE (err.empty());
    }
}

TEST_CASE ("Formula fuzz: 10,000 random expressions never produce NaN, inf or a crash", "[phase7][fuzz]")
{
    dsp::Rng rng (2024);
    const char* atoms[] = { "x", "y", "pi", "e", "0", "1", "2.5", "1e308", "-1e308", "0.0001", "1000" };
    const char* funcs1[] = { "sin", "cos", "tan", "abs", "sqrt", "exp", "log", "floor", "sign", "tri", "saw", "square", "rand" };
    const char* funcs2[] = { "pow", "min", "max" };
    const char* ops[] = { "+", "-", "*", "/", "%", "^" };
    const char* junk[] = { "(", ")", ",", "*", "^^", "sin", "x(", "#", "..", "1.2.3", "max(", "--", " " };
    std::function<std::string (int)> gen = [&] (int depth) -> std::string {
        const int k = depth > 6 ? 0 : (int) (rng.uniform() * 6.0f);
        switch (k)
        {
            case 0: return atoms[rng.next() % 11];
            case 1: return std::string (funcs1[rng.next() % 13]) + "(" + gen (depth + 1) + ")";
            case 2: return std::string (funcs2[rng.next() % 3]) + "(" + gen (depth + 1) + "," + gen (depth + 1) + ")";
            case 3: return "(" + gen (depth + 1) + ")";
            case 4: return "-" + gen (depth + 1);
            default: return gen (depth + 1) + ops[rng.next() % 6] + gen (depth + 1);
        }
    };
    int parsed = 0, rejected = 0;
    for (int i = 0; i < 10000; ++i)
    {
        std::string s = gen (0);
        if (i % 5 == 0) s.insert (rng.next() % (s.size() + 1), junk[rng.next() % 13]); // some malformed input
        if (i % 97 == 0) s = std::string (200, '(') + "x" + std::string (200, ')');       // deep nesting
        std::string err;
        auto f = Formula::parse (s, err);
        if (f == nullptr) { ++rejected; continue; }
        ++parsed;
        for (int k = 0; k < 16; ++k)
        {
            const double v = f->eval (rng.uniformD(), rng.uniformD());
            INFO (s);
            REQUIRE (std::isfinite (v));
        }
        if (i % 500 == 0)
        {
            auto frames = f->render (4);
            dsp::applyTableHygiene (frames);
            for (auto& fr : frames) for (float v : fr) REQUIRE (std::isfinite (v));
        }
    }
    std::printf ("  formula fuzz: %d parsed, %d rejected, all finite\n", parsed, rejected);
    REQUIRE (parsed > 5000);
}

TEST_CASE ("Wavetable edits apply without audio dropouts", "[phase7]")
{
    auto p = makeProcessor (48000.0, 128);
    p->setNonRealtime (false); // real-time: never waits for a table
    std::atomic<bool> stop { false };
    std::atomic<double> worst { 0.0 };
    juce::AudioBuffer<float> out (2, 128);
    std::thread audio ([&] {
        juce::MidiBuffer midi;
        bool first = true;
        while (! stop)
        {
            midi.clear();
            if (first) { midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); first = false; }
            const double c0 = threadCpuMs();
            p->processBlock (out, midi);
            worst = std::max (worst.load(), threadCpuMs() - c0); // CPU time of the audio thread (excludes OS preemption)
            for (int c = 0; c < 2; ++c) for (int s = 0; s < 128; ++s) if (! std::isfinite (out.getSample (c, s))) worst = 1e9;
        }
    });
    // formula / draw / morph / factory edits from the "GUI"
    for (int i = 0; i < 12; ++i)
    {
        std::string err;
        auto f = Formula::parse ("sin(2*pi*x*(1+" + std::to_string (i % 5) + "*y))", err);
        auto frames = f->render (32);
        dsp::applyTableHygiene (frames);
        TableSource s; s.name = "Formula"; s.frames = frames;
        p->setTableSource (i % 2, s);
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        TableSource t; t.factory = dsp::factoryTableNames()[(size_t) (i * 5) % 24];
        p->setTableSource ((i + 1) % 2, t);
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
        p->collectGarbage();
    }
    p->tables.waitUntilGenerated (0, 20000);
    p->tables.waitUntilGenerated (1, 20000);
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    stop = true;
    audio.join();
    p->collectGarbage();
    std::printf ("  table edits: worst block %.3f ms (block = 2.667 ms)\n", worst.load());
    REQUIRE (worst.load() < 2.667);
}

TEST_CASE ("An exported table re-imports identically", "[phase7]")
{
    std::string err;
    auto f = Formula::parse ("saw(2*pi*x) * (1-y) + square(2*pi*x) * y", err);
    REQUIRE (f != nullptr);
    auto frames = f->render (64);
    dsp::applyTableHygiene (frames);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("prism_export_test.wav");
    REQUIRE (exportWavetable (file, frames));
    auto r = importWavetable (file, ImportMode::Auto);
    REQUIRE (r.ok);
    REQUIRE (r.clmFrameSize == 2048);
    REQUIRE (r.frames.size() == frames.size());
    double md = 0;
    for (size_t k = 0; k < frames.size(); ++k)
        for (int i = 0; i < 2048; ++i) md = std::max (md, (double) std::abs (frames[k][(size_t) i] - r.frames[k][(size_t) i]));
    std::printf ("  export/re-import max difference %.2e\n", md);
    REQUIRE (md < 1e-4);
}
