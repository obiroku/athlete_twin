#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include <thread>

namespace prism
{
juce::AudioProcessorValueTreeState::ParameterLayout PrismProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& defs = paramDefs();
    std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> groups;
    std::map<std::string, size_t> groupIndex;
    for (int i = 0; i < (int) defs.size(); ++i)
    {
        const auto& d = defs[(size_t) i];
        if (groupIndex.find (d.group) == groupIndex.end())
        {
            groupIndex[d.group] = groups.size();
            groups.push_back (std::make_unique<juce::AudioProcessorParameterGroup> (
                juce::String (d.group).removeCharacters (" /"), d.group, "|"));
        }
        auto& g = *groups[groupIndex[d.group]];
        const juce::ParameterID pid { d.id, 1 };
        const int idx = i;
        auto toText = [idx] (float v, int) { return juce::String (paramDefs()[(size_t) idx].toText (v)); };
        auto fromText = [idx] (const juce::String& s) {
            float v = paramDefs()[(size_t) idx].def;
            paramDefs()[(size_t) idx].fromText (s.toStdString(), v);
            return v;
        };
        switch (d.type)
        {
            case PType::Float:
            {
                juce::NormalisableRange<float> range (d.min, d.max,
                    [idx] (float, float, float n) { return paramDefs()[(size_t) idx].fromNorm (n); },
                    [idx] (float, float, float v) { return paramDefs()[(size_t) idx].toNorm (v); },
                    [] (float, float, float v) { return v; });
                g.addChild (std::make_unique<juce::AudioParameterFloat> (pid, d.name, range, d.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText).withValueFromStringFunction (fromText)));
                break;
            }
            case PType::Int:
                g.addChild (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) d.def,
                    juce::AudioParameterIntAttributes().withStringFromValueFunction (toText).withValueFromStringFunction ([fromText] (const juce::String& s) { return (int) std::lround (fromText (s)); })));
                break;
            case PType::Choice:
            {
                juce::StringArray ch;
                for (auto& c : d.choices) ch.add (c);
                g.addChild (std::make_unique<juce::AudioParameterChoice> (pid, d.name, ch, (int) d.def));
                break;
            }
            case PType::Bool:
                g.addChild (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def >= 0.5f));
                break;
        }
    }
    for (auto& g : groups) layout.add (std::move (g));
    return layout;
}

PrismProcessor::PrismProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PRISM", createLayout())
{
    const auto& defs = paramDefs();
    for (int i = 0; i < P::NumParams; ++i)
    {
        params[(size_t) i] = apvts.getParameter (defs[(size_t) i].id);
        raw[(size_t) i] = apvts.getRawParameterValue (defs[(size_t) i].id);
        jassert (params[(size_t) i] != nullptr && raw[(size_t) i] != nullptr);
    }
    engine.setParamSource (raw.data());
    for (int i = 0; i < 128; ++i) { ccMap[(size_t) i].store (-1); ccValue[(size_t) i].store (0.0f); ccDirty[(size_t) i].store (false); }
    guiScale = library.settings.guiScale;
    applyPresetNow (PresetData::init());
    setLatencySamples (engine.latencySamples());
    captureUndoSnapshot();
    startTimerHz (30);
}

PrismProcessor::~PrismProcessor()
{
    stopTimer();
}

bool PrismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() > 0 && ! layouts.getMainInputChannelSet().isDisabled()) return false;
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo();
}

void PrismProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    engine.prepare (sr, block);
    // pick up the latest extras immediately
    engine.setExtras (getExtras());
    fadeState = Idle; fadeGain = 1.0f;
    fadeStep = 1.0f / (float) std::max (1.0, 0.01 * sr);
    setLatencySamples (engine.latencySamples());
}

double PrismProcessor::getTailLengthSeconds() const
{
    return std::min (30.0, std::max (0.0, engine.tailSecs.load()));
}

void PrismProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    lastProcessMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
    const int n = buffer.getNumSamples();

    // GUI -> audio: newest PatchExtras
    {
        int r = fifoR.load (std::memory_order_relaxed);
        const int w = fifoW.load (std::memory_order_acquire);
        if (r != w)
        {
            int last = r;
            while (r != w) { last = r; r = (r + 1) % FifoCap; }
            engine.setExtras (fifo[(size_t) last]);
            fifoR.store (r, std::memory_order_release);
        }
    }

    // wavetables: offline bounces may wait for a table that is still generating
    for (int o = 0; o < 2; ++o)
    {
        if (isNonRealtime() && ! tables.isPublished (o))
            for (int i = 0; i < 5000 && ! tables.isPublished (o); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        engine.setTable (o, tables.acquire (o));
    }

    // MIDI learn
    {
        const int learn = midiLearnParam.load (std::memory_order_relaxed);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isController()) continue;
            const int cc = m.getControllerNumber();
            if (learn >= 0) { ccMap[(size_t) cc].store (learn); midiLearnParam.store (-1); }
            if (ccMap[(size_t) cc].load() >= 0) { ccValue[(size_t) cc].store ((float) m.getControllerValue() / 127.0f); ccDirty[(size_t) cc].store (true); }
        }
    }

    Engine::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tr.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) tr.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { tr.ppq = *ppq; tr.ppqValid = true; }
        }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (panicRequest.exchange (false)) engine.panic();
    if (R != nullptr)
        engine.process (L, R, n, midi, tr);

    // preset fade
    if (fadeState == Idle && fadeRequest.load()) fadeState = FadingOut;
    if (fadeState != Idle)
    {
        for (int i = 0; i < n; ++i)
        {
            if (fadeState == FadingOut)
            {
                fadeGain -= fadeStep;
                if (fadeGain <= 0.0f) { fadeGain = 0.0f; fadeState = Muted; audioMuted.store (true); }
            }
            else if (fadeState == Muted)
            {
                if (presetApplied.exchange (false))
                {
                    engine.resetTails();
                    audioMuted.store (false);
                    fadeState = FadingIn;
                }
            }
            else if (fadeState == FadingIn)
            {
                fadeGain += fadeStep;
                if (fadeGain >= 1.0f) { fadeGain = 1.0f; fadeState = Idle; }
            }
            L[i] *= fadeGain;
            if (R != nullptr) R[i] *= fadeGain;
        }
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    midi.clear();

    const auto t1 = juce::Time::getHighResolutionTicks();
    const double secs = juce::Time::highResolutionTicksToSeconds (t1 - t0);
    const double avail = (double) n / sampleRate;
    cpuLoad.store ((float) (secs / std::max (1e-9, avail)), std::memory_order_relaxed);
}

// ======================================================================= state

PresetData PrismProcessor::currentPreset() const
{
    PresetData p;
    {
        std::lock_guard<std::mutex> g (modelLock);
        p = model;
    }
    for (int i = 0; i < P::NumParams; ++i)
        p.params[(size_t) i] = paramDefs()[(size_t) i].snap (raw[(size_t) i]->load());
    p.guiScale = guiScale;
    return p;
}

void PrismProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const auto s = presetToString (currentPreset(), true);
    dest.replaceAll (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void PrismProcessor::setStateInformation (const void* data, int size)
{
    PresetData p;
    juce::String err;
    if (! presetFromString (juce::String::fromUTF8 ((const char*) data, size), p, err)) return;
    guiScale = p.guiScale;
    applyPresetNow (p);
}

void PrismProcessor::applyPresetNow (const PresetData& p)
{
    for (int i = 0; i < P::NumParams; ++i)
    {
        auto* prm = params[(size_t) i];
        const float norm = prm->convertTo0to1 (paramDefs()[(size_t) i].snap (p.params[(size_t) i]));
        if (std::abs (prm->getValue() - norm) > 1e-7f) prm->setValueNotifyingHost (norm);
    }
    {
        std::lock_guard<std::mutex> g (modelLock);
        const auto keepScale = model.guiScale;
        model = p;
        model.guiScale = keepScale;
    }
    pushExtras (p.extras);
    tables.request (0, p.tables[0]);
    tables.request (1, p.tables[1]);
}

void PrismProcessor::loadPreset (const PresetData& p, bool fade)
{
    const bool audioRunning = juce::Time::currentTimeMillis() - lastProcessMs.load() < 250;
    if (! fade || ! audioRunning)
    {
        applyPresetNow (p);
        return;
    }
    {
        std::lock_guard<std::mutex> g (modelLock);
        pendingPreset = std::make_unique<PresetData> (p);
    }
    fadeRequest.store (true);
}

void PrismProcessor::servicePresetLoad()
{
    std::unique_ptr<PresetData> p;
    {
        std::lock_guard<std::mutex> g (modelLock);
        if (pendingPreset == nullptr) return;
        const bool audioRunning = juce::Time::currentTimeMillis() - lastProcessMs.load() < 250;
        if (audioRunning && ! audioMuted.load()) return; // still fading out
        p = std::move (pendingPreset);
    }
    applyPresetNow (*p);
    fadeRequest.store (false);
    presetApplied.store (audioMuted.load());
    if (! audioMuted.load()) { /* audio was stopped: nothing to fade */ }
}

void PrismProcessor::pushExtras (const PatchExtras& x)
{
    std::lock_guard<std::mutex> g (fifoLock);
    const int w = fifoW.load (std::memory_order_relaxed);
    const int next = (w + 1) % FifoCap;
    if (next == fifoR.load (std::memory_order_acquire))
    {
        // full: overwrite the newest slot instead (the audio thread only uses the latest)
        const int prev = (w + FifoCap - 1) % FifoCap;
        fifo[(size_t) prev] = x;
        return;
    }
    fifo[(size_t) w] = x;
    fifoW.store (next, std::memory_order_release);
}

void PrismProcessor::setExtras (const PatchExtras& x)
{
    {
        std::lock_guard<std::mutex> g (modelLock);
        model.extras = x;
    }
    pushExtras (x);
}

PatchExtras PrismProcessor::getExtras() const
{
    std::lock_guard<std::mutex> g (modelLock);
    return model.extras;
}

void PrismProcessor::setTableSource (int osc, const TableSource& src)
{
    {
        std::lock_guard<std::mutex> g (modelLock);
        model.tables[osc] = src;
    }
    tables.request (osc, src);
}

TableSource PrismProcessor::getTableSource (int osc) const
{
    std::lock_guard<std::mutex> g (modelLock);
    return model.tables[osc];
}

std::array<std::string, 4> PrismProcessor::getMacroNames() const
{
    std::lock_guard<std::mutex> g (modelLock);
    return model.macroNames;
}

void PrismProcessor::setMacroName (int m, const std::string& n)
{
    std::lock_guard<std::mutex> g (modelLock);
    model.macroNames[(size_t) m] = n;
}

juce::String PrismProcessor::getPresetName() const
{
    std::lock_guard<std::mutex> g (modelLock);
    return model.name;
}

void PrismProcessor::setPresetMeta (const std::string& name, const std::string& category)
{
    std::lock_guard<std::mutex> g (modelLock);
    model.name = name;
    model.category = category;
}

int PrismProcessor::addRouting (int src, int dest, float depth)
{
    auto x = getExtras();
    int slot = -1;
    for (int s = 0; s < P::NumModSlots; ++s)
        if (x.routings[(size_t) s].src < 0) { slot = s; break; }
    if (slot < 0) return -1;
    auto& r = x.routings[(size_t) slot];
    r = Routing();
    r.src = (int16_t) src;
    r.dest = (int16_t) dest;
    auto* p = params[(size_t) P::modDepth (slot)];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (depth));
    p->endChangeGesture();
    setExtras (x);
    return slot;
}

