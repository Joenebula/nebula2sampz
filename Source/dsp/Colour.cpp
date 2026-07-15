#include "Colour.h"

#include <cmath>

namespace Nebula2
{
    // ---- Compressor (Squeeze) --------------------------------------------------------
    void Compressor::prepare(const juce::dsp::ProcessSpec& spec)
    {
        comp.prepare(spec);
        comp.setThreshold(-6.0f);
        comp.setRatio(1.0f);
        comp.setAttack(3.0f);
        comp.setRelease(80.0f);
    }

    void Compressor::reset()
    {
        comp.reset();
    }

    void Compressor::process(juce::AudioBuffer<float>& buffer, float squeeze) noexcept
    {
        const float sq = juce::jlimit(0.0f, 1.0f, squeeze);
        comp.setThreshold(-6.0f - sq * 36.0f);
        comp.setRatio(1.0f + sq * 17.0f);
        comp.setAttack((0.003f + (1.0f - sq) * 0.02f) * 1000.0f);   // ms
        comp.setRelease((0.08f + sq * 0.30f) * 1000.0f);            // ms

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        comp.process(ctx);
    }

    // ---- Tone (lowpass tilt) ---------------------------------------------------------
    juce::dsp::IIR::Coefficients<float>::Ptr ToneFilter::coefficientsFor(float tone, double sampleRate)
    {
        const float tn   = juce::jlimit(0.0f, 1.0f, tone);
        const float freq = juce::jlimit(20.0f, (float) (sampleRate * 0.49),
                                        200.0f * std::pow(100.0f, tn));
        const float q    = 0.9f + (1.0f - tn) * 6.0f;
        return juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, freq, q);
    }

    void ToneFilter::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        filter.prepare(spec);
        *filter.state = *coefficientsFor(1.0f, sampleRate);
    }

    void ToneFilter::reset()
    {
        filter.reset();
    }

    void ToneFilter::process(juce::AudioBuffer<float>& buffer, float tone) noexcept
    {
        *filter.state = *coefficientsFor(tone, sampleRate);

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        filter.process(ctx);
    }
}
