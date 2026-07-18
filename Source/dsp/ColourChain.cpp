#include "ColourChain.h"

#include <cmath>

namespace Nebula2
{
    float ColourChain::pumpGain(double beatPhase, float depth) noexcept
    {
        // Prototype: setValueAtTime(depth) on the beat, exponentialRampToValueAtTime(1) by
        // phase 0.85. An exponential ramp from depth to 1 is depth^(1 - phase/0.85).
        if (beatPhase >= 0.85) return 1.0f;
        return std::pow(depth, 1.0f - (float) (beatPhase / 0.85));
    }

    void ColourChain::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        saturator.prepare(spec);
        compressor.prepare(spec);
        toneFilter.prepare(spec);
        resonator.prepare(spec);

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
        resonator.reset();
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

        // The prototype's Colour signal order is:
        //   pre-gain -> drive(shaper) -> squeeze(comp) -> tone(filter) -> crush -> width -> post
        // An earlier port ran crush+width right after drive (because the Saturator bundled
        // all three), which put Tone AFTER crush — so Tone filtered the crush grit the
        // prototype leaves raw, and the compressor reacted to the crushed signal instead of
        // the clean one. Matching the order restores both behaviours.

        // 1. Drive INTO the shaper (the prototype's staging).
        preGain.setTargetValue(1.0f + d * (ch == DriveChar::Fold ? 2.5f : 5.0f));
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = preGain.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * g);
        }

        // 2. Drive shaper only (oversampled).
        saturator.processDrive(buffer, d, ch);

        // 3. Squeeze, then tone — BEFORE crush/width, as the prototype wires it.
        compressor.process(buffer, sq);
        toneFilter.process(buffer, tn);

        // 3b. Pump: the per-beat duck, between tone and crush (prototype: filt -> duck ->
        // crush). Off at pump=0, so it's transparent unless dialled up. Sample-accurate so
        // the duck lands on the beat rather than snapping to the block grid.
        const float pumpAmt = p.on ? juce::jlimit(0.0f, 1.0f, p.pump / 100.0f) : 0.0f;
        if (pumpAmt > 0.001f)
        {
            const float depth = 1.0f - pumpAmt * 0.85f;
            const double beatsPerSample = (p.bpm / 60.0) / sampleRate;
            for (int i = 0; i < numSamples; ++i)
            {
                double phase = std::fmod(p.ppq + (double) i * beatsPerSample, 1.0);
                if (phase < 0.0) phase += 1.0;
                const float g = pumpGain(phase, depth);
                for (int c = 0; c < numChannels; ++c)
                    buffer.setSample(c, i, buffer.getSample(c, i) * g);
            }
        }

        // 3c. RESONATE: the tuned bandpass bank, excited POST-FILTER and summed in
        // parallel, exactly where the prototype taps it ("gr.filt.connect(resoBank.input)",
        // bank -> postGain). Adds to the signal rather than replacing it, so at 0 it is a
        // no-op.
        resonator.setTuning(p.resoKey, p.resoScale);
        resonator.processAdd(buffer, p.on ? juce::jlimit(0.0f, 1.0f, p.resonate / 100.0f) : 0.0f);

        // 4. Crush + width, after tone (and the duck).
        saturator.processCrushWidth(buffer, cr, w);

        // 5. Compensate for the drive boost (last in the chain, as in the prototype).
        postGain.setTargetValue(1.0f - d * 0.45f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = postGain.getNextValue();
            for (int c = 0; c < numChannels; ++c)
                buffer.setSample(c, i, buffer.getSample(c, i) * g);
        }
    }
}
