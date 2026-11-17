#include "Pages.h"
#include "../DSP/FactoryTables.h"
#include "../Engine/TableIO.h"

namespace prism::gui
{
void Page::collectLive (std::vector<LiveComponent*>& out)
{
    for (auto& p : panels) p->collectLive (out);
    for (auto& e : extra) if (auto* l = dynamic_cast<LiveComponent*> (e.get())) out.push_back (l);
}

ParamPanel& Page::panel (const juce::String& title)
{
    panels.push_back (std::make_unique<ParamPanel> (proc, title));
    addAndMakeVisible (*panels.back());
    return *panels.back();
}

// ======================================================================= mod source pages

namespace
{
class SourcePage : public juce::Component
{
public:
    SourcePage (PrismProcessor& p, int src, std::function<void (ParamPanel&)> fill, ShapeEditor* editor)
        : handle (p, src), knobs (p, juce::String (srcName (src))), shapeEditor (editor)
    {
        knobs.cellW = 52; knobs.cellH = 56;
        fill (knobs);
        knobs.setHeaderComponent (&handle);
        addAndMakeVisible (knobs);
        if (shapeEditor) addAndMakeVisible (*shapeEditor);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        if (shapeEditor) { shapeEditor->setBounds (r.removeFromRight (220)); r.removeFromRight (4); }
        knobs.setBounds (r);
    }
    void collectLive (std::vector<LiveComponent*>& out)
    {
        out.push_back (&handle);
        knobs.collectLive (out);
        if (shapeEditor) out.push_back (shapeEditor.get());
    }
    ModHandle handle;
    ParamPanel knobs;
    std::unique_ptr<ShapeEditor> shapeEditor;
};

class SourceTabs : public juce::TabbedComponent, public LiveComponent
{
public:
    SourceTabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) { setTabBarDepth (24); }
    std::vector<SourcePage*> pages;
    void refreshLive() override
    {
        std::vector<LiveComponent*> v;
        if (auto* p = dynamic_cast<SourcePage*> (getCurrentContentComponent())) p->collectLive (v);
        for (auto* l : v) l->refreshLive();
    }
};

class MacroHandles : public juce::Component, public LiveComponent
{
public:
    explicit MacroHandles (PrismProcessor& p)
    {
        for (int m = 0; m < 4; ++m) { h[m] = std::make_unique<ModHandle> (p, (int) Src::Macro1 + m); addAndMakeVisible (*h[m]); }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int w = r.getWidth() / 4;
        for (int m = 0; m < 4; ++m) h[m]->setBounds (r.removeFromLeft (w).reduced (2, 0));
    }
    void refreshLive() override { for (auto& x : h) x->refreshLive(); }
    std::unique_ptr<ModHandle> h[4];
};
} // namespace

// ======================================================================= SYNTH

void SynthPage::fillTables (juce::ComboBox& c, int osc)
{
    c.clear (juce::dontSendNotification);
    const auto& names = dsp::factoryTableNames();
    for (size_t i = 0; i < names.size(); ++i) c.addItem (names[i], (int) i + 1);
    c.addSeparator();
    c.addItem ("Import WAV/AIFF...", 1000);
    const auto src = proc.getTableSource (osc);
    const int idx = dsp::factoryTableIndex (src.factory);
    if (idx >= 0) c.setSelectedId (idx + 1, juce::dontSendNotification);
    else c.setText (src.name.empty() ? juce::String ("User") : juce::String (src.name), juce::dontSendNotification);
}

