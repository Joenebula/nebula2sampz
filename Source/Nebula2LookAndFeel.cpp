#include "Nebula2LookAndFeel.h"

using namespace Nebula2;

namespace
{
    // The prototype's nbPolar: 0deg is 12 o'clock, positive clockwise.
    juce::Point<float> polar(juce::Point<float> c, float r, float deg)
    {
        const float a = juce::degreesToRadians(deg - 90.0f);
        return { c.x + r * std::cos(a), c.y + r * std::sin(a) };
    }

    juce::Path arc(juce::Point<float> c, float r, float fromDeg, float toDeg)
    {
        juce::Path p;
        p.addCentredArc(c.x, c.y, r, r,
                        0.0f,
                        juce::degreesToRadians(fromDeg),
                        juce::degreesToRadians(toDeg),
                        true);
        return p;
    }
}

Nebula2LookAndFeel::Nebula2LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, Theme::bg);

    setColour(juce::Slider::textBoxTextColourId,       Theme::ink);
    setColour(juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::Label::textColourId, Theme::sub);

    setColour(juce::ComboBox::backgroundColourId, Theme::well);
    setColour(juce::ComboBox::textColourId,       Theme::ink);
    setColour(juce::ComboBox::outlineColourId,    Nebula2::Theme::shadowMid);
    setColour(juce::ComboBox::arrowColourId,      Theme::accent);

    setColour(juce::PopupMenu::backgroundColourId,            Theme::card2);
    setColour(juce::PopupMenu::textColourId,                  Theme::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId,       Theme::accentLit);

    setColour(juce::ToggleButton::textColourId,   Theme::sub);
    setColour(juce::ToggleButton::tickColourId,   Theme::accent);

    setColour(juce::TextButton::buttonColourId,   Theme::card3);
    setColour(juce::TextButton::textColourOffId,  Theme::sub);
    setColour(juce::TextButton::textColourOnId,   Theme::onAccent);

    setColour(juce::ScrollBar::thumbColourId,     Theme::card3);
    setColour(juce::ScrollBar::trackColourId,     Theme::well);

    setColour(juce::TooltipWindow::backgroundColourId, Theme::card2);
    setColour(juce::TooltipWindow::textColourId,       Theme::ink);
}

