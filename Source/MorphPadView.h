#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The Morph pad: drag the dot, hear four scenes blend.
//
// The dot drives the padX/padY PARAMETERS (not private state), so host automation and the
// mouse are the same thing — the visual always derives from the parameter, and the two can
// never drift apart. That's the prototype's law 2 (one source of truth) applied to a
// gesture control.
//
// Corner labels show which scene sits where. The blend is continuous, so the pad reads as
// a map of the sound rather than four presets with a crossfader.
class MorphPadView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit MorphPadView(Nebula2AudioProcessor& p);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setFromMouse(const juce::MouseEvent&);
    juce::Point<float> dotPos() const;   // in component space, from the params

    Nebula2AudioProcessor& processorRef;
    float lastX = -1.0f, lastY = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphPadView)
};
