#include "PluginProcessor.h"

#include "Params/Parameters.h"
#include "State/StateIO.h"

namespace lens
{
LensProcessor::LensProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LENS", params::createLayout())
{
    bypassParam = apvts.getParameter (params::id::bypass);
    jassert (bypassParam != nullptr);
}

void LensProcessor::prepareToPlay (double, int)
{
    setLatencySamples (0);
}

void LensProcessor::releaseResources() {}

bool LensProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out || (in != mono && in != stereo))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses.getReference (1);
        if (! sc.isDisabled() && sc != mono && sc != stereo)
            return false;
    }

    return true;
}

void LensProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Phase 1: the main bus passes through untouched. Main in and out
    // share the buffer's first channels, so there is nothing to do.
}

juce::AudioProcessorEditor* LensProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

int LensProcessor::activeSlot() const
{
    return apvts.getRawParameterValue (params::id::abSlot)->load() >= 0.5f ? 1 : 0;
}

state::SessionState LensProcessor::getSession() const
{
    const juce::ScopedLock sl (sessionLock);
    return session;
}

void LensProcessor::setSession (const state::SessionState& s)
{
    const juce::ScopedLock sl (sessionLock);
    session = s;
}

juce::String LensProcessor::getPresetJson() const
{
    auto* root = new juce::DynamicObject();
    juce::var holder (root);

    root->setProperty ("format_version", state::currentFormatVersion);
    getSession().writePresetMeta (*root);
    root->setProperty ("bands", state::bandsToVar (apvts, true));
    root->setProperty ("globals", state::globalsToVar (apvts, true));

    return juce::JSON::toString (holder);
}

bool LensProcessor::setPresetJson (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    if (! state::migrate (root))
        return false;

    auto s = getSession();
    s.readPresetMeta (root);
    setSession (s);

    state::applyGlobals (apvts, root["globals"], true);
    state::applyBands (apvts, root["bands"]);
    return true;
}

juce::String LensProcessor::getStateJson() const
{
    auto* root = new juce::DynamicObject();
    juce::var holder (root);
    const auto s = getSession();
    const int active = activeSlot();

    root->setProperty ("format_version", state::currentFormatVersion);
    s.writePresetMeta (*root);

    const auto liveBands = state::bandsToVar (apvts, false);
    root->setProperty ("bands", liveBands);
    root->setProperty ("globals", state::globalsToVar (apvts, false));

    auto* ab = new juce::DynamicObject();
    for (int i = 0; i < 2; ++i)
    {
        const auto& stored = s.abBands[i];
        ab->setProperty (i == 0 ? "A" : "B",
                         i == active ? liveBands
                                     : (stored.isArray() ? stored : juce::var (juce::Array<juce::var>())));
    }
    root->setProperty ("ab", juce::var (ab));

    s.writeStateExtras (*root);

    return juce::JSON::toString (holder);
}

bool LensProcessor::setStateJson (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    if (! state::migrate (root))
        return false;

    auto s = getSession();
    s.readPresetMeta (root);
    s.readStateExtras (root);

    const auto& ab = root["ab"];
    s.abBands[0] = ab["A"].isArray() ? ab["A"] : juce::var();
    s.abBands[1] = ab["B"].isArray() ? ab["B"] : juce::var();
    setSession (s);

    state::applyGlobals (apvts, root["globals"], false);

    // "bands" is the live setup; fall back to the active slot's copy.
    const auto& bands = root["bands"].isArray() ? root["bands"] : s.abBands[activeSlot()];
    state::applyBands (apvts, bands);
    return true;
}

void LensProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto json = getStateJson();
    dest.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void LensProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;
    setStateJson (juce::String::fromUTF8 (static_cast<const char*> (data), size));
}
} // namespace lens

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lens::LensProcessor();
}
