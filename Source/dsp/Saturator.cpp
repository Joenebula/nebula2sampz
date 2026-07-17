#include "Saturator.h"

#include <cmath>

namespace Nebula2
{
    float driveCurveSample(float x, float amt, DriveChar character) noexcept
    {
        if (amt < 0.01f)
            return x;

        switch (character)
        {
            case DriveChar::Fuzz:
            {
                const float g = 1.0f + amt * 24.0f;
                return juce::jlimit(-1.0f, 1.0f, std::tanh(x * g));
            }
            case DriveChar::Fold:
            {
                const float g = 1.0f + amt * 6.0f;
                float v = x * g;
                for (int f = 0; f < 4; ++f)
                    v = std::abs(v) > 1.0f ? (v < 0.0f ? -1.0f : 1.0f) * (2.0f - std::abs(v)) : v;
                return juce::jlimit(-1.0f, 1.0f, v);
            }
            case DriveChar::Tube:
            default:
            {
                const float k = amt * amt * 90.0f;
                return ((1.0f + k) * x) / (1.0f + k * std::abs(x));
            }
        }
    }

    void Saturator::prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        oversampling = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);   // 2x
        oversampling->initProcessing((size_t) spec.maximumBlockSize);

        reset();
    }

    void Saturator::reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();

        holdL = holdR = lpL = lpR = 0.0f;
        sampleCounter = 0;
    }

    int Saturator::getLatencySamples() const noexcept
    {
        return oversampling != nullptr ? (int) oversampling->getLatencyInSamples() : 0;
    }

    void Saturator::processDrive(juce::AudioBuffer<float>& buffer,
                                 float driveAmt, DriveChar character) noexcept
    {
        // Oversampled drive (skipped entirely when there's no drive).
        if (driveAmt >= 0.01f && oversampling != nullptr)
        {
            juce::dsp::AudioBlock<float> block(buffer);
            auto os = oversampling->processSamplesUp(block);

            const size_t nCh = os.getNumChannels();
            const size_t nOS = os.getNumSamples();
            for (size_t ch = 0; ch < nCh; ++ch)
            {
                auto* d = os.getChannelPointer(ch);
                for (size_t i = 0; i < nOS; ++i)
                    d[i] = driveCurveSample(d[i], driveAmt, character);
            }

            oversampling->processSamplesDown(block);
        }
    }

    void Saturator::process(juce::AudioBuffer<float>& buffer,
                            float driveAmt, DriveChar character,
                            float crushAmt, float width) noexcept
    {
        processDrive(buffer, driveAmt, character);
        processCrushWidth(buffer, crushAmt, width);
    }

    void Saturator::processCrushWidth(juce::AudioBuffer<float>& buffer,
                                      float crushAmt, float width) noexcept
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Bit-crush (ZOH + reconstruction LP) and mid/side width, at base rate.
        const bool doCrush = crushAmt > 0.0f;
        const bool doWidth = std::abs(width - 1.0f) > 1.0e-6f;
        if (! doCrush && ! doWidth)
            return;

        int   red = 1;
        float q   = 1.0f;
        float aLP = 1.0f;
        if (doCrush)
        {
            const int bits = juce::jmax(2, (int) std::lround(16.0f - crushAmt * 13.0f));
            q   = std::pow(2.0f, (float) (bits - 1));
            red = juce::jmax(1, (int) std::lround(1.0f + crushAmt * 12.0f));
            aLP = 1.0f - std::exp(-2.827f / (float) red);
        }

        float* L = buffer.getWritePointer(0);
        float* R = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = L[i];
            float r = R != nullptr ? R[i] : l;

            if (doCrush)
            {
                if (sampleCounter % red == 0)
                {
                    holdL = std::round(l * q) / q;
                    holdR = std::round(r * q) / q;
                }
                lpL += aLP * (holdL - lpL);
                lpR += aLP * (holdR - lpR);
                l = lpL;
                r = lpR;
            }
            ++sampleCounter;

            if (doWidth)
            {
                const float m = (l + r) * 0.5f;
                const float s = (l - r) * 0.5f * width;
                l = juce::jlimit(-1.0f, 1.0f, m + s);
                r = juce::jlimit(-1.0f, 1.0f, m - s);
            }

            L[i] = l;
            if (R != nullptr) R[i] = r;
        }
    }
}