void Nebula2LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float, juce::Slider&)
{
    const float n = juce::jlimit(0.0f, 1.0f, sliderPos);
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Scale the padding rather than subtracting a fixed 8px: a fixed pad turns a small
    // cell NEGATIVE and the knob silently disappears. A control that renders as nothing is
    // the worst possible failure — it looks like a bug in the plugin, not in the layout.
    const float d = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const float R = d * 0.5f - juce::jmin(8.0f, d * 0.12f);
    if (R <= 1.0f) return;   // genuinely zero-sized; nothing sensible to draw

    const auto c = bounds.getCentre();

    const float A0 = Theme::knobStartDeg, A1 = Theme::knobEndDeg;
    const float Av = A0 + (A1 - A0) * n;

    // THE AUDIO KIT'S KNOB. Three layers and no value arc:
    //
    //   1. a ring of tick marks, fixed - they do NOT light up as the value passes them
    //   2. a moulded body, lit from 42% rather than the centre
    //   3. a pointer line with a cyan glow
    //
    // The prototype's knob was an arc-and-disc: a filled sweep showed the value at a
    // glance. The kit deliberately does not do that, which is a real change in what the
    // control tells you - the value lives in the pointer angle and the mono readout
    // underneath instead. Dropping the arc WITHOUT that readout would lose information,
    // so drawValueReadout below is part of this change rather than a later polish.

    // --- 1. tick ring ---
    // Every 30 degrees from 214, all the way round. Proportions taken from the kit's mask:
    // the ring sits at r 25..26.5 of a 27px radius, i.e. 0.926..0.981.
    {
        const float rIn = R * 0.926f, rOut = R * 0.981f;
        g.setColour (Nebula2::Theme::knobTick);

        for (int i = 0; i < Theme::knobTickCount; ++i)
        {
            const float a = Theme::knobTickFromDeg + Theme::knobTickStepDeg * (float) i;
            // Drawn as a short radial stroke rather than a filled wedge: at 1.4 degrees the
            // two are indistinguishable, and a stroke stays crisp at small sizes.
            const auto p1 = polar (c, rIn,  a - 90.0f);
            const auto p2 = polar (c, rOut, a - 90.0f);
            g.drawLine (p1.x, p1.y, p2.x, p2.y, juce::jmax (1.0f, R * 0.05f));
        }
    }

    // --- 2. moulded body ---
    // inset 4 of 54 in the kit = 0.852 of the diameter.
    const float bodyR = R * 0.852f;
    if (bodyR <= 2.0f) return;

    {
        // Radial, centred at 50% 42% - above centre. An even fill reads as a flat circle;
        // this is what makes it look turned.
        juce::ColourGradient body (Nebula2::Theme::knobBodyTop,
                                   c.x, c.y - bodyR * 0.16f,
                                   Nebula2::Theme::knobBodyBot,
                                   c.x, c.y + bodyR, true);
        body.addColour (0.56, Nebula2::Theme::knobBodyMid);
        g.setGradientFill (body);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        // Cool rim, then a hairline lit edge across the top only. Both low-alpha and both
        // load-bearing: without them the cap reads as printed on rather than moulded.
        g.setColour (Nebula2::Theme::knobInnerRim);
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        g.setColour (Nebula2::Theme::knobInnerLit);
        juce::Path topLit;
        topLit.addCentredArc (c.x, c.y, bodyR - 0.5f, bodyR - 0.5f, 0.0f,
                              juce::degreesToRadians (-70.0f),
                              juce::degreesToRadians (70.0f), true);
        g.strokePath (topLit, juce::PathStrokeType (1.2f));
    }

    // --- 3. pointer ---
    // 2px wide, 9px tall, starting 4px in from the body's top edge: near the rim, not a
    // spoke from the centre. Proportions from the kit's 54px knob.
    {
        const float len   = bodyR * 0.39f;
        const float inset = bodyR * 0.17f;
        const float wid   = juce::jmax (1.5f, R * 0.074f);

        const auto tip  = polar (c, bodyR - inset,       Av);
        const auto tail = polar (c, bodyR - inset - len, Av);

        // The glow, as a wider soft stroke under the line - the kit uses a 9px blur.
        g.setColour (Theme::accent.withAlpha (0.32f));
        g.drawLine (tail.x, tail.y, tip.x, tip.y, wid + 4.5f);
        g.setColour (Theme::accent.withAlpha (0.5f));
        g.drawLine (tail.x, tail.y, tip.x, tip.y, wid + 2.0f);

        // The line itself: pale at the tip, accent at the tail.
        juce::ColourGradient pg (Nebula2::Theme::pointerTip, tip.x, tip.y,
                                 Theme::accent, tail.x, tail.y, false);
        g.setGradientFill (pg);
        g.drawLine (tail.x, tail.y, tip.x, tip.y, wid);
    }
}

void Nebula2LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto b = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float cy = b.getCentreY();
    const float trackH = 5.0f;

    // Recessed track — the same well treatment as everything else that holds a value.
    auto track = juce::Rectangle<float>(b.getX(), cy - trackH * 0.5f, b.getWidth(), trackH);
    g.setColour(Theme::well);
    g.fillRoundedRectangle(track, trackH * 0.5f);
    g.setColour(Nebula2::Theme::shadowMid);
    g.drawRoundedRectangle(track.reduced(0.25f), trackH * 0.5f, 1.0f);

    // Filled portion. Zero means zero: at the bottom of the range nothing lights up.
    const float fillW = juce::jlimit(0.0f, b.getWidth(), sliderPos - b.getX());
    if (fillW > 1.0f)
    {
        g.setColour(Theme::accent.withAlpha(0.25f));
        g.fillRoundedRectangle(track.withWidth(fillW).expanded(0.0f, 2.0f), trackH * 0.5f + 2.0f);
        g.setColour(Theme::accent);
        g.fillRoundedRectangle(track.withWidth(fillW), trackH * 0.5f);
    }

    // Thumb, machined like the knob caps so the family reads as one set of controls.
    const float tr = juce::jmin(7.0f, b.getHeight() * 0.5f);
    juce::ColourGradient cap(Nebula2::Theme::knobBodyTop, sliderPos, cy - tr,
                             Nebula2::Theme::knobBodyBot, sliderPos, cy + tr, false);
    g.setGradientFill(cap);
    g.fillEllipse(sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f);
    g.setColour(s.isEnabled() ? Theme::accentLit.withAlpha(0.8f) : Theme::faint);
    g.drawEllipse(sliderPos - tr, cy - tr, tr * 2.0f, tr * 2.0f, 1.2f);
}