SynthPage::SynthPage (PrismProcessor& p) : Page (p)
{
    auto oscPanel = [this] (int o, ParamPanel*& out, WavetableView*& view, juce::ComboBox& table) {
        const std::string px = o == 0 ? "oscA_" : "oscB_";
        out = &panel (o == 0 ? "OSC A" : "OSC B");
        out->cellW = 45; out->cellH = 52;
        view = &make<WavetableView> (proc, o);
        out->addCustom (view, 7, 1);
        for (auto id : { "wt_pos", "level", "pan", "pitch", "warp_amt", "warp2_amt", "fine",
                         "detune", "blend", "spread", "wt_spread", "uni_range", "phase", "rand_phase" })
            out->add (px + id);
        out->add (px + "warp_mode"); out->add (px + "warp2_mode"); out->add (px + "unison");
        out->add (px + "uni_mode"); out->add (px + "uni_stack"); out->add (px + "dest");
        out->add (px + "oct"); out->add (px + "semi");
        fillTables (table, o);
        table.setTitle (o == 0 ? "Osc A wavetable" : "Osc B wavetable");
        table.setTooltip ("Wavetable");
        table.onChange = [this, o, &table] {
            const int id = table.getSelectedId();
            if (id == 1000)
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Import wavetable", proc.library.settings.tableFolder, "*.wav;*.aif;*.aiff");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, o, chooser, &table] (const juce::FileChooser& fc) {
                    const auto f = fc.getResult();
                    if (f.existsAsFile())
                    {
                        auto r = importWavetable (f, ImportMode::Auto);
                        if (r.ok) { TableSource s; s.name = r.name.toStdString(); s.frames = r.frames; proc.setTableSource (o, s); }
                        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", r.error);
                    }
                    fillTables (table, o);
                });
                return;
            }
            if (id > 0) { TableSource s; s.factory = dsp::factoryTableNames()[(size_t) id - 1]; proc.setTableSource (o, s); }
        };
        out->setHeaderComponent (&table);
    };
    oscPanel (0, oscA, viewA, tableA);
    oscPanel (1, oscB, viewB, tableB);

    sub = &panel ("SUB");
    sub->cellW = 50; sub->cellH = 58;
    sub->add ("sub_level", "Level"); sub->add ("sub_pan", "Pan"); sub->add ("sub_shape"); sub->add ("sub_oct"); sub->add ("sub_dest");
    noise = &panel ("NOISE");
    noise->cellW = 50; noise->cellH = 58;
    noise->add ("noise_level", "Level"); noise->add ("noise_tone", "Tone"); noise->add ("noise_pan", "Pan");
    noise->add ("noise_type"); noise->add ("noise_dest"); noise->add ("noise_stereo", "Stereo");

    for (int f = 0; f < 2; ++f)
    {
        const std::string px = f == 0 ? "f1_" : "f2_";
        auto& fp = panel (f == 0 ? "FILTER 1" : "FILTER 2");
        fp.cellW = 53; fp.cellH = 58;
        for (auto id : { "cutoff", "reso", "drive", "keytrack", "vowel", "mix" }) fp.add (px + id);
        fp.add (px + "type"); fp.add (px + "enable", "On");
        if (f == 0) { fp.add ("filter_routing"); f1 = &fp; } else f2 = &fp;
    }
    env1 = &panel ("AMP ENVELOPE (ENV 1)");
    env1->cellW = 50; env1->cellH = 56;
    for (auto id : { "delay", "attack", "hold", "decay", "sustain", "release", "curve_a", "curve_d", "curve_r", "vel_amt" })
        env1->add (std::string ("env1_") + id);

    macros = &panel ("MACROS");
    macros->cellW = 72; macros->cellH = 62;
    for (int m = 0; m < 4; ++m) macros->add ("macro" + std::to_string (m + 1), juce::String (proc.getMacroNames()[(size_t) m]));
    auto& mh = make<MacroHandles> (proc);
    macros->addCustom (&mh, 4, 0);

    // ---- bottom: tabbed modulation sources with drawable editors
    auto* tabs = new SourceTabs();
    extra.emplace_back (tabs);
    addAndMakeVisible (tabs);
    for (int l = 0; l < 4; ++l)
    {
        const std::string px = "lfo" + std::to_string (l + 1) + "_";
        auto* pg = new SourcePage (proc, (int) Src::Lfo1 + l, [px] (ParamPanel& k) {
            for (auto id : { "rate", "phase", "delay", "fade", "smooth" }) k.add (px + id);
            k.add (px + "shape"); k.add (px + "mode"); k.add (px + "polarity"); k.add (px + "div");
            k.add (px + "sync", "Sync"); k.add (px + "oneshot", "One-shot");
        }, new ShapeEditor (proc, ShapeEditor::Target::Lfo, l));
        tabs->addTab ("LFO " + juce::String (l + 1), col::panel, pg, true);
    }
    for (int e = 1; e < 3; ++e)
    {
        const std::string px = "env" + std::to_string (e + 1) + "_";
        auto* pg = new SourcePage (proc, (int) Src::Env1 + e, [px] (ParamPanel& k) {
            for (auto id : { "delay", "attack", "hold", "decay", "sustain", "release", "curve_a", "curve_d", "curve_r", "vel_amt" }) k.add (px + id);
        }, nullptr);
        tabs->addTab ("ENV " + juce::String (e + 1), col::panel, pg, true);
    }
    {
        auto* pg = new SourcePage (proc, (int) Src::Mseg, [] (ParamPanel& k) {
            k.add ("mseg_rate", "Length"); k.add ("mseg_mode"); k.add ("mseg_div"); k.add ("mseg_sync", "Sync");
        }, new ShapeEditor (proc, ShapeEditor::Target::Mseg, 0));
        tabs->addTab ("MSEG", col::panel, pg, true);
    }
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (282);
    oscA->setBounds (top.removeFromLeft (330));
    top.removeFromLeft (6);
    oscB->setBounds (top.removeFromLeft (330));
    top.removeFromLeft (6);
    sub->setBounds (top.removeFromTop (136));
    top.removeFromTop (6);
    noise->setBounds (top);
    r.removeFromTop (6);
    auto mid = r.removeFromTop (156);
    f1->setBounds (mid.removeFromLeft (330));
    mid.removeFromLeft (6);
    f2->setBounds (mid.removeFromLeft (330));
    mid.removeFromLeft (6);
    macros->setBounds (mid);
    r.removeFromTop (6);
    env1->setBounds (r.removeFromRight (318));
    r.removeFromRight (6);
    for (auto& e : extra)
        if (auto* t = dynamic_cast<SourceTabs*> (e.get())) t->setBounds (r);
}

