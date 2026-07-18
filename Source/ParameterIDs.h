#pragma once

// Canonical parameter IDs. One string constant per host-automatable parameter.
// Phase 1 seeds a representative spine (one of each parameter shape); the remaining
// ~90 from docs/parameter-inventory.md get added here as each phase's DSP lands.
namespace Nebula2::ParamID
{
    // --- Master / Transport ---
    inline constexpr auto master   = "master";    // float 0..1        (linear)
    inline constexpr auto limiter  = "limiterOn";  // bool

    // RETIRED: `bpm` ("Tempo"). It existed because the browser prototype had no host — the
    // same reason MIDI-learn and WAV export were retired. It was created and published to
    // the DAW, and read by NOTHING: every consumer reads transport.bpm from the host
    // playhead. A user could automate a Tempo lane 90->180 and the delay sync, shatter gate
    // and time-stretch would all ignore it. A dead control is worse than a missing one.
    // If a host ever supplies no tempo, the fix is a fallback in Transport.h, not a
    // parameter that pretends to be the clock.

    // --- Sample layer slicing (re-slices on the message thread when changed) ---
    inline constexpr auto sliceMode  = "sliceMode";   // choice: Grid / Transient
    inline constexpr auto sliceCount = "sliceCount";  // choice: 4/8/16/32/64
    inline constexpr auto sensitivity = "sens";       // float 0..1 (transient mode)

    // --- Morph pad ---
    // The pad POSITION is automatable (it's the performance gesture). The four scenes it
    // blends are state-chunk data, not params — see MorphPad.h for that decision.
    inline constexpr auto padOn = "padOn";    // bool
    inline constexpr auto padX  = "padX";     // float 0..1
    inline constexpr auto padY  = "padY";     // float 0..1
    // Auto-motion: the dot moves itself, tempo-locked, around the (padX,padY) centre.
    inline constexpr auto morphMotion = "morphMotion";  // choice: Off/Circle/Fig8/Square/Drift
    inline constexpr auto morphRate   = "morphRate";     // choice: 1/2/4/8 bar
    inline constexpr auto morphSize   = "morphSize";     // float 0..100 % (travel radius)
    inline constexpr auto morphGlide  = "morphGlide";    // float 0..100 % (smoothing)

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
    inline constexpr auto pump      = "pump";      // float 0..100  % (per-beat tempo-synced duck)
    inline constexpr auto gate      = "gate";      // float 0..100  % (grid-sequenced hard gate)
    inline constexpr auto reverse   = "reverse";   // float 0..100  % (play the step backwards)
    inline constexpr auto stutter   = "stutter";   // float 0..100  % (repeat a chunk of the step)
    inline constexpr auto shatter   = "shatter";   // float 0..100  % (tempo-locked 1/16 gate)
    inline constexpr auto pitchUp   = "pitchUp";   // float 0..100  % (transpose the chop up, to +12 st)
    inline constexpr auto pitchDown = "pitchDown"; // float 0..100  % (transpose the chop down, to -12 st)
    inline constexpr auto resonate  = "resonate";  // float 0..100  % (tuned bandpass bank, parallel)
    inline constexpr auto resoKey   = "resoKey";   // choice 0..11  root note, 0 = A
    inline constexpr auto resoScale = "resoScale"; // choice 0..3   minor/major/phrygian/fifths
    inline constexpr auto smpVol    = "smpVol";    // float 0..150 % sample layer level
    inline constexpr auto drmVol    = "drmVol";    // float 0..150 % drum layer level
    inline constexpr auto soloLayer = "solo";      // choice 0..2   none / sample / drums
    inline constexpr auto fxOn      = "fxOn";      // bool

