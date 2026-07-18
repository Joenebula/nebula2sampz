#include "Resonator.h"

#include <cmath>

namespace Nebula2
{
    const char* resoScaleName(ResoScale s) noexcept
    {
        switch (s)
        {
            case ResoScale::Minor:    return "Minor";
            case ResoScale::Major:    return "Major";
            case ResoScale::Phrygian: return "Phrygian";
            case ResoScale::Fifths:   return "Fifths";
            default:                  return "?";
        }
    }

    const std::array<int, 8>& resoScaleDegrees(ResoScale s) noexcept
    {
        // The prototype's tables, unchanged.
        static const std::array<int, 8> minor  { 0, 3, 7, 10, 12, 15, 19, 22 };
        static const std::array<int, 8> major  { 0, 4, 7, 11, 12, 16, 19, 23 };
        static const std::array<int, 8> phryg  { 0, 1, 5,  7, 12, 13, 17, 19 };   // dark, very techno
        static const std::array<int, 8> fifths { 0, 7, 12, 19, 24, 31, 36, 43 };  // hollow and huge

        switch (s)
        {
            case ResoScale::Major:    return major;
            case ResoScale::Phrygian: return phryg;
            case ResoScale::Fifths:   return fifths;
            case ResoScale::Minor:
            default:                  return minor;
        }
    }

    void Resonator::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        juce::dsp::ProcessSpec mono { spec.sampleRate, spec.maximumBlockSize, 1 };
        for (auto& voice : filters)
            for (auto& f : voice)
                f.prepare(mono);

        tuningDirty = true;
        retune();
        reset();
    }

    void Resonator::reset() noexcept
    {
        for (auto& voice : filters)
            for (auto& f : voice)
                f.reset();
        outGain = 0.0f;
        wasSilent = true;
    }

    void Resonator::setTuning(int keySemis, ResoScale s) noexcept
    {
        const int k = juce::jlimit(0, 11, keySemis);
        if (k == key && s == scale) return;         // nothing changed — don't rebuild
        key = k;
        scale = s;
        tuningDirty = true;
    }

    void Resonator::retune() noexcept
    {
        if (! tuningDirty) return;

        const auto& degrees = resoScaleDegrees(scale);
        for (int v = 0; v < numVoices; ++v)
        {
            // Nyquist guard: the Fifths scale reaches +43 semitones, which above the root
            // of a high key is ~2.2 kHz — fine, but clamp anyway so an odd sample rate
            // can't produce coefficients for a frequency above Nyquist.
            const double hz = juce::jlimit(20.0, sampleRate * 0.45,
                                           resoVoiceHz(key, degrees[(size_t) v]));

            // ArrayCoefficients, NOT makeBandPass(): the factory returns *new Coefficients
            // and would allocate on the audio thread every time the key changed.
            const auto c = juce::dsp::IIR::ArrayCoefficients<float>::makeBandPass(
                               sampleRate, hz, voiceQ);
            for (auto& f : filters[(size_t) v])
                *f.coefficients = c;
        }
        tuningDirty = false;
    }

    void Resonator::processAdd(juce::AudioBuffer<float>& buffer, float amount) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = juce::jmin(2, buffer.getNumChannels());
        if (numSamples <= 0 || numChannels <= 0) return;

        retune();

        const float amt = juce::jlimit(0.0f, 1.0f, amount);
        // The prototype's out.gain = amt * 1.6.
        const float target = amt * 1.6f;

        // Fully off AND already faded: do nothing, and clear the filter state so switching
        // it back on starts silent instead of ringing out whatever it last heard.
        if (target <= 0.0f && outGain < 1.0e-5f)
        {
            if (! wasSilent)
            {
                for (auto& voice : filters)
                    for (auto& f : voice)
                        f.reset();
                wasSilent = true;
            }
            outGain = 0.0f;
            return;
        }
        wasSilent = false;

        // ~20 ms, matching the prototype's setTargetAtTime smoothing on the bank output.
        const float smooth = 1.0f - std::exp(-1.0f / (float) (0.02 * sampleRate));
        const float perVoice = 0.9f / (float) numVoices;

        for (int i = 0; i < numSamples; ++i)
        {
            outGain += (target - outGain) * smooth;

            for (int c = 0; c < numChannels; ++c)
            {
                const float dry = buffer.getSample(c, i);
                float rung = 0.0f;
                for (int v = 0; v < numVoices; ++v)
                    rung += filters[(size_t) v][(size_t) c].processSample(dry);

                buffer.setSample(c, i, dry + rung * perVoice * outGain);
            }
        }
    }
}
