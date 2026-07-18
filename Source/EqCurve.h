#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The parametric EQ's response curve, with a draggable node per band.
//
// This IS the EQ's control surface: twenty parameters (five bands of frequency, gain, Q and
// on/off) and not one dial among them. Twenty knobs would be unreadable, and worse, they
// would not answer the question you actually have - where is this band, and what is it
// doing to the sound. A curve answers both at a glance.
//
//   drag a node          move its frequency and gain together
//   mouse wheel          Q, on whichever node is under the pointer
//   click a node's dot   toggle that band on or off
//
// While a node moves, its frequency is printed - a parametric band you cannot read the
// frequency of is barely parametric.
class EqCurve final : public juce::Component,
                      private juce::Timer
{
public:
    explicit EqCurve (Nebula2AudioProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    juce::RangedAudioParameter* param (const char* id) const;
    float valueOf (const char* id, float fallback) const;
    bool  bandOn (int band) const;

    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float> p) const;      // -1 if none
    float curveDbAt (float hz) const;             // the sum of every live band

    Nebula2AudioProcessor& processorRef;

    int dragBand  = -1;    // which node the mouse is moving
    int hoverBand = -1;
    bool didDrag  = false; // so a click that never moved can toggle instead

    // Redrawn from the parameters each tick, so host automation moves the nodes too - the
    // curve must follow the parameters, never the other way round.
    std::array<float, 3> lastSeen { -1.0f, -1.0f, -1.0f };

    static constexpr float nodeRadius = 5.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurve)
};
