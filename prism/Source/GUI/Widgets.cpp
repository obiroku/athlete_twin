#include "Widgets.h"

namespace prism::gui
{
static juce::String clipboardValue;

juce::String shortName (const ParamDef& d)
{
    juce::String n (d.name);
    for (auto prefix : { "Osc A ", "Osc B ", "Filter 1 ", "Filter 2 ", "Env 1 ", "Env 2 ", "Env 3 ", "LFO 1 ", "LFO 2 ", "LFO 3 ", "LFO 4 ",
                         "Sub ", "Noise ", "MSEG ", "Arp ", "Dist ", "Wow ", "Chorus ", "Phaser ", "Flanger ", "Rotary ", "Delay ",
                         "Reverb ", "Comp ", "MB ", "Dimension " })
        if (n.startsWith (prefix) && n.length() > (int) strlen (prefix)) { n = n.substring ((int) strlen (prefix)); break; }
    static const std::pair<const char*, const char*> shorter[] = {
        { "Attack Curve", "A Curve" }, { "Decay Curve", "D Curve" }, { "Release Curve", "R Curve" },
        { "Unison WT Spread", "WT Spread" }, { "Stereo Spread", "Spread" }, { "Random Phase", "Rand Phase" },
        { "WT Position", "WT Pos" }, { "Warp Amount", "Warp" }, { "Velocity Curve", "Vel Curve" }, { "Fixed Velocity", "Fixed Vel" },
        { "Warp Mode", "Warp" }, { "Destination", "To" }, { "Mono Priority", "Priority" }, { "Glide Mode", "Glide" },
        { "Voice Mode", "Mode" }, { "Warp 2 Mode", "Warp 2" }, { "Warp 2 Amount", "Warp 2" }, { "Unison Mode", "Uni" }, { "Unison Stack", "Stack" }, { "Unison Range", "Range" }, { "Filter Routing", "Routing" }, { "Strum Direction", "Direction" }, { "Polarity", "Pol" },
        { "Division", "Div" }, { "Unison", "Voices" }, { "Octave", "Oct" }, { "Semitone", "Semi" } };
    for (auto& [a, b] : shorter) if (n == a) return b;
    return n;
}

// ============================================================== Knob

Knob::Knob (PrismProcessor& p, int idx, const juce::String& lbl)
    : proc (p), index (idx), param (*p.param (idx)), label (lbl),
      attach (*p.param (idx), [this] (float v) { norm = param.convertTo0to1 (v); repaint(); }, nullptr)
{
    if (label.isEmpty()) label = shortName (paramDefs()[(size_t) idx]);
    attach.sendInitialUpdate();
    setTooltip (juce::String (paramDefs()[(size_t) idx].name) + "  -  drag, Cmd-drag fine, double-click reset, click value to type, drop a mod source here");
    setTitle (paramDefs()[(size_t) idx].name);
    setDescription ("Knob for " + juce::String (paramDefs()[(size_t) idx].name));
    setWantsKeyboardFocus (false);
}

Knob::~Knob() = default;

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    struct ValueIface : juce::AccessibilityRangedNumericValueInterface
    {
        explicit ValueIface (Knob& k) : knob (k) {}
        Knob& knob;
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return knob.norm; }
        void setValue (double v) override { knob.attach.setValueAsCompleteGesture (knob.param.convertFrom0to1 ((float) v)); }
        AccessibleValueRange getRange() const override { return { { 0.0, 1.0 }, 0.01 }; }
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
        juce::AccessibilityActions(), juce::AccessibilityHandler::Interfaces { std::make_unique<ValueIface> (*this) });
}

juce::String Knob::valueText() const
{
    return paramDefs()[(size_t) index].toText (param.convertFrom0to1 (norm));
}

std::vector<int> Knob::routingsHere() const
{
    std::vector<int> out;
    const auto x = proc.getExtras();
    for (int s = 0; s < P::NumModSlots; ++s)
        if (x.routings[(size_t) s].src >= 0 && x.routings[(size_t) s].dest == index) out.push_back (s);
    return out;
}

