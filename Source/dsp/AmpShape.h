#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace Nebula2
{
    // ONE amplitude curve, applied to EVERY slice as it fires.
    //
    // This is what turns a flat chop into a hit with a shape: Punch makes anything sound
    // percussive, Gate cuts the tail dead, Swell reverses the attack. It is per-SLICE and
    // relative to the slice's own length, so it works the same whether the chop is a 1/16
    // or a whole bar — a fixed-time envelope would be a different effect at every tempo.
    //
    // 32 points, the prototype's resolution, read with linear interpolation.
    constexpr int ampShapePoints = 32;

    enum class AmpShapeId { Punch = 0, Gate, Swell, Pluck, Flat, Count };

    const char* ampShapeName(AmpShapeId s) noexcept;

    using AmpShape = std::array<float, (size_t) ampShapePoints>;

    // The prototype's five curves, unchanged.
    AmpShape makeAmpShape(AmpShapeId id);

    // A shape that keeps the character of `base` but varies it — the prototype's dice.
    // Deliberately NOT uniform noise: a random 32-point curve is a stutter, not an
    // envelope. RNG by reference so a seed reproduces a shape.
    AmpShape randomAmpShape(AmpShapeId base, juce::Random& rng);

    // Level at 0..1 through the slice, interpolated. Out-of-range positions clamp rather
    // than wrap: a voice running slightly past its own end must fade out, not jump back to
    // the attack.
    float ampShapeAt(const AmpShape& shape, double position01) noexcept;

    juce::String ampShapeToString(const AmpShape&);
    AmpShape ampShapeFromString(const juce::String&);
}
