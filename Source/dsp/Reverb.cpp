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
                    case ReverbChar::Reverse:   env = std::pow(t, 2.2f);         break;
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
}
