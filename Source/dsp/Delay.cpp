#include "Delay.h"

namespace Nebula2
{
    void PingPongDelay::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec stereo = spec;
        stereo.numChannels = 2;
        delay.prepare(stereo);
        delay.setMaximumDelayInSamples(juce::jmax(64, (int) (sampleRate * 4.0)));

        // 5200 Hz to match the prototype's ping-pong feedback damping (it was 4200 here —
        // darker echoes than the reference). NOTE: the prototype filters only the L->R
        // feedback path and leaves R->L raw; this port damps both, which is the remaining
        // stereo-topology divergence (D7) left for the user's ear.
        const auto damp = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 5200.0f, 0.707f);
        dampL.prepare(spec);
        dampR.prepare(spec);
        dampL.coefficients = damp;
        dampR.coefficients = damp;

        reset();
    }

    void PingPongDelay::reset()
    {
        delay.reset();
        dampL.reset();
        dampR.reset();
    }

    void PingPongDelay::process(juce::AudioBuffer<float>& buffer,
                                float timeSeconds, float feedback, float wetMix, bool pingPong) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        const float delaySamples = juce::jlimit(1.0f, (float) (sampleRate * 4.0),
                                                timeSeconds * (float) sampleRate);
        delay.setDelay(delaySamples);

        const float fb  = juce::jlimit(0.0f, 0.95f, feedback);
        const float mix = juce::jlimit(0.0f, 1.0f, wetMix);

        float* L = buffer.getWritePointer(0);
        float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = L[i];
            const float inR = R != nullptr ? R[i] : inL;

            const float dL = delay.popSample(0);
            const float dR = delay.popSample(1);

            const float fL = dampL.processSample(dL);
            const float fR = dampR.processSample(dR);

            if (pingPong)
            {
                delay.pushSample(0, inL + fR * fb);   // cross-feed = ping-pong
                delay.pushSample(1, inR + fL * fb);
            }
            else
            {
                delay.pushSample(0, inL + fL * fb);
                delay.pushSample(1, inR + fR * fb);
            }

            L[i] = inL + dL * mix;
            if (R != nullptr) R[i] = inR + dR * mix;
        }
    }
}
