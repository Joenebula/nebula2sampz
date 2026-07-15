#pragma once

// Canonical parameter IDs. One string constant per host-automatable parameter.
// Phase 1 seeds a representative spine (one of each parameter shape); the remaining
// ~90 from docs/parameter-inventory.md get added here as each phase's DSP lands.
namespace Nebula2::ParamID
{
    // --- Master / Transport ---
    inline constexpr auto master   = "master";    // float 0..1        (linear)
    inline constexpr auto bpm      = "bpm";        // float 40..220     (linear, 0.5 step)
    inline constexpr auto limiter  = "limiterOn";  // bool

    // --- FX grid (Colour) — one representative amount ---
    inline constexpr auto revMix   = "revMix";     // float 0..100      (linear, %)

    // --- Rack — one representative logarithmic dial ---
    inline constexpr auto fltCut   = "flt.cut";    // float 40..14000   (log skew, Hz)

    // --- One representative discrete/enum ---
    inline constexpr auto revChar  = "revChar";    // choice: room/hall/plate/cathedral/reverse
}
