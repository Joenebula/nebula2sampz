#pragma once

#include <juce_graphics/juce_graphics.h>

// The prototype's design tokens, carried over verbatim from its CSS custom properties.
//
// This is the decision log's Phase 5 instruction: "see reference/nebula2-prototype.html
// for the design tokens and control specs to carry over, NOT the implementation". These
// are those tokens. If a colour here disagrees with the prototype, the prototype is right.
//
// One place, so a colour is never typed twice — the eleventh hard-coded 0xff3fe0d4 is how
// a theme rots.
namespace Nebula2::Theme
{
    // --- surfaces ---
    inline const juce::Colour bg      { 0xff080b13 };   // --bg
    inline const juce::Colour card    { 0xff131a2e };   // --card
    inline const juce::Colour card2   { 0xff0f1526 };   // --card2
    inline const juce::Colour card3   { 0xff242c55 };   // --card3
    inline const juce::Colour well    { 0xff05070d };   // --well  (recessed: grids, scopes, pads)
    inline const juce::Colour chassis { 0xff0d1222 };   // --chassis

    // --- lines ---
    inline const juce::Colour line   { 0x13ffffff };    // --line   rgba(255,255,255,.075)
    inline const juce::Colour hiline { 0x1cffffff };    // --hiline rgba(255,255,255,.11)

    // --- text ---
    inline const juce::Colour ink   { 0xffeef1f9 };     // --ink
    inline const juce::Colour sub   { 0xff9aa3bd };     // --sub
    inline const juce::Colour faint { 0xff5d6580 };     // --faint

    // --- accents ---
    // Every block (Colour / Space / Morph / Rack) is the same teal in the prototype: it
    // ships one accent, not four. Don't invent per-block hues — that's a design change,
    // not a port.
    inline const juce::Colour teal     { 0xff3fe0d4 };  // --teal / --colour / --space / --morph
    inline const juce::Colour tealLit  { 0xff9ff2ec };  // --teal-lit
    inline const juce::Colour coral    { 0xffff6a4d };  // --coral  (warnings, hot states)
    inline const juce::Colour coralLit { 0xffffa38f };  // --coral-lit
    inline const juce::Colour cv       { 0xffffd166 };  // the LFO/CV yellow

    inline const juce::Colour onAccent { 0xff06121a };  // --on-accent (text ON teal)

    // --- geometry ---
    inline constexpr float cardRadius = 14.0f;          // --r
    inline constexpr float wellRadius = 10.0f;

    // The knob's sweep, from nbKnobSVG: -140deg to +140deg. Not a full circle — you can
    // see where the ends are, which is the whole point of a pointer knob.
    inline constexpr float knobStartDeg = -140.0f;
    inline constexpr float knobEndDeg   =  140.0f;

    inline juce::Font mono (float size, bool bold = false)
    {
        // "IBM Plex Mono" in the prototype: values are monospaced so they don't jitter as
        // digits change. Falls back to whatever mono the OS has.
        return juce::Font (juce::FontOptions ("Consolas", size,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font ui (float size, bool bold = false)
    {
        // "Chakra Petch" in the prototype. Not safe to assume it's installed, so this is
        // the nearest ubiquitous face rather than a silent fallback to Times.
        return juce::Font (juce::FontOptions ("Segoe UI", size,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
}
