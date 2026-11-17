#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Params/Parameters.h"
#include "PluginProcessor.h"

using Catch::Approx;
namespace P = lens::params;

namespace
{
float def (const lens::LensProcessor& p, const juce::String& id)
{
    auto* param = p.getApvts().getParameter (id);
    REQUIRE (param != nullptr);
    return param->convertFrom0to1 (param->getDefaultValue());
}
} // namespace

TEST_CASE ("Every SPEC §10 parameter exists with version hint 1", "[params]")
{
    lens::LensProcessor proc;
    auto& apvts = proc.getApvts();

    REQUIRE (P::bandSuffixes().size() == 21);
    REQUIRE (P::globalIds().size() == 9);
    REQUIRE (proc.getParameters().size() == 24 * 21 + 9);

    for (const auto& gid : P::globalIds())
    {
        INFO (gid);
        REQUIRE (apvts.getParameter (gid) != nullptr);
    }

    for (int b = 1; b <= P::numBands; ++b)
        for (const auto& s : P::bandSuffixes())
        {
            INFO (P::bandParamId (b, s));
            REQUIRE (apvts.getParameter (P::bandParamId (b, s)) != nullptr);
        }

    for (auto* p : proc.getParameters())
        REQUIRE (p->getVersionHint() == 1);

    REQUIRE (P::bandParamId (1, P::band::freq) == "b01_freq");
    REQUIRE (P::bandParamId (24, P::band::specAmount) == "b24_spec_amount");
}

TEST_CASE ("Parameter defaults and ranges match SPEC §10", "[params]")
{
    lens::LensProcessor proc;
    auto& apvts = proc.getApvts();

    for (int b = 1; b <= P::numBands; ++b)
    {
        auto id = [b] (const char* s) { return P::bandParamId (b, s); };
        REQUIRE (def (proc, id (P::band::on)) == 0.0f);
        REQUIRE (def (proc, id (P::band::shape)) == 0.0f);
        REQUIRE (def (proc, id (P::band::freq)) == Approx (50.0 * std::pow (2.0, (b - 1) / 3.0)).epsilon (1e-4));
        REQUIRE (def (proc, id (P::band::gain)) == Approx (0.0f).margin (1e-5));
        REQUIRE (def (proc, id (P::band::q)) == Approx (1.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::slope)) == 1.0f); // 12 dB/oct
        REQUIRE (def (proc, id (P::band::stereo)) == 0.0f);
        REQUIRE (def (proc, id (P::band::dynOn)) == 0.0f);
        REQUIRE (def (proc, id (P::band::dynMode)) == 0.0f);
        REQUIRE (def (proc, id (P::band::dynRange)) == Approx (-6.0f));
        REQUIRE (def (proc, id (P::band::dynThresh)) == Approx (-20.0f));
        REQUIRE (def (proc, id (P::band::dynRatio)) == Approx (3.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::dynAttack)) == Approx (10.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::dynRelease)) == Approx (120.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::detSource)) == 0.0f);
        REQUIRE (def (proc, id (P::band::detCustom)) == 0.0f);
        REQUIRE (def (proc, id (P::band::detHp)) == Approx (20.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::detLp)) == Approx (20000.0f).epsilon (1e-5));
        REQUIRE (def (proc, id (P::band::specOn)) == 0.0f);
        REQUIRE (def (proc, id (P::band::specThresh)) == Approx (6.0f));
        REQUIRE (def (proc, id (P::band::specAmount)) == Approx (12.0f));
    }

    REQUIRE (def (proc, P::id::phaseMode) == 0.0f);
    REQUIRE (def (proc, P::id::lpResolution) == 1.0f);
    REQUIRE (def (proc, P::id::character) == 0.0f);
    REQUIRE (def (proc, P::id::outputGain) == Approx (0.0f).margin (1e-5));
    REQUIRE (def (proc, P::id::autoGain) == 1.0f);
    REQUIRE (def (proc, P::id::delta) == 0.0f);
    REQUIRE (def (proc, P::id::bypass) == 0.0f);
    REQUIRE (def (proc, P::id::abSlot) == 0.0f);
    REQUIRE (def (proc, P::id::lookahead) == 0.0f);

    auto range = [&] (const juce::String& id) { return apvts.getParameter (id)->getNormalisableRange(); };
    auto checkRange = [&] (const juce::String& id, float lo, float hi)
    {
        INFO (id);
        auto r = range (id);
        REQUIRE (r.start == lo);
        REQUIRE (r.end == hi);
        REQUIRE (r.convertFrom0to1 (0.0f) == Approx (lo).epsilon (1e-5));
        REQUIRE (r.convertFrom0to1 (1.0f) == Approx (hi).epsilon (1e-5));
    };
    checkRange ("b01_freq", 10.0f, 30000.0f);
    checkRange ("b01_gain", -30.0f, 30.0f);
    checkRange ("b01_q", 0.025f, 40.0f);
    checkRange ("b01_dyn_range", -30.0f, 30.0f);
    checkRange ("b01_dyn_thresh", -60.0f, 0.0f);
    checkRange ("b01_dyn_ratio", 1.5f, 10.0f);
    checkRange ("b01_dyn_attack", 0.1f, 500.0f);
    checkRange ("b01_dyn_release", 10.0f, 5000.0f);
    checkRange ("b01_det_hp", 10.0f, 30000.0f);
    checkRange ("b01_det_lp", 10.0f, 30000.0f);
    checkRange ("b01_spec_thresh", 0.0f, 24.0f);
    checkRange ("b01_spec_amount", 0.0f, 24.0f);
    checkRange ("output_gain", -30.0f, 30.0f);
    checkRange ("lookahead", 0.0f, 10.0f);

    // Log ranges sit at the geometric mean halfway; ratio's midpoint is 3.
    REQUIRE (range ("b01_freq").convertFrom0to1 (0.5f) == Approx (std::sqrt (10.0 * 30000.0)).epsilon (1e-4));
    REQUIRE (range ("b01_dyn_ratio").convertFrom0to1 (0.5f) == Approx (3.0f).epsilon (1e-4));

    auto* shape = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b01_shape"));
    auto* slope = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("b01_slope"));
    REQUIRE (shape != nullptr);
    REQUIRE (slope != nullptr);
    REQUIRE (shape->choices.size() == 9);
    REQUIRE (slope->choices.size() == 9);
    REQUIRE (P::choiceToken (*shape, 1) == "LowShelf");
    REQUIRE (P::choiceToken (*shape, 8) == "AllPass");
    REQUIRE (P::choiceToken (*slope, 1) == "12");
    REQUIRE (P::choiceToken (*slope, 8) == "Brickwall");
    REQUIRE (P::choiceIndexForToken (*slope, "96") == 7);
    REQUIRE (P::choiceIndexForToken (*slope, "13") == -1);
}

