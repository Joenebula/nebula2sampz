#include "MasterProcessor.h"

namespace Nebula2
{
    void MasterProcessor::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        limiter.prepare(spec);
        limiter.setThreshold(-0.5f);   // dB
        limiter.setRelease(100.0f);    // ms

        gain.reset(spec.sampleRate, 0.02);      // 20 ms ramp
        gain.setCurrentAndTargetValue(0.9f);
    }

    void MasterProcessor::reset()
    {
        limiter.reset();
    }

    void MasterProcessor::process(juce::AudioBuffer<float>& buffer, float gainLinear, bool limiterEnabled) noexcept
    {
        const int numSamples   = buffer.getNumSamples();
        const int numChannels  = buffer.getNumChannels();

        // Smoothed output gain (one coefficient per sample frame, shared across channels).
        gain.setTargetValue(gainLinear);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * g);
        }

        if (limiterEnabled)
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            limiter.process(ctx);
        }

        // Brickwall safety clamp — the master must never emit above 0 dBFS, whatever a
        // limiter transient does. Cheap insurance; the correctness test depends on it.
        for (int c = 0; c < numChannels; ++c)
        {
            auto* d = buffer.getWritePointer(c);
            for (int i = 0; i < numSamples; ++i)
                d[i] = juce::jlimit(-1.0f, 1.0f, d[i]);
        }
    }
}