void Knob::updateRing()
{
    const auto x = proc.getExtras();
    float lo = 0, hi = 0;
    hasMod = false;
    for (int s = 0; s < P::NumModSlots; ++s)
    {
        const auto& r = x.routings[(size_t) s];
        if (r.src < 0 || r.dest != index || r.bypass) continue;
        hasMod = true;
        const float d = proc.param (P::modDepth (s))->convertFrom0to1 (proc.param (P::modDepth (s))->getValue());
        bool bi = srcIsBipolar (r.src);
        if (r.src >= (int) Src::Lfo1 && r.src <= (int) Src::Lfo4)
            bi = proc.param (P::lfo (r.src - (int) Src::Lfo1, P::l_polarity))->getValue() >= 0.5f;
        if (r.bipolar == 1) bi = true; else if (r.bipolar == 0) bi = false;
        if (bi) { lo -= std::abs (d); hi += std::abs (d); }
        else { lo += std::min (0.0f, d); hi += std::max (0.0f, d); }
        if (depthSlot < 0) depthSlot = s;
    }
    ringLo = lo; ringHi = hi;
    ringSlots.clear(); ringDepths.clear();
    for (int s = 0; s < P::NumModSlots; ++s)
        if (x.routings[(size_t) s].src >= 0 && x.routings[(size_t) s].dest == index)
        {
            ringSlots.push_back (s);
            ringDepths.push_back (proc.param (P::modDepth (s))->getValue());
        }
}

void Knob::refreshLive()
{
    const int v = proc.extrasVersion.load();
    bool dirty = false;
    if (v != seenVersion) { seenVersion = v; depthSlot = -1; updateRing(); dirty = true; }
    else if (hasMod)
    {
        // depth parameters may be automated: recompute only when one of them moved
        bool moved = false;
        for (size_t i = 0; i < ringSlots.size(); ++i)
        {
            const float d = proc.param (P::modDepth (ringSlots[i]))->getValue();
            if (d != ringDepths[i]) { ringDepths[i] = d; moved = true; }
        }
        if (moved) { updateRing(); dirty = true; }
    }
    float live = -1.0f;
    if (hasMod && proc.engine.voiceCount.load() > 0 && proc.engine.liveValid[index].load())
        live = proc.engine.liveNorm[index].load();
    else if (hasMod && paramDefs()[(size_t) index].scope == Scope::Global && proc.engine.liveValid[index].load())
        live = proc.engine.liveNorm[index].load();
    if (std::abs (live - liveDot) > 0.002f) { liveDot = live; dirty = true; }
    if (dirty) repaint();
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float textH = 14.0f;
    auto knobArea = b.withTrimmedBottom (textH).reduced (4.0f);
    const float sz = std::min (knobArea.getWidth(), knobArea.getHeight());
    auto kr = juce::Rectangle<float> (sz, sz).withCentre (knobArea.getCentre());
    const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
    const float radius = sz * 0.5f - 2.0f;
    const auto c = kr.getCentre();
    auto angleOf = [&] (float n) { return start + (end - start) * juce::jlimit (0.0f, 1.0f, n); };

    juce::Path track;
    track.addCentredArc (c.x, c.y, radius, radius, 0, start, end, true);
    g.setColour (col::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = param.getNormalisableRange().start < 0 && param.getNormalisableRange().end > 0;
    const float from = bipolar ? param.convertTo0to1 (0.0f) : 0.0f;
    juce::Path val;
    val.addCentredArc (c.x, c.y, radius, radius, 0, angleOf (std::min (from, norm)), angleOf (std::max (from, norm)), true);
    g.setColour (col::accent);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (hasMod)
    {
        const float lo = juce::jlimit (0.0f, 1.0f, norm + ringLo), hi = juce::jlimit (0.0f, 1.0f, norm + ringHi);
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, radius - 5.0f, radius - 5.0f, 0, angleOf (lo), angleOf (hi), true);
        g.setColour (col::accent.withAlpha (0.45f));
        g.strokePath (ring, juce::PathStrokeType (3.0f));
        if (liveDot >= 0.0f)
        {
            const float a = angleOf (liveDot) - juce::MathConstants<float>::halfPi;
            g.setColour (col::text);
            g.fillEllipse (c.x + (radius - 5.0f) * std::cos (a) - 2.5f, c.y + (radius - 5.0f) * std::sin (a) - 2.5f, 5, 5);
        }
    }
    // pointer
    {
        const float a = angleOf (norm) - juce::MathConstants<float>::halfPi;
        g.setColour (col::text);
        g.drawLine (c.x + std::cos (a) * radius * 0.35f, c.y + std::sin (a) * radius * 0.35f,
                    c.x + std::cos (a) * (radius - 8.0f), c.y + std::sin (a) * (radius - 8.0f), 2.0f);
    }
    if (dropHover) { g.setColour (col::accent); g.drawEllipse (kr.reduced (1), 2.0f); }

    g.setFont (uiFont (11.5f));
    g.setColour (hover || dragging ? col::accent : col::dim);
    g.drawFittedText (hover || dragging ? valueText() : label, getLocalBounds().removeFromBottom ((int) textH), juce::Justification::centred, 1);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.y > getHeight() - 16 && hover) { beginTextEntry(); return; }
    dragging = true;
    editingDepth = e.mods.isAltDown() && depthSlot >= 0;
    dragStartNorm = norm;
    if (editingDepth)
    {
        auto* dp = proc.param (P::modDepth (depthSlot));
        dragStartDepth = dp->getValue();
        dp->beginChangeGesture();
    }
    else attach.beginGesture();
    e.source.enableUnboundedMouseMovement (true);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const bool fine = e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isShiftDown();
    const float delta = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / (fine ? 2000.0f : 220.0f);
    if (editingDepth)
    {
        auto* dp = proc.param (P::modDepth (depthSlot));
        dp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartDepth + delta * 0.5f));
        updateRing();
        repaint();
        return;
    }
    const float n = juce::jlimit (0.0f, 1.0f, dragStartNorm + delta);
    attach.setValueAsPartOfGesture (param.convertFrom0to1 (n));
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    if (editingDepth) proc.param (P::modDepth (depthSlot))->endChangeGesture();
    else attach.endGesture();
    e.source.enableUnboundedMouseMovement (false);
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    attach.setValueAsCompleteGesture (paramDefs()[(size_t) index].def);
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isCommandDown() ? 0.002f : 0.02f) * (w.deltaY > 0 ? 1.0f : -1.0f);
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, norm + step)));
}

