#include "MatchPanel.h"

#include "MainView.h"
#include "Util/Format.h"

namespace lens::gui
{
MatchPanel::MatchPanel (EditorContext& c, MainView& m) : ctx (c), main (m)
{
    for (auto* b : { &loadBtn, &captureBtn, &suggestBtn, &closeBtn })
        addAndMakeVisible (*b);
    referenceBox.setTitle ("Reference");
    referenceBox.setTextWhenNothingSelected ("No reference loaded");
    sourceBox.addItemList ({ "My track", "Side-chain" }, 1);
    sourceBox.setSelectedItemIndex (0, juce::dontSendNotification);
    sourceBox.setTitle ("Capture source");
    addAndMakeVisible (referenceBox);
    addAndMakeVisible (sourceBox);
    status.setFont (theme::font (12.0f));
    status.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (status);

    loadBtn.onClick = [this] { loadReferenceFile(); };
    closeBtn.onClick = [this] { close(); };
    suggestBtn.onClick = [this] { runSuggest(); };
    captureBtn.onClick = [this]
    {
        auto& an = ctx.analyzer.analyzer();
        if (an.isCapturing())
        {
            an.stopCapture();
            mine = an.captureResult ("My track");
            captureBtn.setButtonText ("Start capture");
        }
        else
        {
            an.startCapture (sourceBox.getSelectedItemIndex() == 1 ? SpectrumAnalyzer::Sidechain : SpectrumAnalyzer::Post);
            captureBtn.setButtonText ("Stop capture");
        }
    };
    main.display().onMatchClicked = [this] { isVisible() ? close() : open(); };
    refreshReferences();
}

MatchPanel::~MatchPanel()
{
    stopTimer();
    main.display().onMatchClicked = nullptr;
    main.display().ghosts.clear();
}

void MatchPanel::open()
{
    refreshReferences();
    setVisible (true);
    toFront (true);
    startTimerHz (4);
}

void MatchPanel::close()
{
    setVisible (false);
    stopTimer();
}

void MatchPanel::refreshReferences()
{
    const auto current = referenceBox.getText();
    references = match::loadReferences();
    referenceBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < references.size(); ++i)
        referenceBox.addItem (references[i].name, (int) i + 1);
    for (size_t i = 0; i < references.size(); ++i)
        if (references[i].name == current)
            referenceBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);
    if (referenceBox.getSelectedItemIndex() < 0 && ! references.empty())
        referenceBox.setSelectedItemIndex ((int) references.size() - 1, juce::dontSendNotification);
}

void MatchPanel::loadReferenceFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference song", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.mp3");
    juce::Component::SafePointer<MatchPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        const auto file = fc.getResult();
        safe->status.setText ("Analysing " + file.getFileName(), juce::dontSendNotification);
        // Background thread: decoding and analysis never touch audio.
        juce::Thread::launch ([safe, file]
        {
            const auto spec = match::analyseFile (file);
            if (spec.isValid())
                match::saveReference (spec);
            juce::MessageManager::callAsync ([safe, spec]
            {
                if (safe == nullptr)
                    return;
                safe->status.setText (spec.isValid() ? "Loaded " + spec.name : juce::String ("Could not read that file"), juce::dontSendNotification);
                safe->refreshReferences();
                for (int i = 0; i < safe->referenceBox.getNumItems(); ++i)
                    if (safe->referenceBox.getItemText (i) == spec.name)
                        safe->referenceBox.setSelectedItemIndex (i, juce::dontSendNotification);
            });
        });
    });
}

void MatchPanel::runSuggest()
{
    const int r = referenceBox.getSelectedItemIndex();
    if (r < 0 || r >= (int) references.size() || ! mine.isValid())
    {
        status.setText (r < 0 ? "Load a reference first" : "Capture your track first", juce::dontSendNotification);
        return;
    }
    diff = match::difference (references[(size_t) r], mine);
    suggestions = match::suggest (diff);
    status.setText (juce::String ((int) suggestions.size()) + " suggestions", juce::dontSendNotification);
    rebuildRows();
    updateGhosts();
    repaint();
}

void MatchPanel::updateGhosts()
{
    auto& g = main.display().ghosts;
    g.clear();
    for (const auto& s : suggestions)
        g.push_back ({ s.freq, s.gainDb, s.q });
    main.display().repaint();
}

