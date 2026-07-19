#pragma once

#include <juce_graphics/juce_graphics.h>

// The AUDIO UI KIT's design tokens.
//
// These replace the original HTML prototype's tokens. The two disagree, and the kit wins:
// it is the later design and the one the user drew. The visible difference is the accent -
// the prototype was teal #3fe0d4, the kit is electric cyan #2ec5ff - plus real typefaces
// instead of whatever the OS happened to have.
//
// Values are taken from the kit by FREQUENCY, not by eye: #2ec5ff appears 78 times in it,
// #6b7a89 29 times, and so on. Where a colour here disagrees with the kit, the kit is right.
//
// One place, so a colour is never typed twice - the eleventh hard-coded 0xff2ec5ff is how
// a theme rots.
namespace Nebula2::Theme
{
    // --- surfaces ---
    // The kit's page is a radial gradient, not a flat fill: bg at the edges lifting to
    // bgLift near the top centre. Drawing it flat is the single easiest way to make a
    // correct palette still look wrong.
    inline const juce::Colour bg      { 0xff05080d };   // page base
    inline const juce::Colour bgLift  { 0xff101d2c };   // radial centre, ~-14% from top
    inline const juce::Colour card    { 0xff141b24 };   // panel top
    inline const juce::Colour card2   { 0xff10161d };   // panel bottom
    inline const juce::Colour card3   { 0xff1a222d };   // raised control
    inline const juce::Colour card4   { 0xff2a3646 };   // control top edge
    inline const juce::Colour well    { 0xff080c12 };   // recessed: grids, scopes, pads
    inline const juce::Colour well2   { 0xff0a0f16 };   // recessed, one step lighter
    inline const juce::Colour chassis { 0xff0b1017 };   // module bodies

    // --- lines ---
    inline const juce::Colour line   { 0x0dffffff };    // rgba(255,255,255,0.05)
    inline const juce::Colour hiline { 0x17ffffff };    // rgba(255,255,255,0.09)

    // --- text ---
    inline const juce::Colour ink   { 0xffeaf6ff };     // primary
    inline const juce::Colour ink2  { 0xffeafaff };     // brightest (LCD readouts)
    inline const juce::Colour sub   { 0xff9fb0bf };     // secondary
    inline const juce::Colour sub2  { 0xff8896a5 };
    inline const juce::Colour faint { 0xff6b7a89 };     // labels, mono captions
    inline const juce::Colour dim   { 0xff546272 };     // disabled / at-rest

    // --- accents ---
    inline const juce::Colour accent    { 0xff2ec5ff };  // electric cyan
    inline const juce::Colour accentLit { 0xff8fe6ff };  // hover / highlight
    inline const juce::Colour accentPale{ 0xffbdefff };  // brightest glow
    inline const juce::Colour onAccent  { 0xff04141d };  // text ON accent

    // The kit's primary button is a vertical gradient, not a flat accent fill.
    inline const juce::Colour btnTop { 0xff6bb9ec };
    inline const juce::Colour btnBot { 0xff1f6fae };

    inline const juce::Colour good  { 0xff5ce0a0 };     // live / connected
    inline const juce::Colour warn  { 0xffffcf4d };     // caution, CV/LFO yellow
    inline const juce::Colour danger{ 0xffff5468 };     // clipping, refused, errors

    // --- scrims, shadows and glass ---
    //
    // Black and white at fixed alphas. These are not brand colours, but they were still
    // typed as raw literals in 33 places, which is how a "grey" ends up subtly different in
    // every panel. Tokens mean the whole UI shares one lighting model.
    inline const juce::Colour shadowFaint { 0x40000000 };
    inline const juce::Colour shadowSoft  { 0x66000000 };
    inline const juce::Colour shadowMid   { 0x99000000 };
    inline const juce::Colour shadowDeep  { 0xa6000000 };
    inline const juce::Colour glassLow    { 0x0fffffff };   // faint top highlight
    inline const juce::Colour glassMid    { 0x29ffffff };
    inline const juce::Colour glassHigh   { 0x30ffffff };

