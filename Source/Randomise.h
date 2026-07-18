#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace Nebula2
{
    // One rolled parameter value.
    struct ParamRoll { const char* id; float value; };

    // Roll the Colour block, and the Space block.
    //
    // Pure functions returning values rather than writing the APVTS directly, so the ranges
    // can be tested without a processor — a randomiser that can produce an unusable state
    // is worse than none, and the only way to know is to roll it a few thousand times and
    // look.
    //
    // They roll INTO usable ranges rather than across the full parameter range: a uniform
    // roll of everything gives mud almost every time, which is why the prototype's dice
    // pick bands per control and leave some controls at zero on purpose.
    //
    // RNG by reference so a seed reproduces a roll.
    std::vector<ParamRoll> randomColourValues(juce::Random& rng);
    std::vector<ParamRoll> randomSpaceValues(juce::Random& rng);

    // Applies rolls to the APVTS with proper begin/end gestures, so the host records them
    // as edits and undo works. Skips any id the tree doesn't know rather than asserting.
    void applyRolls(juce::AudioProcessorValueTreeState& apvts,
                    const std::vector<ParamRoll>& rolls);
}