// ======================================================================= MOD matrix

class ModMatrixPage::Row : public juce::Component, public LiveComponent
{
public:
    Row (PrismProcessor& p, int s) : proc (p), slot (s),
        depthAttach (*p.param (P::modDepth (s)), depth, nullptr)
    {
        num.setText (juce::String (s + 1), juce::dontSendNotification);
        num.setColour (juce::Label::textColourId, col::dim);
        addAndMakeVisible (num);
        auto fillSrc = [] (juce::ComboBox& c, bool none) {
            if (none) c.addItem ("none", 1);
            for (int i = 0; i < NumSrc; ++i) c.addItem (srcName (i), i + 2);
        };
        fillSrc (src, true);
        fillSrc (aux, true);
        dst.addItem ("none", 1);
        juce::String group;
        const auto& defs = paramDefs();
        for (int i = 0; i < P::NumParams; ++i)
        {
            if (! defs[(size_t) i].isModDest()) continue;
            if (defs[(size_t) i].group != group.toStdString()) { group = defs[(size_t) i].group; dst.addSectionHeading (group); }
            dst.addItem (defs[(size_t) i].name, i + 2);
        }
        pol.addItem ("native", 1); pol.addItem ("uni", 2); pol.addItem ("bi", 3);
        depth.setSliderStyle (juce::Slider::LinearHorizontal);
        depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        curve.setSliderStyle (juce::Slider::LinearHorizontal);
        curve.setRange (-1.0, 1.0, 0.01);
        curve.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        curve.setTooltip ("Curve");
        bypass.setTooltip ("Bypass");
        clear.setButtonText ("X");
        for (juce::Component* c : { (juce::Component*) &src, (juce::Component*) &dst, (juce::Component*) &depth, (juce::Component*) &pol,
                                    (juce::Component*) &curve, (juce::Component*) &aux, (juce::Component*) &bypass, (juce::Component*) &clear })
            addAndMakeVisible (c);
        src.setTitle ("Mod " + juce::String (s + 1) + " source");
        dst.setTitle ("Mod " + juce::String (s + 1) + " destination");
        depth.setTitle ("Mod " + juce::String (s + 1) + " depth");
        auto commit = [this] {
            if (loading) return;
            auto x = proc.getExtras();
            auto& r = x.routings[(size_t) slot];
            r.src = (int16_t) (src.getSelectedId() - 2);
            r.dest = (int16_t) (dst.getSelectedId() - 2);
            r.aux = (int16_t) (aux.getSelectedId() - 2);
            r.bipolar = (int8_t) (pol.getSelectedId() - 2);
            r.curve = (float) curve.getValue();
            r.bypass = (int8_t) (bypass.getToggleState() ? 1 : 0);
            proc.setExtras (x);
        };
        src.onChange = commit; dst.onChange = commit; aux.onChange = commit; pol.onChange = commit;
        curve.onDragEnd = commit; bypass.onClick = commit;
        clear.onClick = [this] { proc.removeRouting (slot); };
        refreshLive();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 1);
        num.setBounds (r.removeFromLeft (26));
        src.setBounds (r.removeFromLeft (130).reduced (2, 0));
        dst.setBounds (r.removeFromLeft (210).reduced (2, 0));
        depth.setBounds (r.removeFromLeft (200).reduced (2, 0));
        pol.setBounds (r.removeFromLeft (80).reduced (2, 0));
        curve.setBounds (r.removeFromLeft (90).reduced (2, 0));
        aux.setBounds (r.removeFromLeft (120).reduced (2, 0));
        bypass.setBounds (r.removeFromLeft (52).reduced (2, 0));
        clear.setBounds (r.removeFromLeft (30).reduced (2, 0));
    }
    void refreshLive() override
    {
        const int v = proc.extrasVersion.load();
        if (v == seen) return;
        seen = v;
        const auto x = proc.getExtras();
        const auto& r = x.routings[(size_t) slot];
        loading = true;
        src.setSelectedId (r.src + 2, juce::dontSendNotification);
        dst.setSelectedId (r.dest + 2, juce::dontSendNotification);
        aux.setSelectedId (r.aux + 2, juce::dontSendNotification);
        pol.setSelectedId (r.bipolar + 2, juce::dontSendNotification);
        curve.setValue (r.curve, juce::dontSendNotification);
        bypass.setToggleState (r.bypass != 0, juce::dontSendNotification);
        loading = false;
        const bool used = r.src >= 0;
        for (juce::Component* c : { (juce::Component*) &depth, (juce::Component*) &pol, (juce::Component*) &curve, (juce::Component*) &aux, (juce::Component*) &bypass })
            c->setAlpha (used ? 1.0f : 0.35f);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour ((slot & 1) ? col::panel : col::panelHi);
        g.fillRect (getLocalBounds());
    }