void Knob::beginTextEntry()
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (uiFont (12.0f));
    editor->setJustification (juce::Justification::centred);
    editor->setText (valueText(), false);
    editor->selectAll();
    editor->setBounds (getLocalBounds().removeFromBottom (18));
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
    auto finish = [this] (bool apply) {
        if (editor == nullptr) return;
        if (apply)
        {
            float v;
            if (paramDefs()[(size_t) index].fromText (editor->getText().toStdString(), v)) attach.setValueAsCompleteGesture (v);
        }
        auto* ed = editor.release();
        juce::MessageManager::callAsync ([ed] { delete ed; });
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    editor->onFocusLost = [finish] { finish (true); };
}

void Knob::showMenu()
{
    juce::PopupMenu m;
    const auto routes = routingsHere();
    const auto x = proc.getExtras();
    juce::PopupMenu remove, edit;
    for (int s : routes)
    {
        remove.addItem (1000 + s, juce::String ("Remove ") + srcName (x.routings[(size_t) s].src));
        edit.addItem (2000 + s, juce::String ("Alt-drag edits ") + srcName (x.routings[(size_t) s].src), true, s == depthSlot);
    }
    m.addSectionHeader (paramDefs()[(size_t) index].name);
    m.addSubMenu ("Remove modulation", remove, ! routes.empty());
    m.addSubMenu ("Modulation depth", edit, ! routes.empty());
    m.addItem (1, "MIDI learn");
    m.addItem (2, "Reset to default");
    m.addItem (3, "Copy value");
    m.addItem (4, "Paste value", clipboardValue.isNotEmpty());
    m.addItem (5, "Type a value...");
    juce::Component::SafePointer<Knob> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& k = *safe;
        if (r == 1) k.proc.startMidiLearn (k.index);
        else if (r == 2) k.attach.setValueAsCompleteGesture (paramDefs()[(size_t) k.index].def);
        else if (r == 3) { clipboardValue = juce::String (k.param.convertFrom0to1 (k.norm)); juce::SystemClipboard::copyTextToClipboard (clipboardValue); }
        else if (r == 4) k.attach.setValueAsCompleteGesture (clipboardValue.getFloatValue());
        else if (r == 5) k.beginTextEntry();
        else if (r >= 2000) { k.depthSlot = r - 2000; }
        else if (r >= 1000) k.proc.removeRouting (r - 1000);
    });
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("src:") && paramDefs()[(size_t) index].isModDest();
}

void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("src:", false, false).getIntValue();
    const int slot = proc.addRouting (src, index, 0.25f);
    if (slot >= 0) depthSlot = slot;
    seenVersion = -1;
    refreshLive();
}

// ============================================================== combo / toggle

static void fillChoices (juce::ComboBox& c, int idx)
{
    const auto& d = paramDefs()[(size_t) idx];
    if (d.type == PType::Choice)
        for (size_t i = 0; i < d.choices.size(); ++i) c.addItem (d.choices[i], (int) i + 1);
    else
        for (int v = (int) d.min; v <= (int) d.max; ++v) c.addItem (juce::String (v), v - (int) d.min + 1);
}

