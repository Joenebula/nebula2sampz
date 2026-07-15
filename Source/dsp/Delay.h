#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // The prototype's tempo-synced delay: two cross-fed delay lines (ping-pong), a lowpass
    // in the feedback path (damping, ~4.2 kHz), dry + wet mix. Delay time is set in seconds
    // by the caller (derived from host BPM * note division -> musical time, never ms).
    class PingPongDelay
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // timeSeconds: delay time. feedback 0..~0.92. wetMix 0..1. pingPong = cross-feed.
        void process(juce::AudioBuffer<float>& buffer,
                     float timeSeconds, float feedback, float wetMix, bool pingPong) noexcept;

    private:
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 1 << 18 };
        juce::dsp::IIR::Filter<float> dampL, dampR;
        double sampleRate = 44100.0;
    };
}