private:
    PrismProcessor& proc;
    int slot, seen = -1;
    bool loading = false;
    juce::Label num;
    juce::ComboBox src, dst, pol, aux;
    juce::Slider depth, curve;
    juce::ToggleButton bypass;
    juce::TextButton clear;
    juce::SliderParameterAttachment depthAttach;
};

ModMatrixPage::ModMatrixPage (PrismProcessor& p) : Page (p)
{
    for (int s = 0; s < P::NumModSlots; ++s)
    {
        rows.push_back (std::make_unique<Row> (p, s));
        list.addAndMakeVisible (*rows.back());
    }
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void ModMatrixPage::resized()
{
    viewport.setBounds (getLocalBounds().reduced (6));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    list.setSize (w, (int) rows.size() * 28);
    for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * 28, w, 28);
}

void ModMatrixPage::collectLive (std::vector<LiveComponent*>& out)
{
    for (auto& r : rows) out.push_back (r.get());
}

// ======================================================================= FX

class FxPage::RackList : public juce::Component, public LiveComponent
{
public:
    RackList (PrismProcessor& p, FxPage& o) : proc (p), owner (o)
    {
        for (int f = 0; f < NumFx; ++f)
        {
            toggles[(size_t) f] = std::make_unique<ParamToggle> (p, fxEnableParam (f), "");
            addAndMakeVisible (*toggles[(size_t) f]);
        }
        setTitle ("FX rack - drag to reorder");
        refreshLive();
    }
    void refreshLive() override
    {
        const int v = proc.extrasVersion.load();
        if (v != seen) { seen = v; order = proc.getExtras().fxOrder; resized(); repaint(); }
    }
    void resized() override
    {
        for (int i = 0; i < NumFx; ++i)
            toggles[(size_t) order[(size_t) i]]->setBounds (getWidth() - 46, i * rowH + 6, 40, rowH - 12);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6);
        for (int i = 0; i < NumFx; ++i)
        {
            const int f = order[(size_t) i];
            auto r = juce::Rectangle<int> (0, i * rowH, getWidth(), rowH).reduced (4, 2);
            g.setColour (f == owner.selected ? col::panelHi.brighter (0.1f) : col::panelHi);
            g.fillRoundedRectangle (r.toFloat(), 4);
            if (i == dropIndex && dragging >= 0) { g.setColour (col::accent); g.fillRect (r.getX(), r.getY() - 2, r.getWidth(), 2); }
            g.setColour (proc.engine.voiceCount.load() >= 0 && toggles[(size_t) f]->getToggleState() ? col::text : col::dim);
            g.setFont (uiFont (13.0f, true));
            g.drawText (juce::String (i + 1) + "  " + fxName (f), r.reduced (8, 0), juce::Justification::centredLeft);
            g.setColour (col::dim);
            g.drawText (":::", r.withTrimmedRight (50), juce::Justification::centredRight);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, NumFx - 1, e.y / rowH);
        dragging = i;
        dropIndex = i;
        owner.select (order[(size_t) i]);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        dropIndex = juce::jlimit (0, NumFx - 1, (e.y + rowH / 2) / rowH);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0 && dropIndex != dragging)
        {
            auto x = proc.getExtras();
            auto o = x.fxOrder;
            const int8_t moved = o[(size_t) dragging];
            std::vector<int8_t> v (o.begin(), o.end());
            v.erase (v.begin() + dragging);
            v.insert (v.begin() + std::min ((int) v.size(), dropIndex), moved);
            for (int i = 0; i < NumFx; ++i) x.fxOrder[(size_t) i] = v[(size_t) i];
            proc.setExtras (x); // crossfaded in the audio thread (10 ms)
        }
        dragging = -1;
        repaint();
    }
