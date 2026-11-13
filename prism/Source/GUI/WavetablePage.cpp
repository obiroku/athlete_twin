#include "WavetablePage.h"
#include "../Engine/Formula.h"
#include "../Engine/TableIO.h"
#include "../DSP/Wavetable.h"
#include "../DSP/FactoryTables.h"
#include <thread>

namespace prism::gui
{
class WavetablePage::FrameEditor : public juce::Component
{
public:
    explicit FrameEditor (WavetablePage& o) : owner (o) { setTitle ("Frame drawing area"); setDescription ("Draw the current frame with the mouse"); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (r, 6);
        g.setColour (col::line);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        if (owner.work.empty()) return;
        // ghost of neighbouring frames
        const int F = (int) owner.work.size();
        for (int d : { -8, 8 })
        {
            const int f = owner.currentFrame + d;
            if (f < 0 || f >= F) continue;
            g.setColour (col::dim.withAlpha (0.25f));
            g.strokePath (pathFor (owner.work[(size_t) f], r), juce::PathStrokeType (1.0f));
        }
        g.setColour (owner.keys[(size_t) owner.currentFrame] ? col::text : col::accent);
        g.strokePath (pathFor (owner.work[(size_t) owner.currentFrame], r), juce::PathStrokeType (2.0f));
    }
    juce::Path pathFor (const std::vector<float>& fr, juce::Rectangle<float> r)
    {
        juce::Path p;
        const int W = std::max (2, (int) r.getWidth());
        for (int i = 0; i <= W; ++i)
        {
            const float v = fr[(size_t) std::min (2047, i * 2048 / W)];
            const float y = r.getCentreY() - juce::jlimit (-1.2f, 1.2f, v) * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (r.getX() + (float) i, y); else p.lineTo (r.getX() + (float) i, y);
        }
        return p;
    }
    void mouseDown (const juce::MouseEvent& e) override { last = e.position; drawTo (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { drawTo (e.position); }
    void mouseUp (const juce::MouseEvent&) override { owner.status.setText ("Frame " + juce::String (owner.currentFrame + 1) + " edited (Apply to hear it)", juce::dontSendNotification); }
    void drawTo (juce::Point<float> p)
    {
        if (owner.work.empty()) return;
        auto r = getLocalBounds().toFloat();
        auto toIdx = [&] (float x) { return juce::jlimit (0, 2047, (int) ((x - r.getX()) / r.getWidth() * 2048.0f)); };
        auto toVal = [&] (float y) { return juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - y) / (r.getHeight() * 0.42f)); };
        int i0 = toIdx (last.x), i1 = toIdx (p.x);
        float v0 = toVal (last.y), v1 = toVal (p.y);
        if (i1 < i0) { std::swap (i0, i1); std::swap (v0, v1); }
        auto& fr = owner.work[(size_t) owner.currentFrame];
        for (int i = i0; i <= i1; ++i) fr[(size_t) i] = i1 == i0 ? v1 : v0 + (v1 - v0) * (float) (i - i0) / (float) (i1 - i0);
        last = p;
        repaint();
    }
    WavetablePage& owner;
    juce::Point<float> last;
};

WavetablePage::WavetablePage (PrismProcessor& p) : Page (p)
{
    frameEditor = std::make_unique<FrameEditor> (*this);
    addAndMakeVisible (*frameEditor);
    oscSel.addItem ("Osc A", 1); oscSel.addItem ("Osc B", 2);
    oscSel.setSelectedId (1, juce::dontSendNotification);
    oscSel.setTitle ("Target oscillator");
    importMode.addItem ("Auto", 1); importMode.addItem ("Fixed frames", 2); importMode.addItem ("Pitch detect", 3); importMode.addItem ("Single cycle", 4);
    importMode.setSelectedId (1, juce::dontSendNotification);
    importMode.setTitle ("Import mode");
    for (int n : { 1, 2, 4, 8, 16, 32, 64, 128, 256 }) framesSel.addItem (juce::String (n) + " frames", n);
    framesSel.setSelectedId (64, juce::dontSendNotification);
    framesSel.setTitle ("Frame count");
    const auto& names = dsp::factoryTableNames();
    for (auto* c : { &morphA, &morphB })
    {
        c->addItem ("(current work)", 1000);
        for (size_t i = 0; i < names.size(); ++i) c->addItem (names[i], (int) i + 1);
    }
    morphA.setSelectedId (1000, juce::dontSendNotification);
    morphB.setSelectedId (dsp::factoryTableIndex ("Glass") + 1, juce::dontSendNotification);
    frameSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    frameSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    frameSlider.setRange (1, 2, 1);
    frameSlider.onValueChange = [this] { currentFrame = juce::jlimit (0, std::max (0, (int) work.size() - 1), (int) frameSlider.getValue() - 1); frameEditor->repaint(); };
    frameSlider.setTitle ("Frame");
    formula.setText ("sin(2*pi*x) + 0.5*y*sin(6*pi*x)", false);
    formula.setFont (uiFont (14.0f));
    formula.setTitle ("Formula");
    formula.setTooltip ("x = phase 0..1, y = frame 0..1; sin cos tan abs sqrt pow exp log min max floor sign tri saw square rand");
    status.setColour (juce::Label::textColourId, col::dim);

    loadCurrent.onClick = [this] {
        auto f = proc.tables.displayFrames (oscSel.getSelectedId() - 1);
        if (f) setWork (*f, "Loaded " + juce::String ((int) f->size()) + " frames from the oscillator");
    };
    keyBtn.onClick = [this] {
        if (work.empty()) return;
        keys[(size_t) currentFrame] = ! keys[(size_t) currentFrame];
        frameEditor->repaint();
        status.setText (keys[(size_t) currentFrame] ? "Frame " + juce::String (currentFrame + 1) + " is a keyframe" : "Keyframe removed", juce::dontSendNotification);
    };
    interp.onClick = [this] {
        std::vector<int> k;
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i]) k.push_back ((int) i);
        if (k.size() < 2) { status.setText ("Set at least two keyframes", juce::dontSendNotification); return; }
        for (size_t s = 0; s + 1 < k.size(); ++s)
            for (int f = k[s] + 1; f < k[s + 1]; ++f)
            {
                const float t = (float) (f - k[s]) / (float) (k[s + 1] - k[s]);
                for (int i = 0; i < 2048; ++i)
                    work[(size_t) f][(size_t) i] = work[(size_t) k[s]][(size_t) i] + (work[(size_t) k[s + 1]][(size_t) i] - work[(size_t) k[s]][(size_t) i]) * t;
            }
        frameEditor->repaint();
        status.setText ("Interpolated between " + juce::String ((int) k.size()) + " keyframes", juce::dontSendNotification);
    };
    apply.onClick = [this] { applyToOsc ("Drawn"); };
    formulaApply.onClick = [this] {
        const auto text = formula.getText();
        const int frames = framesSel.getSelectedId();
        status.setText ("Evaluating...", juce::dontSendNotification);
        juce::Component::SafePointer<WavetablePage> safe (this);
        // evaluated on a background thread, then swapped in atomically by the table manager
        std::thread ([text, frames, safe] {
            std::vector<std::vector<float>> out;
            juce::String err;
            const bool ok = buildFromFormula (text, frames, out, err);
            juce::MessageManager::callAsync ([safe, ok, out = std::move (out), err] () mutable {
                if (safe == nullptr) return;
                if (! ok) { safe->status.setText ("Formula error: " + err, juce::dontSendNotification); return; }
                safe->setWork (std::move (out), "Formula OK");
                safe->applyToOsc ("Formula");
            });
        }).detach();
    };
    importBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import wavetable", proc.library.settings.tableFolder, "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            const ImportMode modes[] = { ImportMode::Auto, ImportMode::Fixed, ImportMode::PitchDetect, ImportMode::SingleCycle };
            auto r = importWavetable (f, modes[importMode.getSelectedId() - 1], framesSel.getSelectedId());
            if (! r.ok) { status.setText ("Import failed: " + r.error, juce::dontSendNotification); return; }
            setWork (r.frames, "Preview: " + r.name + " (" + juce::String ((int) r.frames.size()) + " frames" +
                     (r.clmFrameSize > 0 ? ", clm " + juce::String (r.clmFrameSize) : juce::String()) + ") - Apply to use it");
        });
    };
    morphBtn.onClick = [this] {
        const auto a = tableFrames (morphA.getSelectedId()), b = tableFrames (morphB.getSelectedId());
        if (a.empty() || b.empty()) return;
        const int n = framesSel.getSelectedId();
        std::vector<std::vector<float>> out ((size_t) n, std::vector<float> (2048));
        for (int k = 0; k < n; ++k)
        {
            const float t = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
            const auto& fa = a[(size_t) std::min ((int) a.size() - 1, (int) std::lround (t * (float) (a.size() - 1)))];
            const auto& fb = b[(size_t) std::min ((int) b.size() - 1, (int) std::lround (t * (float) (b.size() - 1)))];
            for (int i = 0; i < 2048; ++i) out[(size_t) k][(size_t) i] = fa[(size_t) i] + (fb[(size_t) i] - fa[(size_t) i]) * t;
        }
        dsp::applyTableHygiene (out);
        setWork (std::move (out), "Morphed table - Apply to use it");
    };
    exportBtn.onClick = [this] {
        if (work.empty()) return;
        chooser = std::make_unique<juce::FileChooser> ("Export wavetable", proc.library.settings.tableFolder.getChildFile ("PRISM table.wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            auto frames = work;
            dsp::applyTableHygiene (frames);
            status.setText (exportWavetable (f, frames) ? "Exported " + f.getFileName() : "Export failed", juce::dontSendNotification);
        });
    };
    for (juce::Component* c : { (juce::Component*) &oscSel, (juce::Component*) &importMode, (juce::Component*) &framesSel, (juce::Component*) &morphA,
                                (juce::Component*) &morphB, (juce::Component*) &frameSlider, (juce::Component*) &loadCurrent, (juce::Component*) &keyBtn,
                                (juce::Component*) &interp, (juce::Component*) &apply, (juce::Component*) &formulaApply, (juce::Component*) &importBtn,
                                (juce::Component*) &morphBtn, (juce::Component*) &exportBtn, (juce::Component*) &formula, (juce::Component*) &status })
        addAndMakeVisible (c);
}

