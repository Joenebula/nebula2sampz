#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The step grid: rows of effects, columns of steps, click to paint.
//
// Click cycles a cell 0 -> 3 -> 2 -> 1 -> 0 (paint full first, since that's what you
// usually want), drag paints across. Right-click clears a cell.
//
// LAW 4 (the prototype's, learned the hard way): a control that cannot act must SAY so.
// A row whose knob is at 0 CANNOT sound however hard it's painted — so it renders greyed
// with a "0%" tag rather than letting you paint cells that silently do nothing. Silent
// failure is worse than visible failure.
class GridView final : public juce::Component,
                       private juce::Timer
{
public:
    explicit GridView(Nebula2AudioProcessor& p);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Height comes from Nebula2::gridPanelHeight() — see FxGrid.h for why it lives there
    // (the test binary links that file and not the GUI). Adding a lane must widen the
    // panel, not shrink the rows.
    static int preferredHeight() { return Nebula2::gridPanelHeight() + noteRowHeight; }

    // The melodic lane sits ABOVE the effect lanes, in its own strip: it carries a pitch,
    // not a 0..3 level, so drawing it as another grid row would invite clicking it like one.
    static constexpr int noteRowHeight = 30;

private:
    void timerCallback() override;
    int rowAt(juce::Point<int> pos) const;
    int stepAt(juce::Point<int> pos) const;
    float panelAmountFor(int row) const;   // the knob behind this row
    bool rowIsStarved(int row) const;      // knob at 0 -> the row cannot sound
    int laneHeight() const;                // one definition, shared by paint and rowAt
    void drawNoteRow(juce::Graphics&, Nebula2::FxGrid&, int steps, int gridW, int playing);
    void setNoteFromMouse(juce::Point<int> pos);
    bool editingNotes = false;

    Nebula2AudioProcessor& processorRef;
    int lastStep = -2;
    bool lastOn = false;                   // so the "Grid is OFF" notice repaints on toggle
    int paintLevel = 3;                    // what a drag paints

    static constexpr int labelW = 74;   // fits "Resonate" plus the 0% flag at 10pt

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridView)
};
