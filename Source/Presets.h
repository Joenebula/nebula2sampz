#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/RackGraph.h"
#include "dsp/MorphPad.h"
#include "dsp/FxGrid.h"
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

        // A preset is not just parameters. The rack PATCH and the Morph SCENES are
        // state-chunk data, and a preset that restored every dial but left your previous
        // patch wired would recall to something nobody designed.
        //
        // "" means the DEFAULT, not "leave it alone": an empty patch is an unpatched rack
        // (dry beat straight through) and empty scenes are the seed four. Recall is total
        // or it isn't recall.
        const char* rackPatch = "";
        const char* morphScenes = "";

        // The FX-grid pattern, in FxGrid's own "steps:row,row,..." format (cells 0..3,
        // rows Drive/Crush/Squeeze/Tone/Width/Reverb/Delay). "" clears the grid — same rule
        // as the others: recall is total. Set gridOn in `values` to actually run it.
        const char* gridPattern = "";
    };

    const std::vector<Preset>& getFactoryPresets();

    // Applies a preset by index. Resets EVERY parameter to its default first, then applies
    // the preset's overrides — so recall is total and deterministic. Without the reset,
    // loading preset B after A would silently inherit A's stray values, which is the
    // "preset load applies fully" gate failing in the most confusing way possible.
    //
    // The rack and scenes are passed in because they live outside the APVTS. They used to
    // be absent from this function entirely, which meant preset recall quietly stopped
    // being total the day the rack landed — the doc comment above stayed true-looking and
    // became a lie.
    //
    // The SAMPLE LAYER is deliberately absent, and that IS a decision rather than the same
    // oversight repeated — worth stating, because the paragraph above argues the opposite
    // way for the rack and someone will reasonably ask why that reasoning doesn't apply.
    //
    // A preset is a SOUND DESIGN: colour, space, a rack patch, a grid pattern. Your break
    // and how you have arranged it — the slice order, and each chop's level, pan, pitch and
    // reverse — are your MATERIAL. Trying "Ghost Choir" must not throw away a beat you
    // spent ten minutes rearranging, and a preset author has no idea how many slices your
    // sample even has.
    //
    // So "recall is total" is scoped to the design, not to the material. If that ever
    // changes it needs the treatment the rack got — passed in explicitly, reset first, and
    // gated — rather than being decided by whether a value happens to sit in the APVTS.
    void applyPreset(juce::AudioProcessorValueTreeState& apvts, int index,
                     RackGraph& rack, juce::SpinLock& rackLock,
                     std::array<MorphScene, 4>& scenes, FxGrid& grid);
}
