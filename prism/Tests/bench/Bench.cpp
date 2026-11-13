// Headless benchmark: renders N seconds offline and prints the real-time factor
// and the CPU model. Usage: PrismBench [init|heavy|idle] [seconds]
#include "../TestHelpers.h"
#include <chrono>
#include <fstream>
#include <iostream>
#if JUCE_MAC
 #include <sys/sysctl.h>
#endif

using namespace prism;
using namespace prism::test;

static std::string cpuModel()
{
#if JUCE_MAC
    char buf[256] = {}; size_t len = sizeof (buf);
    if (sysctlbyname ("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0) return buf;
    return juce::SystemStats::getCpuModel().toStdString();
#else
    std::ifstream f ("/proc/cpuinfo");
    std::string line;
    while (std::getline (f, line))
        if (line.rfind ("model name", 0) == 0) return line.substr (line.find (':') + 2);
    return juce::SystemStats::getCpuModel().toStdString();
#endif
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string which = argc > 1 ? argv[1] : "init";
    const double seconds = argc > 2 ? std::atof (argv[2]) : 10.0;
    const double sr = 48000.0;
    const int block = 128;
    auto p = makeProcessor (sr, block);
    std::vector<Note> notes;
    if (which == "init")
    {
        for (int i = 0; i < 16; ++i) notes.push_back ({ 0.0, seconds + 1.0, 36 + i * 3, 0.8f });
    }
    else if (which == "heavy")
    {
        setParam (*p, "oscA_unison", 8); setParam (*p, "oscB_unison", 8);
        setParam (*p, "oscB_level", 0.5f);
        setParam (*p, "f1_type", (float) FilterType::Ladder); setParam (*p, "f1_cutoff", 1500.0f); setParam (*p, "f1_drive", 9.0f);
        setParam (*p, "f2_enable", 1.0f); setParam (*p, "f2_type", (float) FilterType::LP24); setParam (*p, "f2_cutoff", 6000.0f); setParam (*p, "f2_drive", 6.0f);
        setParam (*p, "oscA_wt_pos", 0.5f); setParam (*p, "oscB_wt_pos", 0.3f);
        setParam (*p, "reverb_on", 1); setParam (*p, "delay_on", 1); setParam (*p, "chorus_on", 1); setParam (*p, "tape_on", 1);
        TableSource t; t.factory = "Saw Sweep"; p->setTableSource (0, t);
        TableSource u; u.factory = "Vocal Ah"; p->setTableSource (1, u);
        waitForTables (*p);
        for (int i = 0; i < 8; ++i) notes.push_back ({ 0.0, seconds + 1.0, 48 + i * 4, 0.8f });
    }
    else if (which == "idle")
    {
        setParam (*p, "reverb_on", 1); setParam (*p, "delay_on", 1); setParam (*p, "chorus_on", 1); setParam (*p, "tape_on", 1);
    }
    // warm-up
    RenderOptions o; o.blocks = { block }; o.sampleRate = sr;
    render (*p, {}, 0.2, o);
    const auto t0 = std::chrono::steady_clock::now();
    auto b = render (*p, notes, seconds, o);
    const auto t1 = std::chrono::steady_clock::now();
    const double cpu = std::chrono::duration<double> (t1 - t0).count();
    std::cout << "PRISM benchmark '" << which << "'  " << seconds << " s @ " << sr << " Hz, block " << block << "\n";
    std::cout << "CPU: " << cpuModel() << "\n";
    std::cout << "render time " << cpu << " s  ->  " << (100.0 * cpu / seconds) << " % of one core (RT factor " << (seconds / cpu) << "x)\n";
    std::cout << "output peak " << peak (b) << "\n";
    return 0;
}