WavetablePage::~WavetablePage() { *alive = false; }

bool WavetablePage::buildFromFormula (const juce::String& text, int frames, std::vector<std::vector<float>>& out, juce::String& error)
{
    std::string err;
    auto f = Formula::parse (text.toStdString(), err);
    if (f == nullptr) { error = err; return false; }
    out = f->render (frames);
    dsp::applyTableHygiene (out);
    return true;
}

std::vector<std::vector<float>> WavetablePage::tableFrames (int id)
{
    if (id == 1000) return work;
    if (id <= 0) return {};
    auto f = dsp::generateFactoryFrames (id - 1, 64);
    dsp::applyTableHygiene (f);
    return f;
}

void WavetablePage::setWork (std::vector<std::vector<float>> f, const juce::String& msg)
{
    work = std::move (f);
    keys.assign (work.size(), false);
    if (! keys.empty()) { keys.front() = true; keys.back() = true; }
    currentFrame = 0;
    frameSlider.setRange (1, std::max (2.0, (double) work.size()), 1);
    frameSlider.setEnabled (work.size() > 1);
    frameSlider.setValue (1, juce::dontSendNotification);
    status.setText (msg, juce::dontSendNotification);
    frameEditor->repaint();
}

void WavetablePage::applyToOsc (const juce::String& name)
{
    if (work.empty()) return;
    TableSource s;
    s.name = name.toStdString();
    s.frames = work;
    dsp::applyTableHygiene (s.frames);
    proc.setTableSource (oscSel.getSelectedId() - 1, s); // generated in the background, swapped atomically
    status.setText ("Applied " + juce::String ((int) work.size()) + " frames to " + oscSel.getText(), juce::dontSendNotification);
}

