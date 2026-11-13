#include "TopBar.h"

namespace prism::gui
{
// ======================================================================= browser

PresetBrowser::PresetBrowser (PrismProcessor& p) : proc (p)
{
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, col::panel);
    addAndMakeVisible (list);
    setTitle ("Preset browser");
    refresh();
}

void PresetBrowser::setFilter (const juce::String& s, const juce::String& c, bool f)
{
    search = s; category = c; favOnly = f;
    refresh();
}

void PresetBrowser::refresh()
{
    shown.clear();
    const auto& e = proc.library.entries();
    const auto& favs = proc.library.settings.favourites;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const auto& x = e[i];
        if (category.isNotEmpty() && x.category != category) continue;
        if (favOnly && ! favs.contains (x.key())) continue;
        if (search.isNotEmpty() && ! x.name.containsIgnoreCase (search) && ! x.category.containsIgnoreCase (search)
            && ! x.tags.joinIntoString (" ").containsIgnoreCase (search)) continue;
        shown.push_back ((int) i);
    }
    list.updateContent();
    list.repaint();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6);
    g.setColour (col::accent);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6, 1);
}

void PresetBrowser::resized() { list.setBounds (getLocalBounds().reduced (6)); }

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto& e = proc.library.entries()[(size_t) shown[(size_t) row]];
    if (selected) { g.setColour (col::accentDim); g.fillRect (0, 0, w, h); }
    const bool fav = proc.library.settings.favourites.contains (e.key());
    g.setColour (fav ? col::accent : col::dim);
    g.setFont (uiFont (13.0f));
    g.drawText (fav ? "*" : "", 4, 0, 14, h, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.drawText (e.name, 20, 0, w / 2, h, juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.drawText (e.category + (e.factory ? "" : "  (user)"), w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.x < 18)
    {
        auto& favs = proc.library.settings.favourites;
        const auto key = proc.library.entries()[(size_t) shown[(size_t) row]].key();
        if (favs.contains (key)) favs.removeString (key); else favs.add (key);
        proc.library.settings.save();
        list.repaintRow (row);
    }
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { loadRow (row); }

void PresetBrowser::loadRow (int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    PresetData d; juce::String err;
    if (proc.library.load (shown[(size_t) row], d, err)) proc.loadPreset (d, true);
    if (onClose) onClose();
}

// ======================================================================= top bar

TopBar::TopBar (PrismProcessor& p, PresetBrowser& b)
    : proc (p), browser (b), quality (p, P::quality), volume (p, P::master_vol, "Vol")
{
    panic.setButtonText ("!");
    quality.getProperties().remove ("caption");
    for (juce::Component* c : { (juce::Component*) &name, (juce::Component*) &prev, (juce::Component*) &next, (juce::Component*) &fav,
                                (juce::Component*) &save, (juce::Component*) &saveAs, (juce::Component*) &ab, (juce::Component*) &undo,
                                (juce::Component*) &redo, (juce::Component*) &dice, (juce::Component*) &locks, (juce::Component*) &panic,
                                (juce::Component*) &searchBox, (juce::Component*) &category, (juce::Component*) &quality,
                                (juce::Component*) &volume, (juce::Component*) &cpu, (juce::Component*) &voices })
        addAndMakeVisible (c);
    name.setTooltip ("Open the preset browser");
    name.onClick = [this] { openBrowser(); };
    prev.onClick = [this] { loadIndex (currentIndex <= 0 ? (int) proc.library.entries().size() - 1 : currentIndex - 1); };
    next.onClick = [this] { loadIndex ((currentIndex + 1) % std::max (1, (int) proc.library.entries().size())); };
    fav.setTooltip ("Favourite");
    fav.onClick = [this] {
        if (currentIndex < 0) return;
        auto& favs = proc.library.settings.favourites;
        const auto key = proc.library.entries()[(size_t) currentIndex].key();
        if (favs.contains (key)) favs.removeString (key); else favs.add (key);
        proc.library.settings.save();
        browser.refresh();
    };
    save.onClick = [this] { doSave (false); };
    saveAs.onClick = [this] { doSave (true); };
    ab.setTooltip ("Compare: toggle between slot A and slot B");
    ab.onClick = [this] { proc.toggleAB(); ab.setButtonText (proc.currentABSlot() == 0 ? "A/B: A" : "A/B: B"); };
    undo.onClick = [this] { proc.undo(); };
    redo.onClick = [this] { proc.redo(); };
    dice.setTooltip ("Randomize (respects section locks)");
    dice.onClick = [this] { proc.loadPreset (randomizePreset (proc.currentPreset(), lockState, (uint64_t) juce::Time::getHighResolutionTicks()), true); };
    locks.onClick = [this] {
        juce::PopupMenu m;
        m.addItem (1, "Lock Osc", true, lockState.osc);
        m.addItem (2, "Lock Filter", true, lockState.filter);
        m.addItem (3, "Lock Env", true, lockState.env);
        m.addItem (4, "Lock Mod", true, lockState.mod);
        m.addItem (5, "Lock FX", true, lockState.fx);
        m.addItem (6, "Lock Arp", true, lockState.arp);
        m.addSeparator();
        m.addItem (10, "Mutate (small)"); m.addItem (11, "Mutate (medium)"); m.addItem (12, "Mutate (large)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&locks), [this] (int r) {
            switch (r)
            {
                case 1: lockState.osc = ! lockState.osc; break;
                case 2: lockState.filter = ! lockState.filter; break;
                case 3: lockState.env = ! lockState.env; break;
                case 4: lockState.mod = ! lockState.mod; break;
                case 5: lockState.fx = ! lockState.fx; break;
                case 6: lockState.arp = ! lockState.arp; break;
                case 10: case 11: case 12:
                    proc.loadPreset (mutatePreset (proc.currentPreset(), r == 10 ? 0.15f : (r == 11 ? 0.35f : 0.7f), lockState,
                                                   (uint64_t) juce::Time::getHighResolutionTicks()), true);
                    break;
                default: break;
            }
        });
    };
    panic.setTooltip ("All notes off");
    panic.onClick = [this] { proc.panic(); };
    searchBox.setTextToShowWhenEmpty ("search presets", col::dim);
    searchBox.onTextChange = [this] { browser.setFilter (searchBox.getText(), category.getSelectedId() > 1 ? category.getText() : juce::String(), category.getSelectedId() == 1000); openBrowser(); };
    category.addItem ("All categories", 1);
    for (int i = 0; i < PresetLibrary::categories().size(); ++i) category.addItem (PresetLibrary::categories()[i], i + 2);
    category.addItem ("Favourites", 1000);
    category.setSelectedId (1, juce::dontSendNotification);
    category.onChange = [this] { searchBox.onTextChange(); };
    cpu.setFont (uiFont (11.0f)); voices.setFont (uiFont (11.0f));
    cpu.setColour (juce::Label::textColourId, col::dim); voices.setColour (juce::Label::textColourId, col::dim);
    setTitle ("Top bar");
}

void TopBar::openBrowser()
{
    browser.setVisible (true);
    browser.toFront (false);
}

void TopBar::loadIndex (int i)
{
    PresetData d; juce::String err;
    if (proc.library.load (i, d, err)) { proc.loadPreset (d, true); currentIndex = i; }
}

void TopBar::doSave (bool as)
{
    auto d = proc.currentPreset();
    const bool factory = currentIndex >= 0 && currentIndex < (int) proc.library.entries().size() && proc.library.entries()[(size_t) currentIndex].factory;
    if (! as && ! factory && currentIndex >= 0)
    {
        juce::String err;
        proc.library.saveUser (d, err);
        return;
    }
    auto* w = new juce::AlertWindow ("Save preset", "Name and category", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", d.name);
    w->addComboBox ("cat", PresetLibrary::categories());
    if (auto* cb = w->getComboBoxComponent ("cat")) cb->setText (d.category);
    w->addButton ("Save", 1);
    w->addButton ("Cancel", 0);
    juce::Component::SafePointer<TopBar> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r) {
        if (safe == nullptr || r != 1) return;
        auto d2 = safe->proc.currentPreset();
        d2.name = w->getTextEditorContents ("name").toStdString();
        if (auto* cb = w->getComboBoxComponent ("cat")) d2.category = cb->getText().toStdString();
        safe->proc.setPresetMeta (d2.name, d2.category);
        juce::String err;
        if (! safe->proc.library.saveUser (d2, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", err);
        safe->currentIndex = safe->proc.library.indexOf (juce::String (d2.name));
        safe->browser.refresh();
    }), true);
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRect (getLocalBounds());
    g.setColour (col::accent);
    g.setFont (uiFont (18.0f, true));
    g.drawText ("PRISM", 10, 0, 70, getHeight(), juce::Justification::centredLeft);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (4, 7);
    r.removeFromLeft (76);
    auto put = [&r] (juce::Component& c, int w) { c.setBounds (r.removeFromLeft (w)); r.removeFromLeft (3); };
    put (prev, 22); put (name, 130); put (next, 22); put (fav, 22);
    put (searchBox, 84); put (category, 84);
    put (save, 40); put (saveAs, 52); put (ab, 48); put (undo, 40); put (redo, 40); put (dice, 54); put (locks, 42);
    auto right = r;
    panic.setBounds (right.removeFromRight (30)); right.removeFromRight (3);
    volume.setBounds (right.removeFromRight (34).expanded (0, 7)); right.removeFromRight (2);
    cpu.setBounds (right.removeFromRight (66));
    quality.setBounds (right.removeFromRight (std::max (40, right.getWidth())));
    voices.setVisible (false);
}

void TopBar::refreshLive()
{
    volume.refreshLive();
    if (++frame % 6 != 0 && frame > 2) return; // ~10 Hz for text
    const auto n = proc.getPresetName();
    if (n != shownName) { shownName = n; name.setButtonText (n); currentIndex = proc.library.indexOf (n); }
    cpu.setText ("CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%  " + juce::String (proc.engine.voiceCount.load()) + "v",
                 juce::dontSendNotification);
    const bool isFav = currentIndex >= 0 && proc.library.settings.favourites.contains (proc.library.entries()[(size_t) currentIndex].key());
    fav.setToggleState (isFav, juce::dontSendNotification);
    undo.setEnabled (proc.canUndo());
    redo.setEnabled (proc.canRedo());
}
} // namespace prism::gui
