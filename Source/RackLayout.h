#pragma once

#include <juce_graphics/juce_graphics.h>

namespace Nebula2
{
    // The rack module's bands, as PURE GEOMETRY - no components, no processor, no window.
    //
    // This lives apart from RackView so it can be tested. The whole reason it exists is that
    // module layout was being written blind: offsets computed in five places against the
    // same rectangle, compiled, and never seen until a screenshot came back with truncated
    // titles, "OUT" printed over "IN", and knobs missing entirely. Pure geometry can be
    // asserted about; a paint() call cannot.
    //
    // The bands are carved IN ORDER from one rectangle, so a band can only use what the
    // bands before it did not take. Overlap is not "avoided" here, it is impossible.
    struct ModuleBands
    {
        juce::Rectangle<float> power;     // round on/off button, top-left
        juce::Rectangle<float> title;     // module name, CAPS
        juce::Rectangle<float> caption;   // subtitle, right-aligned
        juce::Rectangle<float> dice;      // per-module randomise, top-right
        juce::Rectangle<float> state;     // LIVE / IDLE / NO PATH OUT
        juce::Rectangle<float> screen;    // scope / curve / formants (empty if none)
        juce::Rectangle<float> knobs;     // the dial row
        juce::Rectangle<float> jacks;     // IN / CV / OUT strip
    };

    struct ModuleBandSpec
    {
        float padX = 14.0f, padY = 12.0f;
        float powerD = 26.0f, diceD = 26.0f;
        float stateH = 13.0f, jacksH = 26.0f, screenH = 30.0f;
        float gap = 8.0f;
    };

    // captionW is the measured width of the caption TEXT. Passing a fixed guess instead is
    // what squeezed "Ladder" down to "LAD": the title got whatever a constant had not
    // already claimed, rather than the caption taking only what it needed.
    inline ModuleBands layoutModuleBands (juce::Rectangle<float> bounds,
                                          bool hasPower, bool hasScreen, bool isEq,
                                          float captionW,
                                          ModuleBandSpec s = {})
    {
        ModuleBands b;
        auto r = bounds.reduced (s.padX, s.padY);
        if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f) return b;

        // 1. header
        {
            auto head = r.removeFromTop (juce::jmin (s.powerD, r.getHeight()));

            if (hasPower && head.getWidth() > s.powerD + s.diceD + 24.0f)
            {
                b.power = head.removeFromLeft (s.powerD);
                head.removeFromLeft (10.0f);
                b.dice = head.removeFromRight (s.diceD);
                head.removeFromRight (10.0f);
            }

            const float capW = juce::jlimit (0.0f, head.getWidth() * 0.5f, captionW);
            b.caption = head.removeFromRight (capW);
            if (capW > 0.0f) head.removeFromRight (s.gap);
            b.title = head;
        }

        if (r.getHeight() <= 0.0f) return b;
        b.state = r.removeFromTop (juce::jmin (s.stateH, r.getHeight()));

        // Jacks come off the BOTTOM before anything claims the middle, so the knob row can
        // never grow over them however tall the module is.
        if (r.getHeight() > 0.0f)
            b.jacks = r.removeFromBottom (juce::jmin (s.jacksH, r.getHeight()));

        if (isEq)
        {
            b.screen = r;      // the EQ's response curve is its entire body
            return b;
        }

        if (hasScreen && r.getHeight() > 0.0f)
        {
            b.screen = r.removeFromTop (juce::jmin (s.screenH, r.getHeight()));
            if (r.getHeight() > 6.0f) r.removeFromTop (6.0f);
        }

        b.knobs = r;
        return b;
    }

    // The height a module NEEDS, from the same spec the bands are carved with. Written as
    // the sum of the bands rather than as its own arithmetic: when these two disagreed, a
    // module was given less room than its contents required and everything inside collided.
    inline float moduleBandsHeight (bool hasScreen, bool isEq, bool hasKnobs,
                                    float knobRowH, float eqCurveH,
                                    ModuleBandSpec s = {})
    {
        float h = s.padY * 2.0f + s.powerD + s.stateH + s.jacksH;
        if (isEq)      return h + eqCurveH;
        if (hasScreen) h += s.screenH + 6.0f;
        if (hasKnobs)  h += knobRowH;
        return h;
    }
}
