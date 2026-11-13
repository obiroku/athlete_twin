// Developer tool: renders presets, measures loudness. Usage: PrismTool <command> ...
#include "../TestHelpers.h"
#include <iostream>
using namespace prism;
using namespace prism::test;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const std::string cmd = argc > 1 ? argv[1] : "";
    std::cout << "PrismTool: command '" << cmd << "' not implemented yet\n";
    return 0;
}
