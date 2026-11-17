#include "PresetBrowser.h"

#include "MainView.h"

namespace lens::gui
{
PresetBrowser::PresetBrowser (EditorContext& c, MainView& m) : ctx (c), main (m), manager (c.proc.coachSettings())
{
    search.setTextToShowWhenEmpty ("Search", theme::textDim);
    search.setTitle ("Search presets");
    search.onTextChange = [this] { refilter(); };
    addAndMakeVisible (search);

    category.addItem ("All categories", 1);
    int id = 2;
    for (const auto& cat : presets::PresetManager::categories())
        category.addItem (cat, id++);
    category.setSelectedId (1, juce::dontSendNotification);
    category.setTitle ("Category");
    category.onChange = [this] { refilter(); };
    addAndMakeVisible (category);

    favoritesOnly.setClickingTogglesState (true);
    favoritesOnly.onClick = [this] { refilter(); };
    addAndMakeVisible (favoritesOnly);

    listBox.setRowHeight (40);
    listBox.setColour (juce::ListBox::backgroundColourId, theme::panel);
    listBox.setTitle ("Presets");
    addAndMakeVisible (listBox);

    saveName.setTextToShowWhenEmpty ("New preset name", theme::textDim);
    saveName.setTitle ("Preset name");
    addAndMakeVisible (saveName);
    for (auto* b : { &loadBtn, &saveBtn, &saveAsBtn, &closeBtn })
        addAndMakeVisible (*b);
    loadBtn.onClick = [this] { load (listBox.getSelectedRow()); };
    saveBtn.onClick = [this] { saveCurrent (false); };
    saveAsBtn.onClick = [this] { saveCurrent (true); };
    closeBtn.onClick = [this] { close(); };

    main.onPresetNameClicked = [this] { isVisible() ? close() : open(); };
    main.onPrevPreset = [this] { step (-1); };
    main.onNextPreset = [this] { step (1); };
    refilter();
}

PresetBrowser::~PresetBrowser()
{
    main.onPresetNameClicked = nullptr;
    main.onPrevPreset = main.onNextPreset = nullptr;
}

void PresetBrowser::open()
{
    manager.rescan();
    refilter();
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void PresetBrowser::close()
{
    setVisible (false);
}

void PresetBrowser::refilter()
{
    const auto cat = category.getSelectedId() > 1 ? category.getText() : juce::String();
    visible = manager.filtered (search.getText(), cat, favoritesOnly.getToggleState());
    listBox.updateContent();
    const auto current = ctx.proc.getSession().presetName;
    for (int r = 0; r < (int) visible.size(); ++r)
        if (manager.all()[(size_t) visible[(size_t) r]].name == current)
            listBox.selectRow (r);
    repaint();
}

void PresetBrowser::load (int row)
{
    if (row < 0 || row >= (int) visible.size())
        return;
    const auto& e = manager.all()[(size_t) visible[(size_t) row]];
    ctx.proc.loadPreset (manager.jsonFor (e));
    main.refreshPresetLabel();
    ctx.uiEvent ("preset loaded");
}

void PresetBrowser::step (int delta)
{
    manager.rescan();
    const auto& all = manager.all();
    if (all.empty())
        return;
    int idx = manager.indexOfName (ctx.proc.getSession().presetName);
    idx = idx < 0 ? 0 : (idx + delta + (int) all.size()) % (int) all.size();
    ctx.proc.loadPreset (manager.jsonFor (all[(size_t) idx]));
    main.refreshPresetLabel();
}

void PresetBrowser::saveCurrent (bool asNew)
{
    const auto current = ctx.proc.getSession().presetName;
    const int idx = manager.indexOfName (current);
    if (! asNew && idx >= 0 && ! manager.all()[(size_t) idx].factory)
    {
        manager.save (idx, ctx.proc.getPresetJson());
        return;
    }
    auto name = saveName.getText().trim();
    if (name.isEmpty())
    {
        saveName.grabKeyboardFocus();
        return;
    }
    auto s = ctx.proc.getSession();
    s.presetName = name;
    ctx.proc.setSession (s);
    manager.saveAs (name, ctx.proc.getPresetJson());
    saveName.clear();
    main.refreshPresetLabel();
    refilter();
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    // A click on the star toggles the favourite.
    if (row >= 0 && row < (int) visible.size() && e.x > listBox.getWidth() - 40)
    {
        manager.toggleFavorite (manager.all()[(size_t) visible[(size_t) row]]);
        refilter();
    }
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) visible.size())
        return;
    const auto& e = manager.all()[(size_t) visible[(size_t) row]];
    if (selected)
    {
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillRect (0, 0, w, h);
    }
    g.setColour (theme::text);
    g.setFont (theme::font (13.5f, true));
    g.drawText (e.name, 10, 3, w - 150, 18, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (e.category + (e.factory ? "" : "  (user)"), w - 140, 3, 96, 18, juce::Justification::centredRight);
    g.drawText (e.note, 10, 21, w - 50, 16, juce::Justification::centredLeft);
    g.setColour (manager.isFavorite (e) ? theme::accent : theme::outline);
    g.setFont (theme::font (16.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x98\x85"), w - 36, 0, 30, h, juce::Justification::centred);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::background.withAlpha (0.6f));
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().reduced (120, 30).toFloat(), 8.0f);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (120, 30).reduced (12);
    auto top = r.removeFromTop (26);
    search.setBounds (top.removeFromLeft (260));
    top.removeFromLeft (8);
    category.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (8);
    favoritesOnly.setBounds (top.removeFromLeft (110));
    closeBtn.setBounds (top.removeFromRight (70));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (28);
    loadBtn.setBounds (bottom.removeFromLeft (70));
    bottom.removeFromLeft (8);
    saveBtn.setBounds (bottom.removeFromLeft (70));
    bottom.removeFromLeft (16);
    saveName.setBounds (bottom.removeFromLeft (220));
    bottom.removeFromLeft (8);
    saveAsBtn.setBounds (bottom.removeFromLeft (80));
    r.removeFromBottom (8);
    listBox.setBounds (r);
}
} // namespace lens::gui
