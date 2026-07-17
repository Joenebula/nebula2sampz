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
    setColour(juce::ComboBox::outlineColourId,    juce::Colour(0x99000000));
    setColour(juce::ComboBox::arrowColourId,      Theme::teal);

    setColour(juce::PopupMenu::backgroundColourId,            Theme::card2);
    setColour(juce::PopupMenu::textColourId,                  Theme::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::teal.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId,       Theme::tealLit);

    setColour(juce::ToggleButton::textColourId,   Theme::sub);
    setColour(juce::ToggleButton::tickColourId,   Theme::teal);

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

    const float pad = 8.0f;
    const float R = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - pad;
    if (R <= 4.0f) return;

    const auto c = bounds.getCentre();
    const bool big = R >= 40.0f;
    const float W = juce::jlimit(3.0f, 6.0f, R * 0.22f);

    const float A0 = Theme::knobStartDeg, A1 = Theme::knobEndDeg;
    const float Av = A0 + (A1 - A0) * n;

    // Zero means zero: below this, nothing lights up.
    const bool lit = n > 0.0005f;

    // --- tick ring ---
    const int NT = big ? 13 : (R <= 20.0f ? 7 : 11);
    for (int i = 0; i < NT; ++i)
    {
        const float a = A0 + (A1 - A0) * (float) i / (float) (NT - 1);
        const bool passed = lit && a <= Av;
        const auto t = polar(c, R + (big ? 9.0f : 6.0f), a);
        const float tr = big ? 1.6f : 1.2f;
        g.setColour(passed ? Theme::teal : juce::Colour(0x29ffffff));
        g.fillEllipse(t.x - tr, t.y - tr, tr * 2.0f, tr * 2.0f);
    }

    // --- recessed track ---
    g.setColour(juce::Colour(0xa6000000));
    g.strokePath(arc(c, R, A0, A1), juce::PathStrokeType(W + 2.5f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0x0fffffff));
    g.strokePath(arc(c, R, A0, A1), juce::PathStrokeType(W, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

    // --- value arc, with a brighter leading edge ---
    if (lit)
    {
        // The SVG used a drop-shadow for the glow; here it's a wider, fainter arc under
        // the real one — same read, no blur pass.
        g.setColour(Theme::teal.withAlpha(0.22f));
        g.strokePath(arc(c, R, A0, Av), juce::PathStrokeType(W + (big ? 7.0f : 4.0f),
                                                             juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
        g.setColour(Theme::teal);
        g.strokePath(arc(c, R, A0, Av), juce::PathStrokeType(W, juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
        g.setColour(Theme::tealLit.withAlpha(0.9f));
        g.strokePath(arc(c, R, juce::jmax(A0, Av - 14.0f), Av),
                     juce::PathStrokeType(W * 0.55f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
    }

    // --- machined cap ---
    const float capR = R - W - 4.0f;
    if (capR > 3.0f)
    {
        juce::ColourGradient cap(juce::Colour(0xff39415f), c.x, c.y - capR,
                                 juce::Colour(0xff10142a), c.x, c.y + capR, false);
        g.setGradientFill(cap);
        g.fillEllipse(c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

        g.setColour(juce::Colour(0x30ffffff));
        g.drawEllipse(c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.4f);

        // Specular highlight — what makes it read as a moulded cap rather than a disc.
        g.setColour(juce::Colour(0x1affffff));
        g.fillEllipse(c.x - capR * 0.28f - capR * 0.42f,
                      c.y - capR * 0.42f - capR * 0.26f,
                      capR * 0.84f, capR * 0.52f);

        // --- pointer ---
        const auto p1 = polar(c, capR * 0.30f, Av);
        const auto p2 = polar(c, capR * 0.86f, Av);
        if (lit)
        {
            g.setColour(Theme::teal.withAlpha(0.35f));
            g.drawLine(p1.x, p1.y, p2.x, p2.y, (big ? 3.4f : 2.6f) + 3.0f);
        }
        g.setColour(lit ? Theme::teal : Theme::faint);
        g.drawLine(p1.x, p1.y, p2.x, p2.y, big ? 3.4f : 2.6f);
    }
}

void Nebula2LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    g.setColour(Theme::well);
    g.fillRoundedRectangle(b, 7.0f);
    g.setColour(juce::Colour(0x99000000));
    g.drawRoundedRectangle(b.reduced(0.5f), 7.0f, 1.0f);
    // A top inner line fakes the CSS inset shadow: it reads as recessed, not stuck on.
    g.setColour(juce::Colour(0x14ffffff));
    g.drawLine(b.getX() + 6.0f, b.getBottom() - 1.0f, b.getRight() - 6.0f, b.getBottom() - 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.isEnabled() ? Theme::teal : Theme::faint);
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

    g.setColour(on ? Theme::teal : Theme::well);
    g.fillRoundedRectangle(box, 4.0f);
    g.setColour(on ? Theme::tealLit.withAlpha(0.8f)
                   : juce::Colour(0x99000000));
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
    const auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();

    juce::ColourGradient grad = on
        ? juce::ColourGradient(Theme::tealLit, 0.0f, bounds.getY(),
                               Theme::teal, 0.0f, bounds.getBottom(), false)
        : juce::ColourGradient(juce::Colour(down ? 0xff151b32 : 0xff232c4c), 0.0f, bounds.getY(),
                               juce::Colour(down ? 0xff232c4c : 0xff151b32), 0.0f, bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 7.0f);

    g.setColour(on ? Theme::teal : (highlighted ? Theme::hiline : juce::Colour(0x66000000)));
    g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
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
    g.setColour(juce::Colour(0x40000000));
    g.fillRoundedRectangle(r.translated(0.0f, 3.0f), Theme::cardRadius);

    juce::ColourGradient grad(juce::Colour(0xff182038), 0.0f, r.getY(),
                              Theme::card2, 0.0f, r.getBottom(), false);
    grad.addColour(0.4, Theme::card);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(r, Theme::cardRadius);

    g.setColour(Theme::line);
    g.drawRoundedRectangle(r.reduced(0.5f), Theme::cardRadius, 1.0f);
    g.setColour(juce::Colour(0x12ffffff));
    g.drawLine(r.getX() + Theme::cardRadius, r.getY() + 1.0f,
               r.getRight() - Theme::cardRadius, r.getY() + 1.0f);

    if (title.isNotEmpty())
    {
        g.setColour(Theme::teal);
        g.setFont(Theme::ui(9.0f, true));
        g.drawText(title, r.reduced(14.0f, 7.0f).removeFromTop(12.0f),
                   juce::Justification::topLeft);
    }
}

void Nebula2LookAndFeel::drawWell(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(Theme::well);
    g.fillRoundedRectangle(r, Theme::wellRadius);
    g.setColour(juce::Colour(0x99000000));
    g.drawRoundedRectangle(r.reduced(0.5f), Theme::wellRadius, 1.0f);
    // --well-sh is an inset shadow; a dark top edge and a light bottom edge is the cheap,
    // convincing version of it.
    g.setColour(juce::Colour(0x66000000));
    g.drawLine(r.getX() + 4.0f, r.getY() + 1.0f, r.getRight() - 4.0f, r.getY() + 1.0f, 1.5f);
    g.setColour(juce::Colour(0x0dffffff));
    g.drawLine(r.getX() + 4.0f, r.getBottom() - 0.5f, r.getRight() - 4.0f, r.getBottom() - 0.5f);
}
