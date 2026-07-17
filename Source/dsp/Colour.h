#pragma once

#include <juce_dsp/juce_dsp.h>

namespace Nebula2
{
    // The prototype's "Squeeze" — a DynamicsCompressor with these mappings (squeeze 0..1):
    //   threshold = -6 - sq*36 dB, ratio = 1 + sq*17, knee = 30 - sq*28 dB,
    //   attack = 3..23 ms, release = 80..380 ms.
    // JUCE's Compressor is hard-knee, so the knee softening is dropped (a small deviation
    // from the browser build; can add a soft-knee later if the ear wants it).
    class Compressor
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void process(juce::AudioBuffer<float>& buffer, float squeeze) noexcept;

    private:
        juce::dsp::Compressor<float> comp;
    };

    // The prototype's "Tone" — a lowpass tilt (tone 0..1):
    //   freq = 200 * 100^tone  (200 Hz .. 20 kHz), Q = 0.9 + (1-tone)*6.
    class ToneFilter
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void process(juce::AudioBuffer<float>& buffer, float tone) noexcept;

        // The audio-thread version: returns a std::array BY VALUE, no heap.
        //
        // This matters more than it looks. juce::dsp::IIR::Coefficients' factories are
        // `return *new Coefficients(...)` — so the old code allocated on EVERY BLOCK, for
        // every user, at ~86 allocations/second, whether or not the FX were even on
        // (ColourChain calls this unconditionally and just passes tone=1 when off). No
        // knob had to move. It was the single most-executed allocation in the plugin.
        static std::array<float, 6> arrayCoefficientsFor(float tone, double sampleRate) noexcept;

        // Exposed for testing the response. ALLOCATES — tests and prepare() only, never
        // the audio thread.
        static juce::dsp::IIR::Coefficients<float>::Ptr coefficientsFor(float tone, double sampleRate);

    private:
        using Filter = juce::dsp::IIR::Filter<float>;
        using Coefficients = juce::dsp::IIR::Coefficients<float>;
        juce::dsp::ProcessorDuplicator<Filter, Coefficients> filter;
        double sampleRate = 44100.0;
    };
}
