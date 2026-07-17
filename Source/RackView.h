#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The modular rack's surface: modules with jacks, and cables you drag between them.
//
// Drag from a jack to another jack to patch. Drag a patched jack away to unplug. Click a
// module's name to bypass it. Every module wears its state (LIVE / NO PATH OUT / IDLE /
// OFF) as a word, not just a colour — the graph already knows why a module is silent, so
// there's no excuse for making you guess.
//
// A refused patch says WHY, in the strip at the bottom, rather than the cable simply
// failing to appear.
class RackView final : public juce::Component,
                       private juce::Timer
{
public:
    explicit RackView(Nebula2AudioProcessor& p);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    void clearPatch();

private:
    struct JackSpot
    {
        Nebula2::Port port;
        juce::Point<float> pos;      // centre, component space
    };

    void timerCallback() override;
    void rebuildLayout();
    juce::Rectangle<float> boundsFor(Nebula2::ModuleId m) const;
    const JackSpot* jackAt(juce::Point<float> p) const;
    juce::Point<float> posOf(const Nebula2::Port& port) const;
    Nebula2::ModuleId moduleAt(juce::Point<float> p) const;

    Nebula2AudioProcessor& processorRef;

    std::vector<JackSpot> jacks;
    std::array<juce::Rectangle<float>, Nebula2::numRackModules> modBounds;

    bool dragging = false;
    Nebula2::Port dragFrom;
    juce::Point<float> dragPos;

    juce::String message;          // why the last patch was refused (or what happened)
    int messageTicks = 0;

    float lfoDot = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackView)
};
