#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// A functional control surface: every live parameter, attached to the APVTS so the host
// and the UI can never disagree (law: one source of truth — the visual derives from state).
// This is deliberately plain — the designed UI is Phase 5. It exists so the FX can be
// judged BY EAR, which is the only test that matters for whether the port sounds right.
class Nebula2AudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit Nebula2AudioProcessorEditor(Nebula2AudioProcessor&);
    ~Nebula2AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    void addKnob(Knob& k, const juce::String& paramID, const juce::String& name);

    Nebula2AudioProcessor& processorRef;

    Knob drive, crush, squeeze, tone, width, master;

    juce::ComboBox charBox;
    juce::Label charLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> charAttachment;

    juce::ToggleButton fxOnButton { "FX On" };
    std::unique_ptr<APVTS::ButtonAttachment> fxOnAttachment;

    juce::ToggleButton limiterButton { "Limiter" };
    std::unique_ptr<APVTS::ButtonAttachment> limiterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessorEditor)
};