void MatchPanel::rebuildRows()
{
    for (auto& r : rows)
    {
        removeChildComponent (r.text.get());
        removeChildComponent (r.accept.get());
        removeChildComponent (r.discard.get());
    }
    rows.clear();
    for (size_t i = 0; i < suggestions.size(); ++i)
    {
        const auto s = suggestions[i];
        Row row;
        row.text = std::make_unique<juce::Label> ("", "Bell " + fmt::freq (s.freq) + "  " + fmt::gain (s.gainDb) + "  Q " + juce::String (s.q, 2));
        row.text->setFont (theme::font (12.5f));
        row.accept = std::make_unique<juce::TextButton> ("Accept");
        row.discard = std::make_unique<juce::TextButton> ("Discard");
        juce::Component::SafePointer<MatchPanel> safe (this);
        row.accept->onClick = [safe, i, s]
        {
            if (safe == nullptr)
                return;
            dsp::BandParams bp;
            bp.shape = dsp::Shape::Bell;
            bp.freq = s.freq;
            bp.gain = s.gainDb;
            bp.q = s.q;
            const int b = safe->ctx.proc.addBand (bp, "EQ Match");
            if (b >= 0)
                safe->ctx.selection.selectOnly (b);
            juce::MessageManager::callAsync ([safe, i]
            {
                if (safe != nullptr && i < safe->suggestions.size())
                {
                    safe->suggestions.erase (safe->suggestions.begin() + (long) i);
                    safe->rebuildRows();
                    safe->updateGhosts();
                }
            });
        };
        row.discard->onClick = [safe, i]
        {
            juce::MessageManager::callAsync ([safe, i]
            {
                if (safe != nullptr && i < safe->suggestions.size())
                {
                    safe->suggestions.erase (safe->suggestions.begin() + (long) i);
                    safe->rebuildRows();
                    safe->updateGhosts();
                }
            });
        };
        addAndMakeVisible (*row.text);
        addAndMakeVisible (*row.accept);
        addAndMakeVisible (*row.discard);
        rows.push_back (std::move (row));
    }
    resized();
}

void MatchPanel::timerCallback()
{
    auto& an = ctx.analyzer.analyzer();
    if (an.isCapturing())
        status.setText ("Capturing " + juce::String (an.captureSeconds(), 1) + " s", juce::dontSendNotification);
}

void MatchPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background.withAlpha (0.6f));
    const auto box = getLocalBounds().reduced (150, 40).toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText ("EQ Match", box.reduced (12).removeFromTop (24), juce::Justification::centredLeft);

    // Difference curve (reference − mine), 30 Hz – 16 kHz, ±12 dB.
    g.setColour (theme::background);
    g.fillRect (graphArea);
    g.setColour (theme::grid);
    g.drawHorizontalLine (graphArea.getCentreY(), (float) graphArea.getX(), (float) graphArea.getRight());
    if (! diff.db.empty())
    {
        juce::Path p;
        for (size_t i = 0; i < diff.db.size(); ++i)
        {
            const float x = graphArea.getX() + graphArea.getWidth() * (float) (std::log (diff.freqs[i] / 30.0) / std::log (16000.0 / 30.0));
            const float y = graphArea.getCentreY() - (float) juce::jlimit (-12.0, 12.0, diff.db[i]) / 12.0f * graphArea.getHeight() * 0.5f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (theme::accent);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}

void MatchPanel::resized()
{
    auto r = getLocalBounds().reduced (150, 40).reduced (12);
    auto title = r.removeFromTop (24);
    closeBtn.setBounds (title.removeFromRight (70));
    r.removeFromTop (8);
    auto row1 = r.removeFromTop (26);
    loadBtn.setBounds (row1.removeFromLeft (140));
    row1.removeFromLeft (8);
    referenceBox.setBounds (row1.removeFromLeft (260));
    r.removeFromTop (6);
    auto row2 = r.removeFromTop (26);
    sourceBox.setBounds (row2.removeFromLeft (140));
    row2.removeFromLeft (8);
    captureBtn.setBounds (row2.removeFromLeft (120));
    row2.removeFromLeft (8);
    suggestBtn.setBounds (row2.removeFromLeft (90));
    row2.removeFromLeft (8);
    status.setBounds (row2);
    r.removeFromTop (8);
    graphArea = r.removeFromTop (110);
    r.removeFromTop (8);
    for (auto& row : rows)
    {
        auto rr = r.removeFromTop (26);
        row.text->setBounds (rr.removeFromLeft (260));
        row.accept->setBounds (rr.removeFromLeft (70));
        rr.removeFromLeft (6);
        row.discard->setBounds (rr.removeFromLeft (70));
        r.removeFromTop (4);
    }
}
} // namespace lens::gui
