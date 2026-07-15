#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace Nebula2
{
    // The prototype's parametric EQ is a bank of RBJ biquads (verified in the web build via
    // coefficient + magnitude eval). JUCE's IIR coefficient makers are the same RBJ cookbook,
    // so we reuse those rather than re-porting the math (law: never write the same helper
    // twice). Correctness is asserted the same way the prototype proved it: magnitude response.

    enum class EqBandType
    {
        HighPass = 0, LowShelf, Peak, HighShelf, LowPass, Notch, BandPass
    };

    struct EqBandSettings
    {
        bool on = true;
        EqBandType type = EqBandType::Peak;
        float freq = 1000.0f;   // Hz
        float gainDb = 0.0f;    // dB (shelves/peak only)
        float q = 0.707f;
    };

    // RBJ coefficients for one band at a sample rate. An *off* band returns an all-pass:
    // unity magnitude everywhere, so a disabled band is audibly transparent.
    juce::dsp::IIR::Coefficients<float>::Ptr makeBandCoefficients(const EqBandSettings& b, double sampleRate);

    // Six-band stereo parametric EQ. Bands run in series.
    class ParametricEq
    {
    public:
        static constexpr int numBands = 6;

        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();
        void setBand(int index, const EqBandSettings& s);
        void process(juce::AudioBuffer<float>& buffer) noexcept;

    private:
        using Filter = juce::dsp::IIR::Filter<float>;
        using Coefficients = juce::dsp::IIR::Coefficients<float>;
        using Band = juce::dsp::ProcessorDuplicator<Filter, Coefficients>;

        std::array<Band, (size_t) numBands> bands;
        std::array<EqBandSettings, (size_t) numBands> settings;
        double sampleRate = 44100.0;
    };
}
