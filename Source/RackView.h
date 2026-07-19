#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "EqCurve.h"

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

    // Each module carries its OWN dials, rather than a shared row at the bottom of the
    // panel. A row of ten knobs labelled "Cut / Res / Tune / Comb FB..." makes you map
    // each one back to a box by memory; on the box, the dial IS the module.
    void buildModuleDials(juce::AudioProcessorValueTreeState&);

private:
    struct Dial
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String label;
        Nebula2::ModuleId owner = Nebula2::ModuleId::count;

        // Where the caption and value are drawn, under the knob. The kit pairs every
        // rotary with both; this build had neither - the label existed only as a tooltip,
        // so a rack knob showed nothing at all until you hovered it.
        juce::Rectangle<int> textArea;
    };
    std::vector<Dial> dials;

    // The EQ's control surface. Not a dial: five bands x four parameters is twenty knobs,
    // and none of them would answer "where is this band and what is it doing".
    EqCurve eqCurve;

    void addDial(juce::AudioProcessorValueTreeState&, Nebula2::ModuleId,
                 const juce::String& paramID, const juce::String& label);

    struct JackSpot
    {
        Nebula2::Port port;
        juce::Point<float> pos;      // centre, component space
    };

    void timerCallback() override;

    // Per-module screens, as the prototype has them: Beat Out shows the beat going in,
    // Main Out its own level, the LFO its shape and where in it we are, the Wavefolder its
    // transfer curve, the Vowel its formant peaks. Without these a module is a box of
    // unlabelled dials and the only way to learn what one does is to turn it and listen.
    // (The EQ's screen is the EqCurve component, which is also its control surface.)
    void drawModuleScreens (juce::Graphics&) const;
    juce::Rectangle<float> screenAreaFor (Nebula2::ModuleId) const;

    // Which modules draw a screen at all. ONE answer, asked by both the painter and the
    // layout - they disagreed, so screens drew over the knobs they were meant to sit above.
    static bool hasScreen (Nebula2::ModuleId) noexcept;
    static constexpr float screenH = 30.0f;

public:
    // The height this view NEEDS, derived from what its modules actually contain - a name,
    // maybe a screen, maybe a row of knobs with captions. Same approach as
    // GridView::preferredHeight().
    //
    // The rack was given a fixed 454px and divided it into equal rows, so every module got
    // the same slice whether it held four knobs or nothing. That is why the captions
    // collided with the knobs: rows were shorter than their contents and everything
    // overlapped. Rows are now sized to their tallest module and the total is derived.
    static int preferredHeight();

    // What one module needs vertically: name + subtitle, its screen if it has one, and a
    // knob row with captions if it has dials.
    static float moduleHeightFor (Nebula2::ModuleId) noexcept;

private:
    static constexpr float headH   = 30.0f;   // name + subtitle
    static constexpr float capH    = 26.0f;   // caption + value under a knob
    static constexpr float eqCurveH = 92.0f;  // the EQ's response curve
    static constexpr float modPadB  = 8.0f;

    // Every effect module gets a visible On/Off button, as the prototype has. Bypass has
    // worked all along - it was reachable only by clicking the module's NAME, which is a
    // gesture with nothing on screen to suggest it exists. A feature nobody can find is not
    // meaningfully different from one that was never built.
    //
    // The RULE is Nebula2::moduleHasPower, in RackGraph, so it can be tested against the
    // bypass the audio thread honours. This is only the alias the drawing code reads.
    static bool hasPower (Nebula2::ModuleId m) noexcept { return Nebula2::moduleHasPower (m); }
    juce::Rectangle<float> powerRectFor (Nebula2::ModuleId) const;

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