ParamCombo::ParamCombo (PrismProcessor& p, int idx)
    : attach (*p.param (idx), [&] () -> juce::ComboBox& { fillChoices (*this, idx); return *this; }())
{
    setTitle (paramDefs()[(size_t) idx].name);
    setTooltip (paramDefs()[(size_t) idx].name);
    getProperties().set ("caption", shortName (paramDefs()[(size_t) idx]));
}

ParamToggle::ParamToggle (PrismProcessor& p, int idx, const juce::String& text)
    : juce::ToggleButton (text), attach (*p.param (idx), *this)
{
    setTitle (paramDefs()[(size_t) idx].name);
    setTooltip (paramDefs()[(size_t) idx].name);
}

// ============================================================== mod handle

ModHandle::ModHandle (PrismProcessor& p, int s) : proc (p), src (s)
{
    setTooltip (juce::String ("Drag ") + srcName (s) + " onto any knob to modulate it");
    setTitle (juce::String (srcName (s)) + " modulation handle");
}

void ModHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::accent);
    g.drawRoundedRectangle (r, 4.0f, 1.2f);
    // level bar
    g.setColour (col::accentDim);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, std::abs (level))), 4.0f);
    g.setColour (col::text);
    g.setFont (uiFont (12.0f, true));
    g.drawText (juce::String (srcName (src)) + "  +", r, juce::Justification::centred);
}

void ModHandle::mouseDrag (const juce::MouseEvent&)
{
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! c->isDragAndDropActive()) c->startDragging ("src:" + juce::String (src), this);
}

void ModHandle::refreshLive()
{
    const float v = proc.engine.liveSrc[src].load();
    if (std::abs (v - level) > 0.01f) { level = v; repaint(); }
}

// ============================================================== panel

ParamPanel::ParamPanel (PrismProcessor& p, const juce::String& t) : title (t), proc (p) {}

juce::Component* ParamPanel::add (const std::string& id, const juce::String& label, int span)
{
    const int idx = paramIndex (id);
    jassert (idx >= 0);
    const auto& d = paramDefs()[(size_t) idx];
    juce::Component* c = nullptr;
    bool small = false;
    if (d.type == PType::Float && d.switchLike) { c = new ParamToggle (proc, idx, label.isNotEmpty() ? label : shortName (d)); small = true; }
    else if (d.type == PType::Float) c = new Knob (proc, idx, label);
    else if (d.type == PType::Bool) { c = new ParamToggle (proc, idx, label.isNotEmpty() ? label : shortName (d)); small = true; }
    else if (d.type == PType::Int && (d.max - d.min) > 24) c = new Knob (proc, idx, label);
    else { c = new ParamCombo (proc, idx); small = true; }
    owned.emplace_back (c);
    addAndMakeVisible (c);
    items.push_back ({ c, small ? std::max (span, 2) : span, 1, small });
    return c;
}

void ParamPanel::addCustom (juce::Component* c, int w, int h)
{
    addAndMakeVisible (c);
    items.push_back ({ c, w, h == 0 ? 1 : h, h == 0 }); // height 0 = a 24 px row
}

void ParamPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (title, r.removeFromTop (22).reduced (8, 0), juce::Justification::centredLeft);
}

void ParamPanel::resized()
{
    auto area = getLocalBounds().reduced (6);
    auto top = area.removeFromTop (18);
    if (header != nullptr) header->setBounds (top.removeFromRight (std::min (170, top.getWidth() / 2)).withHeight (20));
    area.removeFromTop (2);
    int x = area.getX(), y = area.getY();
    int rowH = 0;
    std::vector<Item*> order;
    for (auto& it : items) if (! it.small) order.push_back (&it);
    bool brokeLine = false;
    for (auto& it : items) if (it.small) order.push_back (&it);
    for (auto* itp : order)
    {
        auto& it = *itp;
        if (it.small && ! brokeLine)
        {
            brokeLine = true;
            if (x > area.getX()) { x = area.getX(); y += rowH + 4; rowH = 0; }
        }
        const int w = it.w * cellW, h = it.small ? 26 : it.h * cellH;
        if (x + w > area.getRight() && x > area.getX()) { x = area.getX(); y += rowH + 4; rowH = 0; }
        if (it.small)
        {
            // small controls (combos/toggles) stack two per knob row
            it.c->setBounds (x + 2, y + (rowH == 0 ? 0 : 0), w - 4, 24);
        }
        else it.c->setBounds (x, y, w, h);
        x += w;
        rowH = std::max (rowH, h);
    }
}

void ParamPanel::collectLive (std::vector<LiveComponent*>& out)
{
    for (auto& it : items)
        if (auto* l = dynamic_cast<LiveComponent*> (it.c)) out.push_back (l);
}
} // namespace prism::gui