void PrismProcessor::removeRouting (int slot)
{
    if (slot < 0 || slot >= P::NumModSlots) return;
    auto x = getExtras();
    x.routings[(size_t) slot] = Routing();
    auto* p = params[(size_t) P::modDepth (slot)];
    p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
    setExtras (x);
}

void PrismProcessor::learnChord()
{
    const uint64_t a = engine.heldMask[0].load(), b = engine.heldMask[1].load();
    std::vector<int> notes;
    for (int n = 0; n < 128; ++n)
        if (((n < 64 ? a : b) >> (n % 64)) & 1ull) notes.push_back (n);
    if (notes.empty()) return;
    auto x = getExtras();
    x.chord.count = std::min (8, (int) notes.size());
    for (int i = 0; i < x.chord.count; ++i) x.chord.intervals[(size_t) i] = (int8_t) (notes[(size_t) i] - notes[0]);
    setExtras (x);
}

void PrismProcessor::panic()
{
    panicRequest.store (true); // the audio thread kills all voices at the next block
}

// ======================================================================= undo / AB

void PrismProcessor::captureUndoSnapshot()
{
    auto s = presetToString (currentPreset(), false);
    if (s == lastSnapshot) return;
    lastSnapshot = s;
    if (undoPos + 1 < (int) undoStack.size()) undoStack.erase (undoStack.begin() + undoPos + 1, undoStack.end());
    undoStack.push_back (s);
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    undoPos = (int) undoStack.size() - 1;
}

void PrismProcessor::undo()
{
    if (! canUndo()) return;
    --undoPos;
    PresetData p; juce::String err;
    if (presetFromString (undoStack[(size_t) undoPos], p, err)) { lastSnapshot = undoStack[(size_t) undoPos]; loadPreset (p, true); }
}

void PrismProcessor::redo()
{
    if (! canRedo()) return;
    ++undoPos;
    PresetData p; juce::String err;
    if (presetFromString (undoStack[(size_t) undoPos], p, err)) { lastSnapshot = undoStack[(size_t) undoPos]; loadPreset (p, true); }
}

void PrismProcessor::toggleAB()
{
    abStore[abSlot] = presetToString (currentPreset(), false);
    abSlot = 1 - abSlot;
    if (abStore[abSlot].isEmpty()) { abStore[abSlot] = abStore[1 - abSlot]; return; }
    PresetData p; juce::String err;
    if (presetFromString (abStore[abSlot], p, err)) loadPreset (p, true);
}

void PrismProcessor::timerCallback()
{
    tables.collectGarbage();
    servicePresetLoad();
    // MIDI learn: apply learned CC values to their parameters (message thread)
    for (int cc = 0; cc < 128; ++cc)
        if (ccDirty[(size_t) cc].exchange (false))
        {
            const int p = ccMap[(size_t) cc].load();
            if (p >= 0 && p < P::NumParams) params[(size_t) p]->setValueNotifyingHost (ccValue[(size_t) cc].load());
        }
    // undo snapshots: when the patch has been still for ~0.5 s
    if (++idleTicks >= 15)
    {
        idleTicks = 0;
        if (! presetLoadPending()) captureUndoSnapshot();
    }
}

juce::AudioProcessorEditor* PrismProcessor::createEditor()
{
    return new PrismEditor (*this);
}
} // namespace prism

#ifndef PRISM_HEADLESS_CORE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new prism::PrismProcessor();
}
#endif