private:
    PrismProcessor& proc;
    FxPage& owner;
    std::array<std::unique_ptr<ParamToggle>, NumFx> toggles;
    std::array<int8_t, NumFx> order {};
    int seen = -1, dragging = -1, dropIndex = -1;
    static constexpr int rowH = 40;
};

FxPage::FxPage (PrismProcessor& p) : Page (p)
{
    rack = std::make_unique<RackList> (p, *this);
    addAndMakeVisible (*rack);
    const auto& defs = paramDefs();
    for (int f = 0; f < NumFx; ++f)
    {
        modules[(size_t) f] = std::make_unique<ParamPanel> (p, juce::String (fxName (f)).toUpperCase());
        auto& m = *modules[(size_t) f];
        m.cellW = 64; m.cellH = 72;
        const int en = fxEnableParam (f);
        const std::string group = defs[(size_t) en].group;
        for (int i = 0; i < P::NumParams; ++i)
            if (defs[(size_t) i].group == group && i != en) m.add (defs[(size_t) i].id);
        m.add (defs[(size_t) en].id, "On");
        addChildComponent (m);
    }
    master = &panel ("MASTER");
    master->cellW = 64; master->cellH = 72;
    master->add ("master_vol", "Volume"); master->add ("master_width", "Width"); master->add ("bass_mono", "Bass Mono");
    master->add ("softclip", "Soft Clip");
    auto& meter = make<Meter> (proc);
    master->addCustom (&meter, 4, 0);
    auto& sc = make<ScopeView> (proc);
    master->addCustom (&sc, 4, 2);
    select (selected);
}

void FxPage::select (int fx)
{
    selected = fx;
    for (int f = 0; f < NumFx; ++f) modules[(size_t) f]->setVisible (f == fx);
    if (rack) rack->repaint();
}

void FxPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    rack->setBounds (r.removeFromLeft (250).withHeight (12 * 40 + 8));
    r.removeFromLeft (6);
    master->setBounds (r.removeFromRight (280));
    r.removeFromRight (6);
    for (auto& m : modules) m->setBounds (r);
}