void Nebula2LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    g.setColour(Theme::well);
    g.fillRoundedRectangle(b, 7.0f);
    g.setColour(Nebula2::Theme::shadowMid);
    g.drawRoundedRectangle(b.reduced(0.5f), 7.0f, 1.0f);
    // A top inner line fakes the CSS inset shadow: it reads as recessed, not stuck on.
    g.setColour(Nebula2::Theme::glassLow);
    g.drawLine(b.getX() + 6.0f, b.getBottom() - 1.0f, b.getRight() - 6.0f, b.getBottom() - 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.isEnabled() ? Theme::accent : Theme::faint);
    g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void Nebula2LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 0, box.getWidth() - 26, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

void Nebula2LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                                          bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float boxSize = juce::jmin(15.0f, bounds.getHeight() - 2.0f);
    auto box = juce::Rectangle<float>(bounds.getX() + 1.0f,
                                      bounds.getCentreY() - boxSize * 0.5f,
                                      boxSize, boxSize);
    const bool on = b.getToggleState();

    g.setColour(on ? Theme::accent : Theme::well);
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(on ? Theme::accentLit.withAlpha(0.8f)
                   : Nebula2::Theme::shadowMid);
    g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 3.5f, box.getCentreY());
        tick.lineTo(box.getCentreX() - 0.5f, box.getBottom() - 4.0f);
        tick.lineTo(box.getRight() - 3.0f, box.getY() + 4.0f);
        g.setColour(Theme::onAccent);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    g.setColour(b.isEnabled() ? (highlighted ? Theme::ink : Theme::sub) : Theme::faint);
    g.setFont(Theme::ui(11.0f));
    g.drawText(b.getButtonText(),
               bounds.withTrimmedLeft(boxSize + 8.0f), juce::Justification::centredLeft);
}

void Nebula2LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                              const juce::Colour&, bool highlighted, bool down)
{
    // THE AUDIO KIT'S BUTTON. Two states, both 40h with a 9px radius:
    //
    //   primary    linear-gradient(180deg,#6bb9ec,#1f6fae)
    //              0 0 16px rgba(46,197,255,0.45) + inset 0 1px 0 rgba(255,255,255,0.3)
    //   secondary  a dark bevel: inset 0 1px 0 rgba(255,255,255,0.09)
    //                          + inset 0 -2px 5px rgba(0,0,0,0.5)
    //
    // The old version was close but not the kit: a 7px radius, an accent-to-accentLit fill
    // for the on state instead of the blue gradient, an outline the kit does not draw, and
    // two raw hex literals for the off state. Those literals slipped past the colour gate
    // because they sat behind a ternary - the check has been widened.
    const auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const float r = 9.0f;

    if (on)
    {
        // The glow first, under the body - the kit's 16px cyan spread.
        g.setColour (Theme::accent.withAlpha (0.28f));
        g.fillRoundedRectangle (bounds.expanded (2.5f), r + 2.0f);

        juce::ColourGradient grad (Nebula2::Theme::btnTop, 0.0f, bounds.getY(),
                                   Nebula2::Theme::btnBot, 0.0f, bounds.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, r);

        // inset 0 1px 0 rgba(255,255,255,0.3) - a lit top edge, not a full outline.
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawLine (bounds.getX() + r * 0.6f, bounds.getY() + 1.0f,
                    bounds.getRight() - r * 0.6f, bounds.getY() + 1.0f, 1.0f);
    }
    else
    {
        juce::ColourGradient grad (Theme::card3, 0.0f, bounds.getY(),
                                   Theme::card2, 0.0f, bounds.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, r);

        // The bevel: lit along the top, shadowed along the bottom inside edge.
        g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.16f : 0.09f));
        g.drawLine (bounds.getX() + r * 0.6f, bounds.getY() + 1.0f,
                    bounds.getRight() - r * 0.6f, bounds.getY() + 1.0f, 1.0f);

        g.setColour (Nebula2::Theme::shadowSoft);
        g.drawLine (bounds.getX() + r * 0.6f, bounds.getBottom() - 1.5f,
                    bounds.getRight() - r * 0.6f, bounds.getBottom() - 1.5f, 2.0f);
    }

    // Pressed: the kit scales to 0.94. A repaint cannot scale the component, so this is a
    // darkening wash instead - same "it went down" read, no layout thrash.
    if (down)
    {
        g.setColour (Nebula2::Theme::shadowFaint);
        g.fillRoundedRectangle (bounds, r);
    }
}

