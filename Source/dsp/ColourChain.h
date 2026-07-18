#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Saturator.h"
#include "Colour.h"
#include "Resonator.h"

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

            // Pump: a per-beat sidechain-style duck, tempo-synced (the prototype's groove
            // move). 0 = off, so it changes nothing unless you turn it up. Needs the host
            // position to land the duck on the beat.
            float pump = 0.0f;       // 0..100 %
            double ppq = 0.0;        // host beat position (quarter notes)
            double bpm = 120.0;

            // Resonate: a tuned bandpass bank in parallel, excited post-filter. 0 = off.
            float resonate = 0.0f;   // 0..100 %
            int resoKey = 0;         // semitones above A (0 = A, 3 = C, ...)
            ResoScale resoScale = ResoScale::Minor;
        };

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void process(juce::AudioBuffer<float>& buffer, const Params& p) noexcept;

        // The per-beat pump gain at a beat phase in [0,1): slams to `depth` on the beat,
        // then exponentially breathes back to 1 by phase 0.85 (the prototype's shape).
        // Exposed and pure so it can be tested without a whole processor.
        static float pumpGain(double beatPhase, float depth) noexcept;

    private:
        Saturator saturator;
        Compressor compressor;
        ToneFilter toneFilter;
        Resonator resonator;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preGain, postGain;
        double sampleRate = 44100.0;
    };
}
