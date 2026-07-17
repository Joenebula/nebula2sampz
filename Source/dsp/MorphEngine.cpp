#include "MorphEngine.h"

#include <cmath>

namespace Nebula2
{
    void MorphEngine::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        filter.prepare(spec);
        filter.setMode(juce::dsp::LadderFilterMode::LPF24);
        filter.setCutoffFrequencyHz(16000.0f);
        filter.setResonance(0.1f);
        filter.setDrive(1.0f);

        phaser.prepare(spec);
        phaser.setRate(0.6f);
        phaser.setDepth(0.6f);
        phaser.setCentreFrequency(600.0f);
        phaser.setFeedback(0.5f);
        phaser.setMix(0.0f);

        // A flanger is a chorus with a very short delay and feedback.
        flanger.prepare(spec);
        flanger.setRate(0.35f);
        flanger.setDepth(0.5f);
        flanger.setCentreDelay(3.0f);      // ms — short = flanger, long = chorus
        flanger.setFeedback(0.45f);
        flanger.setMix(0.0f);

        shatterGain.reset(spec.sampleRate, 0.002);   // 2ms so the gate clicks aren't harsh
        shatterGain.setCurrentAndTargetValue(1.0f);

        reset();
    }

    void MorphEngine::reset()
    {
        filter.reset();
        phaser.reset();
        flanger.reset();
        shatterPhase = 0.0;
    }

    void MorphEngine::process(juce::AudioBuffer<float>& buffer, const MorphScene& scene,
                              double bpm, bool on) noexcept
    {
        if (! on) return;                       // off means off — untouched, not "mostly off"

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0) return;

        // --- filter ---
        filter.setCutoffFrequencyHz(juce::jlimit(20.0f, (float) (sampleRate * 0.45), scene.cut));
        // The scene's `res` is a biquad-style Q (0.5..18); LadderFilter wants 0..1, and
        // 1.0 self-oscillates. Map conservatively so a dark corner sings rather than screams.
        filter.setResonance(juce::jlimit(0.0f, 0.95f, (scene.res - 0.5f) / 18.0f));

        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            filter.process(ctx);
        }

        // --- drive: the prototype's morphCurve, tanh(x*k)/tanh(k) ---
        const float d = juce::jlimit(0.0f, 1.0f, scene.drv / 100.0f);
        if (d > 0.001f)
        {
            const float k = 1.0f + d * 12.0f;
            const float norm = std::tanh(k);
            for (int c = 0; c < numChannels; ++c)
            {
                auto* p = buffer.getWritePointer(c);
                for (int i = 0; i < numSamples; ++i)
                    p[i] = std::tanh(p[i] * k) / norm;
            }
        }

        // --- flanger / phaser ---
        flanger.setMix(juce::jlimit(0.0f, 1.0f, scene.flg / 100.0f));
        phaser.setMix(juce::jlimit(0.0f, 1.0f, scene.phs / 100.0f));
        {
            juce::dsp::AudioBlock<float> block(buffer);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            flanger.process(ctx);
            phaser.process(ctx);
        }

        // --- shatter: tempo-locked gate at 1/16 ---
        const float sht = juce::jlimit(0.0f, 1.0f, scene.sht / 100.0f);
        if (sht > 0.001f)
        {
            const double safeBpm = bpm > 0.0 ? bpm : 120.0;
            const double stepSec = (60.0 / safeBpm) * 0.25;                 // 1/16 note
            const double inc = 1.0 / juce::jmax(1.0, stepSec * sampleRate); // phase per sample

            for (int i = 0; i < numSamples; ++i)
            {
                // Open for the first half of each 1/16, ducked for the second.
                const float target = (shatterPhase < 0.5) ? 1.0f : (1.0f - sht);
                shatterGain.setTargetValue(target);
                const float gn = shatterGain.getNextValue();

                for (int c = 0; c < numChannels; ++c)
                    buffer.setSample(c, i, buffer.getSample(c, i) * gn);

                shatterPhase += inc;
                if (shatterPhase >= 1.0) shatterPhase -= 1.0;
            }
        }

        // --- width (mid/side) ---
        const float w = juce::jlimit(0.0f, 2.0f, scene.wid / 100.0f);
        if (numChannels > 1 && std::abs(w - 1.0f) > 1.0e-6f)
        {
            auto* L = buffer.getWritePointer(0);
            auto* R = buffer.getWritePointer(1);
            for (int i = 0; i < numSamples; ++i)
            {
                const float m = (L[i] + R[i]) * 0.5f;
                const float s = (L[i] - R[i]) * 0.5f * w;
                L[i] = juce::jlimit(-1.0f, 1.0f, m + s);
                R[i] = juce::jlimit(-1.0f, 1.0f, m - s);
            }
        }
    }
}
