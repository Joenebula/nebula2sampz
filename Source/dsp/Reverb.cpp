#include "Reverb.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    juce::AudioBuffer<float> makeImpulseResponse(double sampleRate, double seconds,
                                                 ReverbChar character, int seed)
    {
        const int len = juce::jmax(64, (int) (sampleRate * seconds));
        juce::AudioBuffer<float> ir(2, len);

        const int preDelay = character == ReverbChar::Cathedral ? (int) (sampleRate * 0.045)
                           : character == ReverbChar::Hall      ? (int) (sampleRate * 0.020)
                                                                : 0;

        juce::Random rng((juce::int64) seed);   // seeded -> reproducible

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = ir.getWritePointer(ch);
            float lp = 0.0f;

            for (int i = 0; i < len; ++i)
            {
                const float t = (float) i / (float) len;
                if (i < preDelay) { d[i] = 0.0f; continue; }

                float env;
                switch (character)
                {
                    case ReverbChar::Plate:     env = std::pow(1.0f - t, 1.9f) * (1.0f + 0.35f * std::sin(t * 180.0f + (float) ch)); break;
                    case ReverbChar::Room:      env = std::pow(1.0f - t, 5.5f);  break;
                    case ReverbChar::Cathedral: env = std::pow(1.0f - t, 1.15f); break;
                    // Reverse = a decaying (hall-like) envelope, time-reversed below, so it
                    // genuinely SWELLS into the hit. The prototype computed a *growing*
                    // envelope then reversed it, which cancels to a plain decay (the
                    // "reverse" never actually reversed) — corrected here to match intent.
                    case ReverbChar::Reverse:   env = std::pow(1.0f - t, 2.6f);  break;
                    case ReverbChar::Hall:
                    default:                    env = std::pow(1.0f - t, 2.6f);  break;
                }

                const float v = (rng.nextFloat() * 2.0f - 1.0f) * env;
                lp += (v - lp) * (character == ReverbChar::Cathedral ? 0.22f : 0.55f);
                d[i] = character == ReverbChar::Plate ? v : lp * 1.6f;
            }

            if (character == ReverbChar::Reverse)
                std::reverse(d, d + len);
        }

        return ir;
    }

    void Reverb::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        conv.prepare(spec);
        dryScratch.setSize((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        setCharacter(currentChar);
    }

    void Reverb::reset()
    {
        conv.reset();
    }

    void Reverb::setCharacter(ReverbChar character)
    {
        currentChar = character;
        conv.loadImpulseResponse(makeImpulseResponse(sampleRate, 2.2, character),
                                 sampleRate,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::yes);
    }

    void Reverb::process(juce::AudioBuffer<float>& buffer, float wetMix) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const float mix = juce::jlimit(0.0f, 1.0f, wetMix);

        // Keep the dry signal (preallocated scratch — no allocation in the callback).
        for (int c = 0; c < numChannels && c < dryScratch.getNumChannels(); ++c)
            dryScratch.copyFrom(c, 0, buffer, c, 0, numSamples);

        // Wet = convolution.
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        // Blend out = dry*(1-mix) + wet*mix.
        for (int c = 0; c < numChannels && c < dryScratch.getNumChannels(); ++c)
        {
            auto* out = buffer.getWritePointer(c);
            const auto* dry = dryScratch.getReadPointer(c);
            for (int i = 0; i < numSamples; ++i)
                out[i] = dry[i] * (1.0f - mix) + out[i] * mix;
        }
    }
}
