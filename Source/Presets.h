#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace Nebula2
{
    // Factory presets, exposed through the VST3 program interface so they appear in the
    // host's own preset menu (Cubase's dropdown) rather than in a browser we'd have to
    // build and maintain ourselves.
    struct PresetValue
    {
        const char* id;
        float value;      // REAL-WORLD value (e.g. drive 60 = 60%), not normalised.
                          // For choice params this is the option index.
    };

    struct Preset
    {
        const char* name;
        std::vector<PresetValue> values;
    };

    const std::vector<Preset>& getFactoryPresets();

    // Applies a preset by index. Resets EVERY parameter to its default first, then applies
    // the preset's overrides — so recall is total and deterministic. Without the reset,
    // loading preset B after A would silently inherit A's stray values, which is the
    // "preset load applies fully" gate failing in the most confusing way possible.
    void applyPreset(juce::AudioProcessorValueTreeState& apvts, int index);
}
