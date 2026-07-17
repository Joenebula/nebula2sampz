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

            // --- Rack presets ---
            // These exist because a rack you have to wire from scratch teaches you nothing
            // about what it can do. Each one is a legible patch you can pull apart.

            { "Rack: It Talks", {
                { rackOn, 1.0f }, { vowMix, 100.0f }, { vowSharp, 12.0f }, { vowMorph, 0.0f },
                { lfoRate, 0.35f }, { lfoDepth, 60.0f }, { lfoShape, 0.0f } },
              // Beat -> Vowel -> Out, with the LFO sweeping the formants. The formant
              // sweep is what makes a breakbeat pronounce vowels rather than just honk.
              "src:out>vow:in;vow:out>out:in;lfo:out>vow:cv|" },

            { "Rack: Comb Metal", {
                { rackOn, 1.0f }, { cmbTune, 220.0f }, { cmbFb, 88.0f }, { cmbMix, 70.0f },
                { outLvl, 80.0f } },
              // Tuned comb: the break gains a pitch. Feedback is high, so the brick wall
              // on the rack out is doing real work here.
              "src:out>cmb:in;cmb:out>out:in|" },

            { "Rack: Dub Chamber", {
                { rackOn, 1.0f }, { echTime, 375.0f }, { echFb, 72.0f }, { echWow, 55.0f },
                { echMix, 60.0f }, { fltCut, 2200.0f }, { fltRes, 1.5f }, { fltType, 0.0f } },
              // Echo into a filter, so each repeat is darker than the last.
              "src:out>ech:in;ech:out>flt:in;flt:out>out:in|" },

            { "Rack: Fold & Sweep", {
                { rackOn, 1.0f }, { fldDrive, 65.0f }, { fldSym, 20.0f }, { fldMix, 80.0f },
                { fltCut, 900.0f }, { fltRes, 4.0f }, { fltType, 0.0f },
                { lfoRate, 0.8f }, { lfoDepth, 75.0f }, { lfoShape, 1.0f } },
              // Wavefolder into a resonant filter the LFO sweeps — the classic rack move.
              "src:out>fld:in;fld:out>flt:in;flt:out>out:in;lfo:out>flt:cv|" },

            { "Rack: Parallel Wreck", {
                { rackOn, 1.0f }, { fldDrive, 80.0f }, { fldMix, 100.0f },
                { vowMix, 100.0f }, { vowMorph, 2.0f }, { outLvl, 70.0f } },
              // TWO branches summed at the out: folded on one side, vowel on the other.
              // This is the topology the prototype's original rack drew as "dead" — it's
              // very much alive, and it's why the graph does two reachability sweeps.
              "src:out>fld:in;fld:out>out:in;src:out>vow:in;vow:out>out:in|" },

            { "Rack: Phase Drift", {
                { rackOn, 1.0f }, { phsRate, 0.18f }, { phsDepth, 90.0f }, { phsFb, 65.0f },
                { phsMix, 70.0f }, { choRate, 0.4f }, { choDepth, 60.0f }, { choMix, 45.0f } },
              "src:out>phs:in;phs:out>cho:in;cho:out>out:in|" },

            // --- Morph pad presets ---
            // The pad is only as good as the four corners you give it. These ship corners
            // worth travelling between, rather than the seed set.

            { "Morph: Open to Broken", {
                { padOn, 1.0f }, { padX, 0.0f }, { padY, 0.0f } },
              "",
              // A(open)          B(dirty)          C(dark)           D(broken)
              // cut res drv flg phs sht wid spc
              "18000 0.7 0 0 0 0 100 0,"
              "3500 2.5 75 35 0 0 120 10,"
              "420 3.5 20 0 25 0 80 35,"
              "900 6 85 70 80 75 140 55" },

            { "Morph: Underwater", {
                { padOn, 1.0f }, { padX, 0.3f }, { padY, 0.6f } },
              "",
              "6000 1 10 20 10 0 110 20,"
              "1800 4 30 60 40 0 130 40,"
              "300 5 15 40 20 0 70 60,"
              "600 8 45 90 70 30 150 80" },
        };
        return presets;
    }

    void applyPreset(juce::AudioProcessorValueTreeState& apvts, int index,
                     RackGraph& rack, juce::SpinLock& rackLock,
                     std::array<MorphScene, 4>& scenes)
    {
        const auto& presets = getFactoryPresets();
        if (index < 0 || index >= (int) presets.size()) return;
        const auto& preset = presets[(size_t) index];

        // 1. Everything back to default, so nothing leaks in from the previous preset.
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                rp->setValueNotifyingHost(rp->getDefaultValue());

        // 2. Apply this preset's overrides.
        for (const auto& v : preset.values)
            if (auto* rp = apvts.getParameter(v.id))
                rp->setValueNotifyingHost(rp->convertTo0to1(v.value));

        // 3. The structural state. This is step 3 because it used to be MISSING: a preset
        //    reset every dial and left the previous patch wired, so you'd recall to a
        //    combination nobody designed. Note "" means DEFAULT, not "skip" — an empty
        //    patch string genuinely clears the rack.
        {
            auto restored = RackGraph::fromString(preset.rackPatch);
            const juce::SpinLock::ScopedLockType sl(rackLock);
            rack = restored;
        }
        scenes = juce::String(preset.morphScenes).isNotEmpty()
                     ? morphScenesFromString(preset.morphScenes)
                     : defaultMorphScenes();
    }
}
