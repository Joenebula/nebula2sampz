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

    // Readouts belong on the PARAMETER, not on a slider: the host's own automation lane
    // and generic editor read this too. Put it on the slider and a DAW user still sees
    // "5999.99951..." — these are log-skewed ranges, so trailing float garbage is the norm.
    static juce::AudioParameterFloatAttributes hzText()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int)
            {
                return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz"
                                    : juce::String(juce::roundToInt(v)) + " Hz";
            })
            .withValueFromStringFunction([](const juce::String& t)
            {
                const float v = t.getFloatValue();
                return t.containsIgnoreCase("k") ? v * 1000.0f : v;
            });
    }

    static juce::AudioParameterFloatAttributes suffixText(const juce::String& suffix, int dp)
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([suffix, dp](float v, int)
            {
                return (dp <= 0 ? juce::String(juce::roundToInt(v)) : juce::String(v, dp)) + suffix;
            })
            .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue(); });
    }

    // The Vowel dial reads 0..4. "2.31" tells you nothing; "I>O 31%" tells you what you're
    // about to hear, which is the entire point of the control.
    static juce::AudioParameterFloatAttributes vowelText()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int)
            {
                static const char* names[] = { "A", "E", "I", "O", "U" };
                const float p = juce::jlimit(0.0f, 4.0f, v);
                const int i = juce::jlimit(0, 4, (int) p);
                const float fr = p - (float) i;
                if (fr < 0.02f || i >= 4) return juce::String(names[i]);
                return juce::String(names[i]) + ">" + juce::String(names[i + 1])
                     + " " + juce::String(juce::roundToInt(fr * 100.0f)) + "%";
            })
            .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue(); });
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using APF = juce::AudioParameterFloat;
        using APB = SnappedBool;   // see Parameters.h: a bool whose value is actually a bool
        using APC = juce::AudioParameterChoice;
        using PID = juce::ParameterID;

        // Bump this when the parameter set changes in a way that affects host automation IDs.
        constexpr int version = 1;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // --- Master / Transport ---
        layout.add(std::make_unique<APF>(
            PID{ ParamID::master, version }, "Master",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.9f));

        // No "Tempo" parameter — see the retirement note in ParameterIDs.h. The host is the
        // clock; a parameter claiming to be one and being ignored is a dead control.

        layout.add(std::make_unique<APB>(
            PID{ ParamID::limiter, version }, "Limiter", true));

        const auto pct = [] { return juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f); };

        // --- Morph pad (position only; scenes live in the state chunk) ---
        layout.add(std::make_unique<APB>(PID{ ParamID::padOn, version }, "Morph On", false));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::padX, version }, "Morph X",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::padY, version }, "Morph Y",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

        // --- FX grid ---
        layout.add(std::make_unique<APB>(PID{ ParamID::gridOn, version }, "Grid On", false));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::gridSteps, version }, "Grid Steps",
            juce::StringArray{ "8", "16", "32" }, 1));   // default 16 = one bar of 1/16s

        // --- Sample slicing ---
        layout.add(std::make_unique<APC>(
            PID{ ParamID::sliceMode, version }, "Slice Mode",
            juce::StringArray{ "Grid", "Transient" }, 0));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::sliceCount, version }, "Slices",
            juce::StringArray{ "4", "8", "16", "32", "64" }, 2));      // default 16
        layout.add(std::make_unique<APF>(
            PID{ ParamID::sensitivity, version }, "Sensitivity",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

        // --- Colour block (live) ---

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

        // --- Space (live) ---
        layout.add(std::make_unique<APF>(PID{ ParamID::revMix, version }, "Reverb Mix", pct(), 0.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::revSize, version }, "Reverb Size", pct(), 50.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::dlyMix, version }, "Delay Mix",  pct(), 0.0f));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::dlyFb, version }, "Delay Feedback",
            juce::NormalisableRange<float>(0.0f, 92.0f, 1.0f), 40.0f));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::dlySync, version }, "Delay Sync",
            juce::StringArray{ "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4." }, 2));   // default 1/8
        layout.add(std::make_unique<APB>(PID{ ParamID::spaceOn, version }, "Space On", true));

        // --- Modular rack ---
        // Defaults are the prototype's own, so a fresh rack sounds like a fresh rack did.
        layout.add(std::make_unique<APB>(PID{ ParamID::rackOn, version }, "Rack On", true));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::fltCut, version }, "Ladder Cutoff",
            logRange(40.0f, 14000.0f), 6000.0f, hzText()));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::fltRes, version }, "Ladder Res",
            juce::NormalisableRange<float>(0.1f, 18.0f, 0.1f), 4.0f, suffixText("", 1)));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::fltType, version }, "Ladder Type",
            juce::StringArray{ "Low Pass", "Band Pass", "High Pass" }, 0));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::lfoRate, version }, "LFO Rate", logRange(0.05f, 20.0f), 1.5f, suffixText(" Hz", 2)));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::lfoDepth, version }, "LFO Depth", pct(), 50.0f));
        layout.add(std::make_unique<APC>(
            PID{ ParamID::lfoShape, version }, "LFO Shape",
            juce::StringArray{ "Sine", "Triangle", "Saw", "Square" }, 0));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::phsRate, version }, "Phaser Rate", logRange(0.05f, 8.0f), 0.5f, suffixText(" Hz", 2)));
        layout.add(std::make_unique<APF>(PID{ ParamID::phsDepth, version }, "Phaser Depth", pct(), 70.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::phsFb,    version }, "Phaser Feedback", pct(), 45.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::phsMix,   version }, "Phaser Mix", pct(), 60.0f));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::choRate, version }, "Chorus Rate", logRange(0.05f, 8.0f), 0.8f, suffixText(" Hz", 2)));
        layout.add(std::make_unique<APF>(PID{ ParamID::choDepth, version }, "Chorus Depth", pct(), 45.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::choMix,   version }, "Chorus Mix", pct(), 50.0f));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::cmbTune, version }, "Comb Tune", logRange(20.0f, 2000.0f), 180.0f, hzText()));
        layout.add(std::make_unique<APF>(PID{ ParamID::cmbFb,  version }, "Comb Feedback", pct(), 80.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::cmbMix, version }, "Comb Mix", pct(), 55.0f));

        layout.add(std::make_unique<APF>(PID{ ParamID::fldDrive, version }, "Folder Drive", pct(), 35.0f));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::fldSym, version }, "Folder Symmetry",
            juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::fldMix, version }, "Folder Mix", pct(), 70.0f));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::vowMorph, version }, "Vowel",
            juce::NormalisableRange<float>(0.0f, 4.0f, 0.01f), 0.0f, vowelText()));
        layout.add(std::make_unique<APF>(
            PID{ ParamID::vowSharp, version }, "Vowel Sharpness",
            juce::NormalisableRange<float>(2.0f, 40.0f, 0.1f), 9.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::vowMix, version }, "Vowel Mix", pct(), 70.0f));

        layout.add(std::make_unique<APF>(
            PID{ ParamID::echTime, version }, "Echo Time",
            logRange(20.0f, 2000.0f), 320.0f, suffixText(" ms", 0)));
        layout.add(std::make_unique<APF>(PID{ ParamID::echFb,  version }, "Echo Feedback", pct(), 55.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::echWow, version }, "Echo Wow", pct(), 25.0f));
        layout.add(std::make_unique<APF>(PID{ ParamID::echMix, version }, "Echo Mix", pct(), 45.0f));

        layout.add(std::make_unique<APF>(PID{ ParamID::outLvl, version }, "Rack Out", pct(), 100.0f));

        const auto dB = [] { return juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f); };
        const char* eqIDs[6]   = { ParamID::eqGain0, ParamID::eqGain1, ParamID::eqGain2,
                                   ParamID::eqGain3, ParamID::eqGain4, ParamID::eqGain5 };
        const char* eqNames[6] = { "EQ 35Hz", "EQ 110Hz", "EQ 420Hz", "EQ 1.6k", "EQ 5.2k", "EQ 9k" };
        for (int i = 0; i < 6; ++i)
            layout.add(std::make_unique<APF>(PID{ eqIDs[i], version }, eqNames[i], dB(), 0.0f));

        // --- Discrete/enum (representative) ---
        layout.add(std::make_unique<APC>(
            PID{ ParamID::revChar, version }, "Reverb Character",
            juce::StringArray{ "Room", "Hall", "Plate", "Cathedral", "Reverse" }, 1));

        return layout;
    }
}
