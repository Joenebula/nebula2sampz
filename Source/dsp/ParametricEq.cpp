#include "ParametricEq.h"

namespace Nebula2
{
    juce::dsp::IIR::Coefficients<float>::Ptr makeBandCoefficients(const EqBandSettings& b, double sampleRate)
    {
        using C = juce::dsp::IIR::Coefficients<float>;

        const auto sr   = sampleRate;
        const auto f    = (float) juce::jlimit(10.0, sr * 0.49, (double) b.freq);
        const auto q    = juce::jmax(0.05f, b.q);
        const auto gain = juce::Decibels::decibelsToGain(b.gainDb);

        if (! b.on)
            return C::makeAllPass(sr, f, q);   // transparent (unity magnitude)

        switch (b.type)
        {
            case EqBandType::HighPass:  return C::makeHighPass (sr, f, q);
            case EqBandType::LowPass:   return C::makeLowPass  (sr, f, q);
            case EqBandType::LowShelf:  return C::makeLowShelf (sr, f, q, gain);
            case EqBandType::HighShelf: return C::makeHighShelf(sr, f, q, gain);
            case EqBandType::Peak:      return C::makePeakFilter(sr, f, q, gain);
            case EqBandType::Notch:     return C::makeNotch    (sr, f, q);
            case EqBandType::BandPass:  return C::makeBandPass (sr, f, q);
        }
        return C::makeAllPass(sr, f, q);
    }

    void ParametricEq::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (size_t i = 0; i < bands.size(); ++i)
        {
            bands[i].prepare(spec);
            *bands[i].state = *makeBandCoefficients(settings[i], sampleRate);
        }
    }

    void ParametricEq::reset()
    {
        for (auto& b : bands)
            b.reset();
    }

    void ParametricEq::setBand(int index, const EqBandSettings& s)
    {
        jassert(index >= 0 && index < numBands);
        settings[(size_t) index] = s;
        *bands[(size_t) index].state = *makeBandCoefficients(s, sampleRate);
    }

    void ParametricEq::process(juce::AudioBuffer<float>& buffer) noexcept
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        for (auto& b : bands)
            b.process(ctx);
    }
}
