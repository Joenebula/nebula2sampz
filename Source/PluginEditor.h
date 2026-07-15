#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Phase 0: blank editor, proves the window opens. Real UI arrives in Phase 5.
class Nebula2AudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit Nebula2AudioProcessorEditor(Nebula2AudioProcessor&);
    ~Nebula2AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    [[maybe_unused]] Nebula2AudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Nebula2AudioProcessorEditor)
};
