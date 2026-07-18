#include "Randomise.h"
#include "ParameterIDs.h"

namespace Nebula2
{
    namespace
    {
        float rr(juce::Random& rng, float lo, float hi)
        {
            return lo + rng.nextFloat() * (hi - lo);
        }

        // "Sometimes off": several controls read better at zero than at a small value, and a
        // roll that always dials in a bit of everything is how you get mud. The prototype
        // does this deliberately — crush is off 60% of the time, and so on.
        float maybe(juce::Random& rng, float chance, float lo, float hi)
        {
            return rng.nextFloat() < chance ? rr(rng, lo, hi) : 0.0f;
        }
    }

    std::vector<ParamRoll> randomColourValues(juce::Random& rng)
    {
        // Tube twice: it's the character that flatters most material, so weighting it means
        // the dice lands somewhere usable more often than a flat 1-in-3 would.
        const int chars[] = { 0, 0, 1, 2 };            // tube, tube, fuzz, fold
        const int keys[]  = { 0, 2, 3, 5, 7, 8, 10 };  // scale degrees of A minor

        return {
            { ParamID::drive,     rr(rng, 20.0f, 75.0f) },
            { ParamID::crush,     maybe(rng, 0.40f, 10.0f, 55.0f) },
            { ParamID::squeeze,   rr(rng, 25.0f, 70.0f) },
            { ParamID::tone,      rr(rng, 50.0f, 100.0f) },
            { ParamID::pump,      maybe(rng, 0.60f, 35.0f, 88.0f) },
            { ParamID::width,     rr(rng, 90.0f, 160.0f) },
            { ParamID::driveChar, (float) chars[rng.nextInt(4)] },
            { ParamID::resonate,  maybe(rng, 0.50f, 25.0f, 70.0f) },
            { ParamID::shatter,   maybe(rng, 0.35f, 15.0f, 60.0f) },
            { ParamID::stutter,   maybe(rng, 0.50f, 20.0f, 65.0f) },
            { ParamID::resoKey,   (float) keys[rng.nextInt(7)] },
            { ParamID::resoScale, (float) rng.nextInt(4) },

            // The whole block has to be ON, or the dice rolls a set of values that make no
            // sound at all and reads as broken.
            { ParamID::fxOn, 1.0f },
        };
    }

    std::vector<ParamRoll> randomSpaceValues(juce::Random& rng)
    {
        return {
            { ParamID::revMix,  rr(rng, 20.0f, 70.0f) },
            { ParamID::revSize, rr(rng, 30.0f, 95.0f) },
            { ParamID::revChar, (float) rng.nextInt(5) },
            { ParamID::dlyMix,  maybe(rng, 0.75f, 18.0f, 60.0f) },
            { ParamID::dlyFb,   rr(rng, 25.0f, 80.0f) },
            { ParamID::dlySync, (float) rng.nextInt(6) },
            { ParamID::dlyMode, (float) rng.nextInt(3) },
            { ParamID::haunt,   maybe(rng, 0.45f, 20.0f, 65.0f) },
            { ParamID::spaceOn, 1.0f },
        };
    }

    void applyRolls(juce::AudioProcessorValueTreeState& apvts,
                    const std::vector<ParamRoll>& rolls)
    {
        for (const auto& r : rolls)
        {
            auto* p = apvts.getParameter(r.id);
            if (p == nullptr) continue;

            // Gestures, so the host records this as a real edit and undo has something to
            // undo. setValueNotifyingHost takes a NORMALISED value, which is the mistake
            // waiting to happen here — a percentage passed raw would clamp to 1.0 and turn
            // every rolled control up to maximum.
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(r.value));
            p->endChangeGesture();
        }
    }
}
