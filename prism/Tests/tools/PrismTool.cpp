// Developer tool. Usage:
//   PrismTool peaks <warp> <amt> <quality> [note]   - strongest spectral peaks of a warped saw
//   PrismTool render <preset name> <out.wav>         - render the C3-E3-G3 test chord
#include "../TestHelpers.h"
#include "PluginEditor.h"
#include <cstdio>
#include <iostream>
using namespace prism;
using namespace prism::test;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "peaks" && argc >= 5)
    {
        const int warp = std::atoi (argv[2]);
        const float amt = (float) std::atof (argv[3]);
        const int q = std::atoi (argv[4]);
        const int note = argc > 5 ? std::atoi (argv[5]) : 84;
        auto p = makeProcessor();
        TableSource t; t.factory = "Saw Sweep"; p->setTableSource (0, t); p->setTableSource (1, t); waitForTables (*p);
        setParam (*p, "oscA_wt_pos", 1); setParam (*p, "oscA_rand_phase", 0); setParam (*p, "drift", 0); setParam (*p, "f1_enable", 0);
        setParam (*p, "oscA_warp_mode", (float) warp); setParam (*p, "oscA_warp_amt", amt); setParam (*p, "quality", (float) q);
        setParam (*p, "env1_vel_amt", 0);
        auto b = render (*p, { { 0.0, 2.0, note, 1.0f } }, 1.0);
        auto db = spectrumDb (b.getReadPointer (0) + 9600, 32768);
        const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        std::vector<std::pair<double, int>> pk;
        for (size_t k = 2; k + 1 < db.size(); ++k) if (db[k] > db[k - 1] && db[k] >= db[k + 1]) pk.push_back ({ db[k], (int) k });
        std::sort (pk.rbegin(), pk.rend());
        // worst non-harmonic bins
        std::vector<std::pair<double, int>> nh;
        for (auto& [d, k] : pk)
        {
            const double h = k * 48000.0 / 32768 / f0;
            if (std::abs (h - std::round (h)) * f0 > 12.0) nh.push_back ({ d, k });
        }
        for (int i = 0; i < 8 && i < (int) nh.size(); ++i)
            std::printf ("NONHARM %9.1f Hz %7.1f dB  (h = %.3f)\n", nh[(size_t) i].second * 48000.0 / 32768, nh[(size_t) i].first, nh[(size_t) i].second * 48000.0 / 32768 / f0);
        for (int i = 0; i < 20 && i < (int) pk.size(); ++i)
            std::printf ("%9.1f Hz %7.1f dB  (h = %.3f)\n", pk[(size_t) i].second * 48000.0 / 32768, pk[(size_t) i].first, pk[(size_t) i].second * 48000.0 / 32768 / f0);
        return 0;
    }
    if (cmd == "snapshot" && argc >= 4)
    {
        auto p = makeProcessor();
        render (*p, { { 0.0, 2.0, 60, 0.8f }, { 0.0, 2.0, 67, 0.7f } }, 0.5);
        std::unique_ptr<PrismEditor> ed (dynamic_cast<PrismEditor*> (p->createEditor()));
        if (argc > 4) ed->setScale (std::atof (argv[4]));
        ed->showTab (std::atoi (argv[3]));
        for (int i = 0; i < 3; ++i) ed->tickForTest();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        std::cout << "wrote " << out.getFullPathName() << "\n";
        return 0;
    }
    std::cout << "unknown command\n";
    return 1;
}
