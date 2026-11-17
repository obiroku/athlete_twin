#pragma once

#include "EditorContext.h"
#include "Theme.h"

#include "DSP/FilterDesign.h"

namespace lens::gui
{
// The centre of LENS: analyzer, EQ curve, band dots, peak markers, zone
// and piano strips (SPEC §6, §12).
class SpectrumDisplay final : public juce::Component,
                              public juce::SettableTooltipClient,
                              private juce::ChangeListener
{
public:
    explicit SpectrumDisplay (EditorContext&);
    ~SpectrumDisplay() override;

    // Called from the editor's vblank: repaints only when something changed.
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Ear trainer (Phase 7): when set, clicks become guesses.
    std::function<void (double freq)> onEarGuess;
    bool earTrainerActive = false;

    // Ghost bands from EQ Match (Phase 7).
    struct Ghost
    {
        float freq, gain, q;
    };
    std::vector<Ghost> ghosts;
    std::function<void()> onMatchClicked;

    // Coordinate helpers (public for tests and the Coach).
    float freqToX (double f) const;
    double xToFreq (float x) const;
    float gainToY (double db) const;
    double yToGain (float y) const;
    float analyzerToY (double db) const;
    juce::Rectangle<float> plotArea() const { return plot; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void syncControls();

    struct BandView
    {
        int index = -1;
        dsp::BandParams p;
        float dynGainDb = 0.0f;
        juce::Point<float> pos;
    };
    void gatherBands();
    int bandAt (juce::Point<float>) const;
    int peakAt (juce::Point<float>) const;
    double curveDbAt (double f) const;
    dsp::BandDesign designFor (const dsp::BandParams&, double gainDb) const;
    void showBandMenu (int band);
    void addBellAt (double freq);
    void drawGrid (juce::Graphics&);
    void drawAnalyzer (juce::Graphics&);
    void drawCurve (juce::Graphics&);
    void drawBands (juce::Graphics&);
    void drawPeaks (juce::Graphics&);
    void drawStrips (juce::Graphics&);
    void drawHover (juce::Graphics&);

    EditorContext& ctx;
    juce::Rectangle<float> plot, zoneStrip, pianoStrip;
    double fs = 48000.0, fMin = 10.0, fMax = 24000.0;
    double eqRange = 24.0;
    bool linearPhase = false;

    std::vector<BandView> bands;
    SpectrumAnalyzer::Snapshot snapshot;
    std::vector<PeakMarker> peakMarkers;
    juce::String paramsHash;
    int lastAnalyzerVersion = -1;
    std::array<float, dsp::kNumBands> lastDyn {};

    // Controls row.
    juce::ToggleButton preBtn { "Pre" }, postBtn { "Post" }, scBtn { "SC" }, freezeBtn { "Freeze" };
    juce::TextButton matchBtn { "EQ Match" };
    juce::ComboBox smoothBox, speedBox, tiltBox, rangeBox;

    // Interaction.
    juce::Point<float> hover { -1.0f, -1.0f };
    struct DragStart
    {
        int band;
        double freq, gain;
    };
    std::vector<DragStart> dragStart;
    bool dragging = false, soloDrag = false;
    juce::Point<float> dragOrigin;
};
} // namespace lens::gui
