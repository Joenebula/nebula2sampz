#include "ColourChain.h"

namespace Nebula2
{
    void ColourChain::prepare(const juce::dsp::ProcessSpec& spec)
    {
        saturator.prepare(spec);
        compressor.prepare(spec);
        toneFilter.prepare(spec);

        preGain.reset(spec.sampleRate, 0.02);
        postGain.reset(spec.sampleRate, 0.02);
        preGain.setCurrentAndTargetValue(1.0f);
        postGain.setCurrentAndTargetValue(1.0f);
    }

    void ColourChain::reset()
    {
        saturator.reset();
        compressor.reset();
        toneFilter.reset();
    }

    void ColourChain::process(juce::AudioBuffer<float>& buffer, const Params& p) noexcept
    {
        // Off means off — every stage goes to its neutral value, not "mostly off".
        const float d  = p.on ? juce::jlimit(0.0f, 1.0f, p.drive   / 100.0f) : 0.0f;
        const float cr = p.on ? juce::jlimit(0.0f, 1.0f, p.crush   / 100.0f) : 0.0f;
        const float sq = p.on ? juce::jlimit(0.0f, 1.0f, p.squeeze / 100.0f) : 0.0f;
        const float tn = p.on ? juce::jlimit(0.0f, 1.0f, p.tone    / 100.0f) : 1.0f;
        const float w  = p.on ? juce::jlimit(0.0f, 2.0f, p.width   / 100.0f) : 1.0f;
        const auto ch  = (DriveChar) juce::jlimit(0, 2, p.driveChar);

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // 1. Drive INTO the shaper (the prototype's staging).
        preGain.setTargetValue(1.0f + d * (ch == DriveChar::Fold ? 2.5f : 5.0f));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = preGain.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * g);
        }

        // 2. Shaper (oversampled) + crush + width.
        saturator.process(buffer, d, ch, cr, w);

        // 3. Squeeze, then tone.
        compressor.process(buffer, sq);
        toneFilter.process(buffer, tn);

        // 4. Compensate for the drive boost (last in the chain, as in the prototype).
        postGain.setTargetValue(1.0f - d * 0.45f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = postGain.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * g);
        }
    }
}
