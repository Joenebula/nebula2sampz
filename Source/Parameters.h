#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace Nebula2
{
    // Builds the full parameter layout for the plugin's AudioProcessorValueTreeState.
    // Kept in its own translation unit so it can be unit-tested without instantiating
    // the whole processor (see Tests/).
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
