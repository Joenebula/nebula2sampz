#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Saturator.h"
#include "Colour.h"

namespace Nebula2
{
    // The prototype's "Colour" block, assembled in its original signal order:
    //   pre-gain -> drive shaper -> (crush, width) -> compressor -> tone filter -> post-gain
    // The pre/post gain staging is the prototype's: it drives INTO the shaper
    // (1 + d*5, or *2.5 for the wavefolder) and compensates after (*(1 - d*0.45)).
    // Without it the drive fizzes instead of hitting. Both gains are smoothed so
    // automating drive doesn't zipper.
    //
    // When `on` is false the whole block goes neutral (law: a control that reads off IS off).
    class ColourChain
    {
    public:
        struct Params
        {
            float drive = 0.0f;      // 0..100
            float crush = 0.0f;      // 0..100
            float squeeze = 0.0f;    // 0..100
            float tone = 100.0f;     // 0..100 (100 = fully open)
            float width = 100.0f;    // 0..200 (100 = unchanged)
            int driveChar = 0;       // DriveChar: tube/fuzz/fold
            bool on = true;
        };

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void process(juce::AudioBuffer<float>& buffer, const Params& p) noexcept;

    private:
        Saturator saturator;
        Compressor compressor;
        ToneFilter toneFilter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preGain, postGain;
    };
}