void WavetablePage::refreshLive()
{
    if (work.empty())
        if (auto f = proc.tables.displayFrames (0)) setWork (*f, "Editing the table of Osc A");
}

void WavetablePage::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().reduced (6).toFloat(), 6);
}

void WavetablePage::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto row1 = r.removeFromTop (28);
    oscSel.setBounds (row1.removeFromLeft (90)); row1.removeFromLeft (6);
    loadCurrent.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (6);
    framesSel.setBounds (row1.removeFromLeft (110)); row1.removeFromLeft (6);
    apply.setBounds (row1.removeFromLeft (110)); row1.removeFromLeft (6);
    exportBtn.setBounds (row1.removeFromLeft (110));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (120);
    auto sliderRow = r.removeFromBottom (30);
    frameSlider.setBounds (sliderRow.removeFromLeft (sliderRow.getWidth() - 330));
    sliderRow.removeFromLeft (6);
    keyBtn.setBounds (sliderRow.removeFromLeft (140)); sliderRow.removeFromLeft (6);
    interp.setBounds (sliderRow);
    r.removeFromBottom (6);
    frameEditor->setBounds (r);
    auto f = bottom.removeFromTop (28);
    formula.setBounds (f.removeFromLeft (f.getWidth() - 130)); f.removeFromLeft (6);
    formulaApply.setBounds (f);
    bottom.removeFromTop (8);
    auto im = bottom.removeFromTop (28);
    importMode.setBounds (im.removeFromLeft (130)); im.removeFromLeft (6);
    importBtn.setBounds (im.removeFromLeft (120)); im.removeFromLeft (24);
    morphA.setBounds (im.removeFromLeft (150)); im.removeFromLeft (6);
    morphB.setBounds (im.removeFromLeft (150)); im.removeFromLeft (6);
    morphBtn.setBounds (im.removeFromLeft (120));
    bottom.removeFromTop (8);
    status.setBounds (bottom.removeFromTop (24));
}
} // namespace prism::gui
