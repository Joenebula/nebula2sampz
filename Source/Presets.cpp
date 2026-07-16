#include "Presets.h"
#include "ParameterIDs.h"

namespace Nebula2
{
    // Choice indices, for readability below.
    namespace Ch
    {
        // driveChar
        constexpr float tube = 0, fuzz = 1, fold = 2;
        // revChar
        constexpr float room = 0, hall = 1, plate = 2, cathedral = 3, reverse = 4;
        // dlySync
        constexpr float s16 = 0, s8t = 1, s8 = 2, s8d = 3, s4 = 4, s4d = 5;
        // sliceMode
        constexpr float grid = 0, transient = 1;
        // sliceCount: 4/8/16/32/64
        constexpr float c8 = 1, c16 = 2, c32 = 3;
    }

    const std::vector<Preset>& getFactoryPresets()
    {
        using namespace ParamID;
        static const std::vector<Preset> presets = {
            { "Init", {} },   // everything at its default

            { "Tube Drive", {
                { drive, 60.0f }, { driveChar, Ch::tube }, { tone, 85.0f } } },

            { "Crushed", {
                { crush, 70.0f }, { drive, 30.0f }, { driveChar, Ch::fuzz },
                { tone, 70.0f }, { squeeze, 40.0f } } },

            { "Wavefolder", {
                { drive, 80.0f }, { driveChar, Ch::fold }, { tone, 60.0f }, { width, 130.0f } } },

            { "Squashed", {
                { squeeze, 80.0f }, { drive, 25.0f }, { tone, 95.0f }, { master, 0.8f } } },

            { "Mono Punch", {
                { width, 0.0f }, { drive, 40.0f }, { squeeze, 55.0f } } },

            { "Hall Space", {
                { revMix, 45.0f }, { revChar, Ch::hall }, { dlyMix, 15.0f }, { dlySync, Ch::s8 } } },

            { "Reverse Swell", {
                { revMix, 70.0f }, { revChar, Ch::reverse }, { tone, 90.0f } } },

            { "Wide Plate", {
                { width, 160.0f }, { revMix, 40.0f }, { revChar, Ch::plate } } },

            { "Dub Echo", {
                { dlyMix, 55.0f }, { dlyFb, 75.0f }, { dlySync, Ch::s4d },
                { revMix, 20.0f }, { revChar, Ch::plate } } },

            { "Cathedral Drift", {
                { revMix, 65.0f }, { revChar, Ch::cathedral }, { dlyMix, 25.0f },
                { dlyFb, 60.0f }, { dlySync, Ch::s8d }, { tone, 80.0f } } },

            { "Transient Chops", {
                { sliceMode, Ch::transient }, { sensitivity, 0.6f } } },

            { "Tight Grid 32", {
                { sliceMode, Ch::grid }, { sliceCount, Ch::c32 } } },

            { "Lo-Fi Break", {
                { sliceMode, Ch::grid }, { sliceCount, Ch::c16 },
                { crush, 55.0f }, { tone, 55.0f }, { drive, 35.0f }, { driveChar, Ch::tube },
                { width, 80.0f }, { revMix, 12.0f }, { revChar, Ch::room } } },

            { "Jungle Slam", {
                { sliceMode, Ch::transient }, { sensitivity, 0.65f },
                { drive, 55.0f }, { driveChar, Ch::tube }, { squeeze, 60.0f },
                { tone, 90.0f }, { width, 115.0f }, { dlyMix, 10.0f }, { dlySync, Ch::s16 } } },
        };
        return presets;
    }

    void applyPreset(juce::AudioProcessorValueTreeState& apvts, int index)
    {
        const auto& presets = getFactoryPresets();
        if (index < 0 || index >= (int) presets.size()) return;

        // 1. Everything back to default, so nothing leaks in from the previous preset.
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                rp->setValueNotifyingHost(rp->getDefaultValue());

        // 2. Apply this preset's overrides.
        for (const auto& v : presets[(size_t) index].values)
            if (auto* rp = apvts.getParameter(v.id))
                rp->setValueNotifyingHost(rp->convertTo0to1(v.value));
    }
}
