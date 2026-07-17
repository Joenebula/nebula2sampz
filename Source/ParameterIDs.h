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

    // --- Sample layer slicing (re-slices on the message thread when changed) ---
    inline constexpr auto sliceMode  = "sliceMode";   // choice: Grid / Transient
    inline constexpr auto sliceCount = "sliceCount";  // choice: 4/8/16/32/64
    inline constexpr auto sensitivity = "sens";       // float 0..1 (transient mode)

    // --- FX grid sequencer ---
    inline constexpr auto gridOn    = "gridOn";     // bool
    inline constexpr auto gridSteps = "gridSteps";  // choice: 8/16/32 (1/16-note spacing)

    // --- Colour block (live: drives the FX chain on the drum bus) ---
    inline constexpr auto drive     = "drive";     // float 0..100  %
    inline constexpr auto driveChar = "char";      // choice: tube/fuzz/fold
    inline constexpr auto crush     = "crush";     // float 0..100  %
    inline constexpr auto squeeze   = "squeeze";   // float 0..100  %
    inline constexpr auto tone      = "tone";      // float 0..100  % (100 = open)
    inline constexpr auto width     = "width";     // float 0..200  % (100 = unchanged)
    inline constexpr auto fxOn      = "fxOn";      // bool

    // --- Space (live: parallel reverb + tempo-synced delay send) ---
    inline constexpr auto revMix   = "revMix";     // float 0..100  %
    inline constexpr auto dlyMix   = "dlyMix";     // float 0..100  %
    inline constexpr auto dlyFb    = "dlyFb";      // float 0..92   %
    inline constexpr auto dlySync  = "dlySync";    // choice: 1/16, 1/8T, 1/8, 1/8., 1/4, 1/4.
    inline constexpr auto spaceOn  = "spaceOn";    // bool

    // --- Rack — one representative logarithmic dial ---
    inline constexpr auto fltCut   = "flt.cut";    // float 40..14000   (log skew, Hz)

    // --- One representative discrete/enum ---
    inline constexpr auto revChar  = "revChar";    // choice: room/hall/plate/cathedral/reverse
}