    // --- knob body ---
    //
    // The AUDIO KIT's rotary, which is a moulded cap lit from above rather than the
    // prototype's arc-and-disc:
    //
    //   body    radial-gradient(circle at 50% 42%, #232d38, #12181f 56%, #0b0f15)
    //   ticks   repeating-conic-gradient(from 214deg, #5d6c7b 0 1.4deg, transparent 1.4 30)
    //   pointer linear-gradient(180deg,#d4f4ff,#2ec5ff) + a 9px cyan glow
    //
    // The light is off-centre (42%, not 50%) - that is what stops it reading as a flat
    // circle, and it is the easiest detail to lose when eyeballing a knob.
    inline const juce::Colour knobBodyTop { 0xff232d38 };   // lit face
    inline const juce::Colour knobBodyMid { 0xff12181f };   // 56% stop
    inline const juce::Colour knobBodyBot { 0xff0b0f15 };   // shaded skirt
    inline const juce::Colour knobTick    { 0xff5d6c7b };   // the tick ring
    inline const juce::Colour knobLabel   { 0xff7d8b9a };   // caption under the knob
    inline const juce::Colour pointerTip  { 0xffd4f4ff };   // pointer gradient, top

    // Inner lighting: a hairline highlight along the top edge and a cool rim. Both are
    // low-alpha and both are load-bearing - without them the cap reads as printed on.
    inline const juce::Colour knobInnerLit { 0x24a0b9cd };  // rgba(160,185,205,0.14)
    inline const juce::Colour knobInnerRim { 0x217d96af };  // rgba(125,150,175,0.13)

    // --- geometry ---
    //
    // ONE knob size, everywhere. The kit draws a 54px rotary and draws it the same on every
    // panel; this build had three different sizes - Colour knobs came out around 88px
    // because they filled a tall cell, Space around 53, rack knobs smaller again - purely
    // because each layout sized them from whatever space was left over. Knobs that differ
    // in size read as differing in importance, which is a claim none of these panels are
    // making. Layouts now CENTRE a knob of this size in their cell rather than fill it.
    inline constexpr int knobSize = 54;

    // --- surfaces (radii) ---
    inline constexpr float cardRadius = 14.0f;
    inline constexpr float wellRadius = 10.0f;
    inline constexpr float ctrlRadius = 10.0f;

    // The knob's sweep. The kit's own maths is `-135 + value * 270`, so 270 degrees, not
    // the prototype's 280. Taken from the design's script rather than measured off a
    // picture - a five-degree error at each end is invisible alone and obvious in a row.
    inline constexpr float knobStartDeg = -135.0f;
    inline constexpr float knobEndDeg   =  135.0f;

    // The tick ring: 12 marks 30 degrees apart, each 1.4 degrees wide, the first at 214.
    // They run the whole way round in the kit - some fall outside the 270-degree sweep,
    // which is the design's choice and not a bug to tidy up.
    inline constexpr int   knobTickCount   = 12;
    inline constexpr float knobTickStepDeg = 30.0f;
    inline constexpr float knobTickFromDeg = 214.0f;
    inline constexpr float knobTickWideDeg = 1.4f;

    // --- type ---
    //
    // Chakra Petch (UI) and IBM Plex Mono (values), both embedded in the binary - see
    // Theme.cpp and the juce_add_binary_data block in CMakeLists.txt. A plugin cannot fetch
    // Google Fonts the way the design's HTML does, and falling back to a system face is how
    // a UI ends up looking nothing like its design while every colour is correct.
    //
    // Weight is the CSS number the kit uses (400/500/600/700), not a bool, because the kit
    // distinguishes 500 from 600 constantly and a bool cannot express that.
    juce::Font ui   (float size, int weight = 400);
    juce::Font mono (float size, int weight = 400);

    // These signatures used to take a bool. `true` would now convert silently to weight 1
    // and land on Regular, so every bold label in the plugin would quietly un-bold with no
    // warning anywhere. Deleting the bool overloads turns that into a compile error at each
    // call site, which is the only way to be sure all of them were actually looked at.
    juce::Font ui   (float size, bool) = delete;
    juce::Font mono (float size, bool) = delete;

    // True once the embedded faces are registered. False means every call above is falling
    // back to a system font - worth being able to ASSERT rather than squint at.
    bool fontsLoaded();
}
