#include "PluginProcessor.h"

#include "GUI/Editor.h"
#include "Params/Parameters.h"
#include "State/StateIO.h"


namespace lens
{
LensProcessor::LensProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LENS", params::createLayout()),
      reader (apvts)
{
    bypassParam = apvts.getParameter (params::id::bypass);
    jassert (bypassParam != nullptr);

    // Gestures on band parameters (knobs, display drags, Learn) become
    // undo steps.
    for (int b = 1; b <= dsp::kNumBands; ++b)
        for (const auto& suffix : params::bandSuffixes())
            apvts.getParameter (params::bandParamId (b, suffix))->addListener (this);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
        startTimerHz (30);
}

LensProcessor::~LensProcessor()
{
    stopTimer();
    for (int b = 1; b <= dsp::kNumBands; ++b)
        for (const auto& suffix : params::bandSuffixes())
            apvts.getParameter (params::bandParamId (b, suffix))->removeListener (this);
}

void LensProcessor::parameterGestureChanged (int, bool starting)
{
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    if (starting)
        beginEdit();
    else
        endEdit ("Edit band");
}

int LensProcessor::findFreeBand() const
{
    for (int b = 1; b <= dsp::kNumBands; ++b)
        if (apvts.getRawParameterValue (params::bandParamId (b, params::band::on))->load() < 0.5f)
            return b - 1;
    return -1;
}

void LensProcessor::setBandValue (int band, const char* suffix, float value)
{
    auto* p = apvts.getParameter (params::bandParamId (band + 1, suffix));
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float LensProcessor::bandValue (int band, const char* suffix) const
{
    return apvts.getRawParameterValue (params::bandParamId (band + 1, suffix))->load();
}

int LensProcessor::addBand (const dsp::BandParams& bp, const juce::String& undoName)
{
    const int b = findFreeBand();
    if (b < 0)
        return -1;
    const auto before = getBands();
    namespace pb = params::band;
    // Reset the slot to defaults first, then apply.
    for (const auto& suffix : params::bandSuffixes())
    {
        auto* p = apvts.getParameter (params::bandParamId (b + 1, suffix));
        p->setValueNotifyingHost (p->getDefaultValue());
    }
    setBandValue (b, pb::shape, (float) (int) bp.shape);
    setBandValue (b, pb::freq, bp.freq);
    setBandValue (b, pb::gain, bp.gain);
    setBandValue (b, pb::q, bp.q);
    setBandValue (b, pb::slope, (float) bp.slope);
    setBandValue (b, pb::stereo, (float) (int) bp.stereo);
    if (bp.dynOn)
    {
        setBandValue (b, pb::dynMode, (float) (int) bp.dynMode);
        setBandValue (b, pb::dynRange, bp.dynRange);
        setBandValue (b, pb::dynThresh, bp.dynThresh);
        setBandValue (b, pb::dynRatio, bp.dynRatio);
        setBandValue (b, pb::dynAttack, bp.dynAttack);
        setBandValue (b, pb::dynRelease, bp.dynRelease);
        setBandValue (b, pb::dynOn, 1.0f);
    }
    setBandValue (b, pb::on, 1.0f);
    editHistory.push (before, getBands(), undoName);
    return b;
}

void LensProcessor::removeBands (const juce::Array<int>& bands)
{
    if (bands.isEmpty())
        return;
    const auto before = getBands();
    for (int b : bands)
        if (b >= 0 && b < dsp::kNumBands)
            setBandValue (b, params::band::on, 0.0f);
    editHistory.push (before, getBands(), "Delete band");
}

void LensProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int mainCh = juce::jmax (1, getMainBusNumInputChannels());
    const int scCh = hasSidechainBus() ? getBus (true, 1)->getNumberOfChannels() : 0;

    reader.read (blockParams);
    engine.setParams (blockParams);
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock), mainCh, scCh);
    setLatencySamples (engine.latencySamples());
    preparedFlag = true;
}

void LensProcessor::releaseResources()
{
    preparedFlag = false;
}

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

bool LensProcessor::isMono() const
{
    return getMainBusNumInputChannels() < 2;
}

bool LensProcessor::hasSidechainBus() const
{
    auto* bus = getBus (true, 1);
    return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
}

double LensProcessor::getTailLengthSeconds() const
{
    return engine.stats().tailSeconds.load();
}

void LensProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void LensProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void LensProcessor::process (juce::AudioBuffer<float>& buffer, bool forceBypass) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const double startMs = juce::Time::getMillisecondCounterHiRes();
    const int n = buffer.getNumSamples();

    const int mainCh = juce::jmin (getMainBusNumInputChannels(), buffer.getNumChannels());
    if (! preparedFlag.load (std::memory_order_relaxed) || mainCh <= 0 || n <= 0)
        return;

    reader.read (blockParams);
    if (forceBypass)
        blockParams.global.bypass = true;

    engine.setSoloBand (soloBand.load (std::memory_order_relaxed));
    engine.setListenBand (listenBand.load (std::memory_order_relaxed));
    engine.setEarBand (earTrainer.on.load(), earTrainer.freq.load(), earTrainer.gainDb.load(), earTrainer.q.load());
    engine.setParams (blockParams);

    Command c;
    while (commands.pop (c))
    {
        if (c == Command::BeginSwap)
            engine.beginSwap();
        else
            engine.endSwap();
    }

    const float* scPtrs[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
    {
        auto* bus = getBus (true, 1);
        if (bus != nullptr && bus->isEnabled())
        {
            const int first = bus->getChannelIndexInProcessBlockBuffer (0);
            scCh = juce::jmin (2, bus->getNumberOfChannels(), buffer.getNumChannels() - first);
            for (int i = 0; i < scCh; ++i)
                scPtrs[i] = buffer.getReadPointer (first + i);
        }
    }

    engine.process (buffer.getArrayOfWritePointers(), mainCh, scPtrs, scCh, n);

    // Clear any extra output channels.
    for (int ch = mainCh; ch < getTotalNumOutputChannels() && ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    const double endMs = juce::Time::getMillisecondCounterHiRes();
    const double blockMs = 1000.0 * n / juce::jmax (1.0, getSampleRate());
    const float load = (float) ((endMs - startMs) / juce::jmax (1.0e-6, blockMs));
    cpuLoadValue.store (cpuLoadValue.load() * 0.9f + load * 0.1f);
    lastProcessMs.store (endMs);
}

juce::AudioProcessorEditor* LensProcessor::createEditor()
{
    return new gui::LensEditor (*this);
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

// ---------------------------------------------------------------- JSON

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

bool LensProcessor::loadPreset (const juce::String& json)
{
    auto root = juce::JSON::parse (json);
    if (! state::migrate (root))
        return false;

    auto s = getSession();
    s.readPresetMeta (root);
    setSession (s);

    requestSwap (root["bands"].isArray() ? root["bands"] : juce::var (juce::Array<juce::var>()), root["globals"], "Load preset");
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
    lastSlot = activeSlot();
    pendingSwap = {};
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

// ---------------------------------------------------------------- Edits

juce::var LensProcessor::getBands() const
{
    return state::bandsToVar (apvts, false);
}

void LensProcessor::applyBands (const juce::var& bands)
{
    state::applyBands (apvts, bands);
}

void LensProcessor::beginEdit()
{
    if (editDepth++ == 0)
        editBefore = getBands();
}

void LensProcessor::endEdit (const juce::String& name)
{
    if (editDepth <= 0)
        return;
    if (--editDepth == 0)
        editHistory.push (editBefore, getBands(), name);
}

void LensProcessor::pushUndo (const juce::var& before, const juce::String& name)
{
    // Records before → current. Used for one-shot actions.
    editHistory.push (before, getBands(), name);
}

bool LensProcessor::undo()
{
    const auto v = editHistory.undo();
    if (v.isVoid())
        return false;
    applyBands (v);
    return true;
}

bool LensProcessor::redo()
{
    const auto v = editHistory.redo();
    if (v.isVoid())
        return false;
    applyBands (v);
    return true;
}

// ---------------------------------------------------------------- A/B

void LensProcessor::copyActiveToOther()
{
    auto s = getSession();
    s.abBands[1 - activeSlot()] = getBands();
    setSession (s);
}

void LensProcessor::copyOtherToActive()
{
    const auto s = getSession();
    const auto& other = s.abBands[1 - activeSlot()];
    if (other.isArray())
        requestSwap (other, {}, "Copy A/B");
}

void LensProcessor::requestSwap (const juce::var& bands, const juce::var& globals, const juce::String& undoName)
{
    if (undoName.isNotEmpty() && ! pendingSwap.recordUndo)
    {
        pendingSwap.recordUndo = true;
        pendingSwap.undoBefore = getBands();
        pendingSwap.undoName = undoName;
    }
    pendingSwap.active = true;
    pendingSwap.bands = bands;
    pendingSwap.globals = globals;
    if (! pendingSwap.sent)
    {
        pendingSwap.sent = true;
        pendingSwap.startMs = juce::Time::getMillisecondCounterHiRes();
        engine.stats().swapHolding.store (false);
        commands.push (Command::BeginSwap);
    }
}

void LensProcessor::serviceSwap()
{
    if (! pendingSwap.active)
        return;

    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool audioRunning = preparedFlag.load() && now - lastProcessMs.load() < 100.0;
    const bool holding = engine.stats().swapHolding.load();
    if (audioRunning && ! holding && now - pendingSwap.startMs < 150.0)
        return;

    if (pendingSwap.globals.isObject())
        state::applyGlobals (apvts, pendingSwap.globals, true);
    state::applyBands (apvts, pendingSwap.bands);
    if (pendingSwap.recordUndo)
        editHistory.push (pendingSwap.undoBefore, getBands(), pendingSwap.undoName);
    pendingSwap = {};
    commands.push (Command::EndSwap);
}

void LensProcessor::serviceAB()
{
    const int slot = activeSlot();
    if (slot == lastSlot)
        return;

    auto s = getSession();
    s.abBands[lastSlot] = getBands();
    const auto target = s.abBands[slot].isArray() ? s.abBands[slot] : s.abBands[lastSlot];
    setSession (s);
    lastSlot = slot;
    requestSwap (target, {});
}

// ---------------------------------------------------------------- Learn

void LensProcessor::toggleLearn (int b)
{
    if (b < 0 || b >= dsp::kNumBands)
        return;
    auto& det = engine.detector (b);
    const int st = det.learnState.load();
    if (st == dsp::Detector::LearnIdle || st == dsp::Detector::LearnDone)
    {
        learnBefore[(size_t) b] = getBands();
        learnPending[(size_t) b] = true;
        det.learnState.store (dsp::Detector::LearnRequested);
    }
    else if (st == dsp::Detector::LearnRecording)
    {
        det.learnState.store (dsp::Detector::LearnStopRequested);
    }
    else if (st == dsp::Detector::LearnRequested)
    {
        det.learnState.store (dsp::Detector::LearnIdle);
        learnPending[(size_t) b] = false;
    }
}

bool LensProcessor::isLearning (int b) const
{
    return b >= 0 && b < dsp::kNumBands && learnPending[(size_t) b];
}

float LensProcessor::learnProgress (int b) const
{
    if (! isLearning (b))
        return 0.0f;
    auto& det = const_cast<dsp::Engine&> (engine).detector (b);
    if (det.learnState.load() != dsp::Detector::LearnRecording || det.learnTargetSamples <= 0)
        return 0.0f;
    return (float) juce::jlimit (0.0, 1.0, (double) det.histogram.count / (double) det.learnTargetSamples);
}

void LensProcessor::serviceLearn()
{
    for (int b = 0; b < dsp::kNumBands; ++b)
    {
        if (! learnPending[(size_t) b])
            continue;
        auto& det = engine.detector (b);
        if (det.learnState.load (std::memory_order_acquire) != dsp::Detector::LearnDone)
            continue;

        learnPending[(size_t) b] = false;
        if (det.histogram.count > 0)
        {
            dsp::BandParams bp;
            reader.readBand (b, bp);
            // Act about 20% of the time.
            const double level = det.histogram.percentile (bp.dynMode == dsp::DynMode::Above ? 0.8 : 0.2);
            const double thresh = juce::jlimit (-60.0, 0.0, std::round (level * 2.0) / 2.0);
            auto* p = apvts.getParameter (params::bandParamId (b + 1, params::band::dynThresh));
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) thresh));
            p->endChangeGesture(); // the gesture records the undo step
        }
        det.learnState.store (dsp::Detector::LearnIdle);
        if (onLearnFinished)
            onLearnFinished (b);
    }
}

// ---------------------------------------------------------------- Timer

void LensProcessor::serviceMessageThread()
{
    serviceAB();
    serviceSwap();
    serviceLearn();

    const int lat = engine.stats().latencySamples.load();
    if (lat != getLatencySamples())
        setLatencySamples (lat);
}
} // namespace lens

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lens::LensProcessor();
}