void FxPage::collectLive (std::vector<LiveComponent*>& out)
{
    Page::collectLive (out);
    out.push_back (rack.get());
    for (auto& m : modules) if (m->isVisible()) m->collectLive (out);
}

// ======================================================================= ARP / VOICING

class ArpVoicingPage::AccentEditor : public juce::Component, public LiveComponent
{
public:
    explicit AccentEditor (PrismProcessor& p) : proc (p) { setTitle ("Arp accent pattern"); }
    void refreshLive() override
    {
        const int v = proc.extrasVersion.load();
        if (v != seen) { seen = v; acc = proc.getExtras().arpAccents; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 4);
        const float w = r.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            auto b = juce::Rectangle<float> (r.getX() + i * w + 2, r.getY() + 4, w - 4, r.getHeight() - 8);
            g.setColour (col::line);
            g.fillRect (b);
            g.setColour (col::accent);
            g.fillRect (b.withTop (b.getBottom() - b.getHeight() * acc[(size_t) i]));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 15, (int) (e.x * 16 / std::max (1, getWidth())));
        acc[(size_t) i] = juce::jlimit (0.05f, 1.0f, 1.0f - (float) (e.y - 4) / (float) std::max (1, getHeight() - 8));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        auto x = proc.getExtras();
        x.arpAccents = acc;
        proc.setExtras (x);
        seen = proc.extrasVersion.load();
    }
private:
    PrismProcessor& proc;
    std::array<float, 16> acc {};
    int seen = -1;
};

ArpVoicingPage::ArpVoicingPage (PrismProcessor& p) : Page (p)
{
    voicing = &panel ("VOICING");
    voicing->cellW = 64; voicing->cellH = 72;
    for (auto id : { "glide_time", "vel_curve", "drift", "amp_level", "voice_pitch", "poly" }) voicing->add (id);
    for (auto id : { "voice_mode", "mono_priority", "glide_mode" }) voicing->add (id);
    voicing->add ("glide_legato", "Legato glide");
    arpPanel = &panel ("ARPEGGIATOR");
    arpPanel->cellW = 64; arpPanel->cellH = 72;
    for (auto id : { "arp_gate", "arp_swing", "arp_vel_fixed" }) arpPanel->add (id);
    for (auto id : { "arp_mode", "arp_div", "arp_octaves", "arp_vel_mode" }) arpPanel->add (id);
    arpPanel->add ("arp_on", "Arp on"); arpPanel->add ("arp_latch", "Latch");
    accents = std::make_unique<AccentEditor> (p);
    arpPanel->addCustom (accents.get(), 8, 1);
    chordPanel = &panel ("CHORD MODE");
    chordPanel->cellW = 64; chordPanel->cellH = 72;
    chordPanel->add ("chord_strum", "Strum");
    chordPanel->add ("chord_dir"); chordPanel->add ("chord_on", "Chord on");
    learn.setTooltip ("Hold notes on your keyboard, then press Learn");
    learn.onClick = [this] { proc.learnChord(); };
    chordPanel->addCustom (&learn, 2, 0);
    chordLabel.setColour (juce::Label::textColourId, col::dim);
    chordPanel->addCustom (&chordLabel, 6, 0);
}

void ArpVoicingPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    voicing->setBounds (r.removeFromTop (200));
    r.removeFromTop (6);
    arpPanel->setBounds (r.removeFromTop (240));
    r.removeFromTop (6);
    chordPanel->setBounds (r);
    const auto x = proc.getExtras();
    juce::String s = "Chord: ";
    if (x.chord.count == 0) s += "(none learned)";
    for (int i = 0; i < x.chord.count; ++i) s += juce::String (x.chord.intervals[(size_t) i]) + (i + 1 < x.chord.count ? ", " : "");
    chordLabel.setText (s, juce::dontSendNotification);
}

// ======================================================================= SETTINGS