void Nebula2LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                                         bool /*highlighted*/, bool /*down*/)
{
    // The kit sets button labels at 600 11px Chakra with 1.5px of letter-spacing, in CAPS.
    // The tracking is the part that reads as "hardware" rather than "web form", and JUCE
    // has no letter-spacing property - withExtraKerningFactor is a FRACTION OF THE FONT
    // HEIGHT, so 1.5px at 11px is 0.136, not 1.5.
    auto font = Theme::ui (11.0f, 600).withExtraKerningFactor (1.5f / 11.0f);
    g.setFont (font);

    const bool on = b.getToggleState();
    g.setColour (on ? Theme::ink
                    : b.findColour (juce::TextButton::textColourOffId));

    g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(),
                juce::Justification::centred, false);
}

void Nebula2LookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited()) { LookAndFeel_V4::drawLabel(g, label); return; }

    g.setColour(label.findColour(juce::Label::textColourId)
                     .withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.45f));
    g.setFont(getLabelFont(label));
    g.drawFittedText(label.getText(), label.getLocalBounds(),
                     label.getJustificationType(), 1, 1.0f);
}

juce::Font Nebula2LookAndFeel::getLabelFont(juce::Label& label)
{
    return Theme::ui(juce::jlimit(9.0f, 12.0f, (float) label.getHeight() * 0.72f));
}

juce::Font Nebula2LookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return Theme::ui(11.0f);
}

juce::Font Nebula2LookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return Theme::ui(11.0f);
}

void Nebula2LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(Theme::card2);
    g.setColour(Theme::hiline);
    g.drawRect(0, 0, width, height, 1);
}

void Nebula2LookAndFeel::drawCard(juce::Graphics& g, juce::Rectangle<float> r,
                                  const juce::String& title)
{
    // --card-sh: a drop shadow plus a 1px inner top highlight. The highlight is what makes
    // the panel read as raised rather than as a flat rectangle.
    g.setColour(Nebula2::Theme::shadowFaint);
    g.fillRoundedRectangle(r.translated(0.0f, 3.0f), Theme::cardRadius);

    // The kit's panel: a simple two-stop linear-gradient(180deg,#141b24,#0b1017). It had a
    // third stop and a knob token for its top colour, which is how a panel ends up made of
    // knob-coloured plastic.
    juce::ColourGradient grad(Theme::card, 0.0f, r.getY(),
                              Nebula2::Theme::chassis, 0.0f, r.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, Theme::cardRadius);

    g.setColour(Theme::line);
    g.drawRoundedRectangle(r.reduced(0.5f), Theme::cardRadius, 1.0f);
    g.setColour(Nebula2::Theme::glassLow);
    g.drawLine(r.getX() + Theme::cardRadius, r.getY() + 1.0f,
               r.getRight() - Theme::cardRadius, r.getY() + 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour(Theme::accent);
        g.setFont(Theme::ui(9.0f, 600));
        g.drawText(title, r.reduced(14.0f, 7.0f).removeFromTop(12.0f),
                   juce::Justification::topLeft);
    }
}

void Nebula2LookAndFeel::drawWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(Theme::well);
    g.fillRoundedRectangle(r, Theme::wellRadius);
    g.setColour(Nebula2::Theme::shadowMid);
    g.drawRoundedRectangle(r.reduced(0.5f), Theme::wellRadius, 1.0f);
    // --well-sh is an inset shadow; a dark top edge and a light bottom edge is the cheap,
    // convincing version of it.
    g.setColour(Nebula2::Theme::shadowSoft);
    g.drawLine(r.getX() + 4.0f, r.getY() + 1.0f, r.getRight() - 4.0f, r.getY() + 1.0f, 1.5f);
    g.setColour(Nebula2::Theme::line);
    g.drawLine(r.getX() + 4.0f, r.getBottom() - 0.5f, r.getRight() - 4.0f, r.getBottom() - 0.5f);
}