TEST_CASE ("Parameters are grouped Global, then Band 01 … Band 24", "[params]")
{
    lens::LensProcessor proc;
    const auto& tree = proc.getParameterTree();
    auto groups = tree.getSubgroups (false);

    REQUIRE (groups.size() == 25);
    REQUIRE (groups[0]->getName() == "Global");
    REQUIRE (groups[0]->getParameters (false).size() == 9);
    for (int b = 1; b <= 24; ++b)
    {
        REQUIRE (groups[b]->getName() == "Band " + juce::String (b).paddedLeft ('0', 2));
        REQUIRE (groups[b]->getParameters (false).size() == 21);
    }
}

TEST_CASE ("Bus layouts: mono or stereo main, optional mono/stereo side-chain", "[buses]")
{
    lens::LensProcessor proc;
    using Set = juce::AudioChannelSet;
    auto layout = [] (Set in, Set out, Set sc)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (in);
        l.outputBuses.add (out);
        l.inputBuses.add (sc);
        return l;
    };

    REQUIRE (proc.getBusCount (true) == 2);
    REQUIRE (proc.getBusCount (false) == 1);

    for (auto main : { Set::mono(), Set::stereo() })
        for (auto sc : { Set::disabled(), Set::mono(), Set::stereo() })
            REQUIRE (proc.checkBusesLayoutSupported (layout (main, main, sc)));

    REQUIRE_FALSE (proc.checkBusesLayoutSupported (layout (Set::mono(), Set::stereo(), Set::disabled())));
    REQUIRE_FALSE (proc.checkBusesLayoutSupported (layout (Set::stereo(), Set::mono(), Set::disabled())));
    REQUIRE_FALSE (proc.checkBusesLayoutSupported (layout (Set::createLCR(), Set::createLCR(), Set::disabled())));
    REQUIRE_FALSE (proc.checkBusesLayoutSupported (layout (Set::stereo(), Set::stereo(), Set::createLCR())));

    REQUIRE (proc.getBypassParameter() == proc.getApvts().getParameter ("bypass"));
    REQUIRE (proc.getTailLengthSeconds() == 1.0);
}
