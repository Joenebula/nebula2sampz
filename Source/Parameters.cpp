#include "Parameters.h"
#include "ParameterIDs.h"

#include <cmath>

namespace Nebula2
{
    // A NormalisableRange with a logarithmic skew centred on the geometric mean —
    // the right feel for frequency/rate dials that span decades (rule: musical, not linear).
    static juce::NormalisableRange<float> logRange(float minV, float maxV, float step = 0.0f)
    {
        juce::NormalisableRange<float> r(minV, maxV, step);
        r.setSkewForCentre(std::sqrt(minV * maxV));
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using APF = juce::AudioParameterFloat;
        using APB = juce::AudioParameterBool;
        using APC = juce::AudioParameterChoice;
        using PID = juce::ParameterID;

        // Bump this when the parameter set changes in a way that affects host automation IDs.
        constexpr int version = 1;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // --- Master / Transport ---
        layout.add(std::make_unique<APF>(
            PID{ ParamID::master, version }, "Master",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.9f));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::bpm, version }, "Tempo",
            juce::NormalisableRange<float>(40.0f, 220.0f, 0.5f), 120.0f));

        layout.add(std::make_unique<APB>(
            PID{ ParamID::limiter, version }, "Limiter", true));

        // --- Colour block (live) ---
        const auto pct = [] { return juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f); };

        layout.add(std::make_unique<APF>(PID{ ParamID::drive,   version }, "Drive",   pct(), 0.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::crush,   version }, "Crush",   pct(), 0.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::squeeze, version }, "Squeeze", pct(), 0.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::tone,    version }, "Tone",    pct(), 100.0f));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::width, version }, "Width",
            juce::NormalisableRange<float>(0.0f, 200.0f, 1.0f), 100.0f));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::driveChar, version }, "Drive Character",
            juce::StringArray{ "Tube", "Fuzz", "Fold" }, 0));
        layout.add(std::make_unique<APB>(PID{ ParamID::fxOn, version }, "FX On", true));

        // --- Space (not yet wired) ---
        layout.add(std::make_unique<APF>(
            PID{ ParamID::revMix, version }, "Reverb Mix",
            juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));

        // --- Rack log dial (representative) ---
        layout.add(std::make_unique<APF>(
            PID{ ParamID::fltCut, version }, "Ladder Cutoff",
            logRange(40.0f, 14000.0f), 6000.0f));

        // --- Discrete/enum (representative) ---
        layout.add(std::make_unique<APC>(
            PID{ ParamID::revChar, version }, "Reverb Character",
            juce::StringArray{ "Room", "Hall", "Plate", "Cathedral", "Reverse" }, 1));

        return layout;
    }
}
