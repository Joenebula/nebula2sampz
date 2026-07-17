#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// The prototype's controls, rebuilt as native JUCE drawing.
//
// The knob is a direct port of its `nbKnobSVG`: a tick ring, a recessed track, a glowing
// value arc with a brighter leading edge, a machined cap with a specular highlight, and a
// pointer. Same angles (-140..+140), same proportions.
//
// One rule carried over from the prototype and worth stating: at zero, the arc and the
// pointer go DARK. Zero means zero — a knob at the bottom of its range must not glow as
// though it's doing something.
class Nebula2LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    Nebula2LookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawLabel(juce::Graphics&, juce::Label&) override;

    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;

    // Shared panel chrome, so every panel is drawn by one function rather than by each
    // view's own guess at the radius.
    static void drawCard(juce::Graphics&, juce::Rectangle<float>, const juce::String& title);
    static void drawWell(juce::Graphics&, juce::Rectangle<float>);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2LookAndFeel)
};