SettingsPage::SettingsPage (PrismProcessor& p, std::function<void (double)> fn) : Page (p), setScaleFn (std::move (fn))
{
    perf = &panel ("PERFORMANCE");
    perf->cellW = 70; perf->cellH = 76;
    perf->add ("vel_curve"); perf->add ("mpe_pb_range", "MPE Bend");
    perf->add ("pb_range"); perf->add ("quality"); perf->add ("mpe_on", "MPE");
    for (int s : { 75, 100, 125, 150, 175, 200 }) scale.addItem (juce::String (s) + " %", s);
    scale.setSelectedId ((int) std::lround (p.guiScale * 100 / 25) * 25, juce::dontSendNotification);
    scale.onChange = [this] { if (setScaleFn) setScaleFn (scale.getSelectedId() / 100.0); };
    scale.setTitle ("GUI scale");
    tooltips.setToggleState (p.library.settings.tooltips, juce::dontSendNotification);
    tooltips.onClick = [this] { proc.library.settings.tooltips = tooltips.getToggleState(); proc.library.settings.save(); };
    auto pick = [this] (bool presets) {
        chooser = std::make_unique<juce::FileChooser> (presets ? "User preset folder" : "User wavetable folder",
                                                       presets ? proc.library.settings.presetFolder : proc.library.settings.tableFolder);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, presets] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.isDirectory()) return;
            (presets ? proc.library.settings.presetFolder : proc.library.settings.tableFolder) = f;
            proc.library.settings.save();
            proc.library.rescan();
            updateLabels();
        });
    };
    presetFolder.onClick = [pick] { pick (true); };
    tableFolder.onClick = [pick] { pick (false); };
    auto ccMenu = [this] (bool a) {
        juce::PopupMenu m;
        for (int c = 1; c < 120; ++c) if (c != 64 && c != 74) m.addItem (c, "CC " + juce::String (c));
        m.showMenuAsync ({}, [this, a] (int r) {
            if (r <= 0) return;
            auto x = proc.getExtras();
            (a ? x.ccA : x.ccB) = r;
            proc.setExtras (x);
            updateLabels();
        });
    };
    ccA.onClick = [ccMenu] { ccMenu (true); };
    ccB.onClick = [ccMenu] { ccMenu (false); };
    for (juce::Component* c : { (juce::Component*) &scale, (juce::Component*) &tooltips, (juce::Component*) &presetFolder,
                                (juce::Component*) &tableFolder, (juce::Component*) &ccA, (juce::Component*) &ccB, (juce::Component*) &info })
        addAndMakeVisible (c);
    info.setColour (juce::Label::textColourId, col::dim);
    info.setJustificationType (juce::Justification::topLeft);
    updateLabels();
}

void SettingsPage::updateLabels()
{
    const auto x = proc.getExtras();
    presetFolder.setButtonText ("Presets: " + proc.library.settings.presetFolder.getFullPathName());
    tableFolder.setButtonText ("Wavetables: " + proc.library.settings.tableFolder.getFullPathName());
    ccA.setButtonText ("CC A source: CC " + juce::String (x.ccA));
    ccB.setButtonText ("CC B source: CC " + juce::String (x.ccB));
    info.setText ("PRISM 1.0  -  middle C (MIDI 60) is C3.\nKnobs: drag, Cmd-drag fine, double-click reset, click the value to type,\n"
                  "Alt-drag a modulated knob to change its modulation depth, right-click for more.\n"
                  "Drag a modulation handle (LFO, envelope, MSEG, macro) onto any knob to modulate it.",
                  juce::dontSendNotification);
}

void SettingsPage::resized()
{
    auto r = getLocalBounds().reduced (6);
    perf->setBounds (r.removeFromTop (150));
    r.removeFromTop (10);
    auto row = [&r] { auto x = r.removeFromTop (30); r.removeFromTop (6); return x; };
    auto a = row(); a.removeFromLeft (4);
    scale.setBounds (a.removeFromLeft (120));
    a.removeFromLeft (12);
    tooltips.setBounds (a.removeFromLeft (200));
    presetFolder.setBounds (row().withWidth (600));
    tableFolder.setBounds (row().withWidth (600));
    auto c = row();
    ccA.setBounds (c.removeFromLeft (220));
    c.removeFromLeft (8);
    ccB.setBounds (c.removeFromLeft (220));
    info.setBounds (r.removeFromTop (120));
}
} // namespace prism::gui
