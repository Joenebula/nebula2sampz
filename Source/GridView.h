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

private:
    void timerCallback() override;
    int rowAt(juce::Point<int> pos) const;
    int stepAt(juce::Point<int> pos) const;
    float panelAmountFor(int row) const;   // the knob behind this row
    bool rowIsStarved(int row) const;      // knob at 0 -> the row cannot sound

    Nebula2AudioProcessor& processorRef;
    int lastStep = -2;
    int paintLevel = 3;                    // what a drag paints

    static constexpr int labelW = 62;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridView)
};
