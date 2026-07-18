#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace Nebula2
{
    // The Morph pad: four "scenes" at the corners of an X/Y pad, bilinearly blended by the
    // dot's position. Move the dot, hear the sound morph between four characters.
    //
    // PARAMETER-MODEL DECISION (joe: this was the architectural call I flagged — overrule
    // it freely, it's cheap to change):
    //
    //   * padX / padY ARE host-automatable parameters. The pad position IS the performance
    //     gesture — it's the thing you'd draw automation for.
    //   * The four SCENES are state-chunk data, NOT parameters. They define the sound (they
    //     are preset material), they're edited via the UI, and exposing 4 x 8 = 32 scene
    //     values as automatable params would bury every host's automation list under
    //     things nobody automates.
    //
    // The alternative (32 scene params) buys the ability to automate a corner's filter
    // cutoff independently — which is not how the instrument is played. If you ever want
    // it, the scenes are already a plain struct; promoting them is mechanical.
    struct MorphScene
    {
        float cut = 16000.0f;   // filter cutoff, Hz  — blended in LOG space (see below)
        float res = 0.7f;       // filter resonance
        float drv = 0.0f;       // drive %
        float flg = 0.0f;       // flanger %
        float phs = 0.0f;       // phaser %
        float sht = 0.0f;       // shatter % (tempo-locked gate)
        float wid = 100.0f;     // width %
        float spc = 0.0f;       // the pad's own space send %
    };

    // Corner order matches the prototype: A(top-left) B(top-right) C(bottom-left) D(bottom-right)
    enum class MorphCorner { A = 0, B, C, D, Count };

    // Auto-motion: the pad dot moves itself in a shape, tempo-locked. The prototype's
    // Off/Circle/Fig-8/Square/Drift. When on, the effective pad position is the base
    // (padX,padY) plus this offset — so the sliders still set the CENTRE the motion orbits.
    enum class MorphMotion { Off = 0, Circle, Fig8, Square, Drift };

    // The (dx,dy) offset for a motion mode at a cycle phase in [0,1), scaled by `size`
    // (0..1, the travel radius). Pure and testable. dx/dy are each in roughly [-size, size].
    void morphMotionOffset(MorphMotion mode, double phase, float size, float& dx, float& dy) noexcept;

    // The four scenes the pad ships with: open / dirty / dark-resonant / wide-wet-broken.
    std::array<MorphScene, 4> defaultMorphScenes();

    // Bilinear weights for the dot at (x, y), both 0..1, y=0 at the top.
    std::array<float, 4> morphWeights(float x, float y) noexcept;

    // Blend the four corners at (x, y).
    //
    // `cut` is interpolated in LOG space (as the prototype does). That matters musically:
    // blending 16 kHz and 700 Hz linearly lands at 8.3 kHz — barely moved from "open" to
    // the ear. Geometrically it lands at ~3.3 kHz, which is what "halfway between bright
    // and dark" actually sounds like. Frequency is perceived logarithmically; linear
    // interpolation of a cutoff makes the bottom half of the pad's travel do nothing.
    MorphScene blendMorph(const std::array<MorphScene, 4>& corners, float x, float y) noexcept;

    // State-chunk serialisation (the scenes are structured data, not parameters).
    juce::String morphScenesToString(const std::array<MorphScene, 4>& corners);
    std::array<MorphScene, 4> morphScenesFromString(const juce::String& s);
}