    // --- Space (live: parallel reverb + tempo-synced delay send) ---
    inline constexpr auto revMix   = "revMix";     // float 0..100  %
    inline constexpr auto revSize  = "revSize";    // float 0..100  % (reverb tail length)
    inline constexpr auto dlyMix   = "dlyMix";     // float 0..100  %
    inline constexpr auto dlyFb    = "dlyFb";      // float 0..92   %
    inline constexpr auto dlySync  = "dlySync";    // choice: 1/16, 1/8T, 1/8, 1/8., 1/4, 1/4.
    inline constexpr auto dlyMode  = "dlyMode";    // choice: Ping-Pong / Dub / Warp
    inline constexpr auto haunt    = "haunt";      // float 0..100 % (drone from your slices)
    inline constexpr auto spaceOn  = "spaceOn";    // bool

    // --- Modular rack ---
    // Every DIAL is a parameter: a DAW user will want to automate the rack's filter, and a
    // dial you can't reach from the host is a dial that half-works. The PATCH (which cables
    // run where) is state-chunk data, not a parameter — same split as the Morph pad's
    // position-vs-scenes, and for the same reason: continuous controls automate, topology
    // doesn't. See RackGraph.h.
    inline constexpr auto rackOn   = "rackOn";     // bool — mutes the rack, dry beat survives

    inline constexpr auto fltCut   = "flt.cut";    // float 40..14000   (log skew, Hz)
    inline constexpr auto fltRes   = "flt.res";    // float 0.1..18     (Q)
    inline constexpr auto fltType  = "flt.type";   // choice: LP / BP / HP

    inline constexpr auto lfoRate  = "lfo.rate";   // float 0.05..20    (log skew, Hz)
    inline constexpr auto lfoDepth = "lfo.depth";  // float 0..100  %
    inline constexpr auto lfoShape = "lfo.shape";  // choice: Sine / Tri / Saw / Square

    inline constexpr auto phsRate  = "phs.rate";   // float 0.05..8     (Hz)
    inline constexpr auto phsDepth = "phs.depth";  // float 0..100  %
    inline constexpr auto phsFb    = "phs.fb";     // float 0..100  %
    inline constexpr auto phsMix   = "phs.mix";    // float 0..100  %

    inline constexpr auto choRate  = "cho.rate";   // float 0.05..8     (Hz)
    inline constexpr auto choDepth = "cho.depth";  // float 0..100  %
    inline constexpr auto choMix   = "cho.mix";    // float 0..100  %

    inline constexpr auto cmbTune  = "cmb.tune";   // float 20..2000    (log skew, Hz)
    inline constexpr auto cmbFb    = "cmb.fb";     // float 0..100  %
    inline constexpr auto cmbMix   = "cmb.mix";    // float 0..100  %

    inline constexpr auto fldDrive = "fld.drive";  // float 0..100  %
    inline constexpr auto fldSym   = "fld.sym";    // float -100..100 % (asymmetry)
    inline constexpr auto fldMix   = "fld.mix";    // float 0..100  %

    inline constexpr auto vowMorph = "vow.morph";  // float 0..4        (A E I O U, continuous)
    inline constexpr auto vowSharp = "vow.sharp";  // float 2..40       (Q)
    inline constexpr auto vowMix   = "vow.mix";    // float 0..100  %

    inline constexpr auto echTime  = "ech.time";   // float 20..2000    (ms)
    inline constexpr auto echFb    = "ech.fb";     // float 0..100  %
    inline constexpr auto echWow   = "ech.wow";    // float 0..100  %
    inline constexpr auto echMix   = "ech.mix";    // float 0..100  %

    inline constexpr auto outLvl   = "out.lvl";    // float 0..100  %

    // EQ band gains, dB. Bands are at fixed frequencies (35/110/420/1600/5200/9000).
    inline constexpr auto eqGain0  = "eq.0";
    inline constexpr auto eqGain1  = "eq.1";
    inline constexpr auto eqGain2  = "eq.2";
    inline constexpr auto eqGain3  = "eq.3";
    inline constexpr auto eqGain4  = "eq.4";
    inline constexpr auto eqGain5  = "eq.5";

    // --- One representative discrete/enum ---
    inline constexpr auto revChar  = "revChar";    // choice: room/hall/plate/cathedral/reverse
}
