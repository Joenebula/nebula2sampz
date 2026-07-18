#pragma once

#include <juce_dsp/juce_dsp.h>
#include "MorphPad.h"
#include "StepFx.h"      // shatterGainAt — the gate shape, defined once

namespace Nebula2
{
    // The multi-effect the Morph pad drives: filter -> drive -> flanger -> phaser ->
    // shatter -> width. The pad blends four scenes and hands the result here every block.
    //
    // Built on JUCE's LadderFilter/Phaser/Chorus rather than hand-rolled equivalents —
    // the prototype's own law 8: never write the same helper twice. Its versions are
    // tested and I'd only be reintroducing bugs theirs already fixed.
    class MorphEngine
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // `bpm` clocks the shatter gate. `on` false = fully bypassed (off means off).
        void process(juce::AudioBuffer<float>& buffer, const MorphScene& scene,
                     double bpm, bool on) noexcept;

    private:
        juce::dsp::LadderFilter<float> filter;
        juce::dsp::Phaser<float> phaser;
        juce::dsp::Chorus<float> flanger;

        // Shatter: a tempo-locked gate. Phase is kept in NOTE fractions, not seconds, so
        // it follows the host tempo instead of drifting off the grid.
        double shatterPhase = 0.0;
        double sampleRate = 44100.0;

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> shatterGain;
    };
}
