#include "RackView.h"
#include "dsp/EqCurveMath.h"
#include "ParameterIDs.h"
#include "Theme.h"

using namespace Nebula2;

namespace
{
    const auto& kWell   = Nebula2::Theme::well;
    const auto& kPanel  = Nebula2::Theme::chassis;
    const auto& kAccent = Nebula2::Theme::accent;
    const auto& kCV     = Nebula2::Theme::warn;
    const auto& kDang   = Nebula2::Theme::danger;
    const auto& kSub    = Nebula2::Theme::sub;
    const auto& kLine   = Nebula2::Theme::hiline;

    constexpr float jackR = 5.0f;

    // The rack's layout: 4 columns x 3 rows. Beat top-left, Main Out top-right, so signal
    // reads left-to-right the way you'd wire a real one.
    struct Slot { ModuleId m; int col, row; };
    const Slot slots[] =
    {
        { ModuleId::src, 0, 0 }, { ModuleId::eq,  1, 0 }, { ModuleId::flt, 2, 0 }, { ModuleId::out, 3, 0 },
        { ModuleId::lfo, 0, 1 }, { ModuleId::phs, 1, 1 }, { ModuleId::cho, 2, 1 }, { ModuleId::cmb, 3, 1 },
        { ModuleId::fld, 0, 2 }, { ModuleId::vow, 1, 2 }, { ModuleId::ech, 2, 2 },
    };

    juce::Colour colourForState(ModuleState s)
    {
        switch (s)
        {
            case ModuleState::live:      return kAccent;
            case ModuleState::noPathOut: return kDang;
            case ModuleState::off:       return kSub.withAlpha(0.4f);
            default:                     return kSub.withAlpha(0.55f);
        }
    }
}

RackView::RackView(Nebula2AudioProcessor& p) : processorRef(p), eqCurve(p)
{
    startTimerHz(24);
    buildModuleDials(p.getValueTreeState());
    addAndMakeVisible(eqCurve);
}

void RackView::addDial(juce::AudioProcessorValueTreeState& apvts, ModuleId owner,
                       const juce::String& paramID, const juce::String& label)
{
    Dial d;
    d.slider = std::make_unique<juce::Slider>();
    d.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    d.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);   // the module is small;
                                                                      // the tooltip carries the value
    d.slider->setPopupDisplayEnabled(true, true, this);
    d.slider->setTooltip(label);
    d.label = label;
    d.owner = owner;
    addAndMakeVisible(*d.slider);
    d.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramID, *d.slider);
    dials.push_back(std::move(d));
}

void RackView::buildModuleDials(juce::AudioProcessorValueTreeState& apvts)
{
    // ONE table, in RackGraph.cpp, looped here. It used to be this list written out by hand,
    // and five parameters the DSP reads every block (phsDepth, echWow and three of the six
    // EQ bands) simply were not in it — so nothing on screen could move them and they sat at
    // their defaults forever. Building the knobs FROM the table means the list and the panel
    // cannot disagree; a test then checks the table covers every rack DSP parameter.
    for (const auto& d : rackDialDefs())
        addDial(apvts, d.owner, d.paramId, d.label);
}

void RackView::timerCallback()
{
    if (messageTicks > 0 && --messageTicks == 0) message.clear();

    const float v = processorRef.getRackLfoValue();
    if (std::abs(v - lfoDot) > 0.005f) { lfoDot = v; repaint(); return; }
    repaint();      // the state flags follow the graph, which the editor can change
}

void RackView::resized() { rebuildLayout(); }

juce::Rectangle<float> RackView::powerRectFor (ModuleId m) const
{
    if (! hasPower (m)) return {};
    return modBounds[(int) m].reduced (6.0f, 0.0f).withTrimmedTop (4.0f)
                             .withWidth (20.0f).withHeight (11.0f);
}

juce::Rectangle<float> RackView::screenAreaFor (ModuleId m) const
{
    // A strip under the name, above the dials. Modules with dials get a thin one; the two
    // with no dials at all (Beat Out, LFO has two) can afford more.
    auto r = modBounds[(int) m].reduced (18.0f, 0.0f).withTrimmedTop (26.0f);
    const float h = juce::jmin (34.0f, r.getHeight() * 0.45f);
    return r.withHeight (juce::jmax (12.0f, h));
}

void RackView::drawModuleScreens (juce::Graphics& g) const
{
    using namespace Nebula2;

    auto well = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (Nebula2::Theme::chassis);
        g.fillRoundedRectangle (r, 3.0f);
    };

    const auto& dials = processorRef.getRackDialsSnapshot();

    // --- Beat Out: the beat going in ---
    {
        auto r = screenAreaFor (ModuleId::src);
        well (r);
        juce::Path p;
        const int n = Nebula2AudioProcessor::scopePoints;
        for (int i = 0; i < n; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, processorRef.getScopePoint (i));
            const float x = r.getX() + r.getWidth() * ((float) i / (float) (n - 1));
            const float half = r.getHeight() * 0.5f * v;
            if (i == 0) p.startNewSubPath (x, r.getCentreY() - half);
            else        p.lineTo (x, r.getCentreY() - half);
        }
        for (int i = n - 1; i >= 0; --i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, processorRef.getScopePoint (i));
            const float x = r.getX() + r.getWidth() * ((float) i / (float) (n - 1));
            p.lineTo (x, r.getCentreY() + r.getHeight() * 0.5f * v);
        }
        p.closeSubPath();
        g.setColour (kAccent.withAlpha (0.75f));
        g.fillPath (p);
    }

    // --- Main Out: its own level ---
    {
        auto r = screenAreaFor (ModuleId::out);
        well (r);
        const float lvl = juce::jlimit (0.0f, 1.0f, processorRef.getRackOutLevel());
        auto bar = r.reduced (3.0f).withWidth ((r.getWidth() - 6.0f) * lvl);
        // Green until it is close to clipping, then red. A meter that reads the same at
        // -20 and at 0 dBFS is just a moving rectangle.
        g.setColour (lvl > 0.95f ? Nebula2::Theme::danger : kAccent.withAlpha (0.8f));
        g.fillRoundedRectangle (bar, 2.0f);
    }

    // --- LFO: its shape, and where in the cycle it is ---
    {
        auto r = screenAreaFor (ModuleId::lfo);
        well (r);
        juce::Path p;
        const int n = 48;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float v = lfoShapeAt (dials.lfoShape, t);
            const float x = r.getX() + r.getWidth() * t;
            const float y = r.getCentreY() - v * r.getHeight() * 0.4f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (kCV.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.3f));

        // The dot: the LFO's actual current value, so the picture MOVES with the sound
        // rather than being a static drawing of a sine wave.
        const float v = juce::jlimit (-1.0f, 1.0f, processorRef.getRackLfoValue());
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (r.getRight() - 4.0f, r.getCentreY() - v * r.getHeight() * 0.4f - 2.0f,
                       4.0f, 4.0f);
    }

    // --- Wavefolder: its transfer curve ---
    {
        auto r = screenAreaFor (ModuleId::fld);
        well (r);
        juce::Path p;
        const int n = 64;
        for (int i = 0; i <= n; ++i)
        {
            const float x01 = (float) i / (float) n;
            const float in  = x01 * 2.0f - 1.0f;
            const float out = foldSample (in, dials.fldDrive / 100.0f, dials.fldSym / 100.0f);
            const float x = r.getX() + r.getWidth() * x01;
            const float y = r.getCentreY() - out * r.getHeight() * 0.42f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (Nebula2::Theme::warn.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    // --- Vowel: the formant peaks it is making ---
    {
        auto r = screenAreaFor (ModuleId::vow);
        well (r);
        float f[3];
        vowelFormantsAt (dials.vowMorph, f);

        juce::Path p;
        const int n = 64;
        for (int i = 0; i <= n; ++i)
        {
            const float t = (float) i / (float) n;
            const float db = vowelResponseDb (f, dials.vowSharp, eqNormToFreq (t));
            const float x = r.getX() + r.getWidth() * t;
            const float y = r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, eqGainToNorm (db, 20.0f));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (Nebula2::Theme::accentPale.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.3f));

        // Which vowel, in words. "2.31" tells you nothing about what you are about to hear.
        g.setColour (kSub.withAlpha (0.7f));
        g.setFont (juce::FontOptions (8.0f));
        static const char* names[] = { "A", "E", "I", "O", "U" };
        const int vi = juce::jlimit (0, 4, (int) juce::jlimit (0.0f, 4.0f, dials.vowMorph));
        g.drawText (names[vi], r.reduced (3.0f, 1.0f), juce::Justification::topRight);
    }
}

juce::Rectangle<float> RackView::boundsFor(ModuleId m) const
{
    return modBounds[(int) m];
}

void RackView::rebuildLayout()
{
    jacks.clear();

    const float w = (float) getWidth(), h = (float) getHeight() - 18.0f;   // strip at the bottom
    const float cw = w / 4.0f, chh = h / 3.0f;
    const float pad = 4.0f;

    for (const auto& s : slots)
    {
        juce::Rectangle<float> r ((float) s.col * cw + pad, (float) s.row * chh + pad,
                                  cw - pad * 2.0f, chh - pad * 2.0f);
        modBounds[(int) s.m] = r;

        // Inputs down the left edge, outputs down the right — so a cable's direction is
        // legible at a glance rather than something you have to remember.
        if (hasJack (s.m, Jack::in))
            jacks.push_back ({ { s.m, Jack::in },  { r.getX() + 8.0f, r.getCentreY() } });
        if (hasJack (s.m, Jack::cv))
            jacks.push_back ({ { s.m, Jack::cv },  { r.getX() + 8.0f, r.getCentreY() + 14.0f } });
        if (hasJack (s.m, Jack::out))
            jacks.push_back ({ { s.m, Jack::out }, { r.getRight() - 8.0f, r.getCentreY() } });
    }

    // Dials sit in the module's middle, between the jack columns. Clicking a module body
    // toggles bypass, so the dials must not sit under that gesture — they're inset, and
    // mouseDown checks for a jack/dial before treating a click as a power toggle.
    for (const auto& s : slots)
    {
        std::vector<Dial*> mine;
        for (auto& d : dials)
            if (d.owner == s.m) mine.push_back (&d);
        if (mine.empty()) continue;

        auto r = modBounds[(int) s.m].reduced (20.0f, 0.0f).withTrimmedTop (16.0f)
                                     .withTrimmedBottom (4.0f);
        const float w = r.getWidth() / (float) mine.size();
        for (size_t i = 0; i < mine.size(); ++i)
        {
            auto cell = r.withX (r.getX() + w * (float) i).withWidth (w).reduced (2.0f, 0.0f);

            // Reserve the bottom strip for the caption and value, as the kit does. The knob
            // takes what's left rather than the whole cell; drawing the text over the knob
            // is the only other way to fit it, and it is unreadable.
            auto cellI = cell.toNearestInt();
            const int textH = juce::jmin (22, cellI.getHeight() / 3);
            mine[i]->textArea = cellI.removeFromBottom (textH);
            mine[i]->slider->setBounds (cellI);
        }
    }

    // The EQ has no dials at all - its whole control surface is the curve, which fills the
    // module body between the jack columns.
    eqCurve.setBounds (modBounds[(int) ModuleId::eq]
                           .reduced (18.0f, 0.0f)
                           .withTrimmedTop (26.0f)      // clear of the name and its subtitle
                           .withTrimmedBottom (4.0f)
                           .toNearestInt());
}

juce::Point<float> RackView::posOf(const Port& port) const
{
    for (const auto& j : jacks)
        if (j.port == port) return j.pos;
    return {};
}

const RackView::JackSpot* RackView::jackAt(juce::Point<float> p) const
{
    for (const auto& j : jacks)
        if (j.pos.getDistanceFrom (p) <= jackR * 2.4f) return &j;
    return nullptr;
}

ModuleId RackView::moduleAt(juce::Point<float> p) const
{
    for (const auto& s : slots)
        if (modBounds[(int) s.m].contains (p)) return s.m;
    return ModuleId::count;
}

void RackView::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (const auto* j = jackAt (p))
    {
        auto& g = processorRef.getRackGraph();

        // Grabbing a patched INPUT unplugs it — that's how a real cable behaves, and it
        // saves inventing a delete gesture nobody would find.
        if (j->port.jack != Jack::out)
        {
            Port fromFound;
            bool found = false;
            {
                const juce::SpinLock::ScopedLockType sl (processorRef.getRackLock());
                for (const auto& c : g.getCables())
                    if (c.to == j->port) { fromFound = c.from; found = true; break; }
                if (found) g.removeCable (fromFound, j->port);
            }
            if (found)
            {
                message = "Unplugged";
                messageTicks = 40;
                repaint();
                return;
            }
        }

        if (j->port.jack == Jack::out)
        {
            dragging = true;
            dragFrom = j->port;
            dragPos = p;
            repaint();
        }
        return;
    }

    // The On/Off button, or anywhere else along the header. The button is the affordance -
    // something visible that says this can be switched - and the rest of the header stays
    // live because it always was and it is a far easier target to hit.
    //
    // Not the whole body: the body holds dials and a screen, and a stray click that
    // silently bypassed a module would be a nasty surprise mid-take.
    const auto m = moduleAt (p);
    if (hasPower (m))
    {
        const auto header = modBounds[(int) m].withHeight (16.0f);
        if (! header.contains (p) && ! powerRectFor (m).contains (p)) return;

        auto& g = processorRef.getRackGraph();
        const juce::SpinLock::ScopedLockType sl (processorRef.getRackLock());
        g.setBypassed (m, ! g.isBypassed (m));
        message = juce::String (moduleName (m)) + (g.isBypassed (m) ? " off" : " on");
        messageTicks = 40;
        repaint();
    }
}

void RackView::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragPos = e.position;
    repaint();
}

void RackView::mouseUp(const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;

    if (const auto* j = jackAt (e.position))
    {
        auto& g = processorRef.getRackGraph();
        PatchResult r;
        {
            const juce::SpinLock::ScopedLockType sl (processorRef.getRackLock());
            r = g.addCable (dragFrom, j->port);
        }
        // A refused patch says why. A cable that just fails to appear teaches you nothing.
        message = patchResultText (r);
        messageTicks = 60;
    }
    repaint();
}

void RackView::clearPatch()
{
    auto& g = processorRef.getRackGraph();
    const juce::SpinLock::ScopedLockType sl (processorRef.getRackLock());
    g.clear();
    message = "Patch cleared";
    messageTicks = 60;
    repaint();
}

void RackView::paint(juce::Graphics& g)
{
    g.fillAll (kWell);

    auto& graph = processorRef.getRackGraph();

    // Snapshot what we need under the lock, then draw without holding it — painting is slow
    // and the audio thread only try-locks; holding it here would make it drop the rack.
    std::vector<Cable> cables;
    std::array<ModuleState, numRackModules> states {};
    bool live = false;
    {
        const juce::SpinLock::ScopedLockType sl (processorRef.getRackLock());
        cables = graph.getCables();
        for (int i = 0; i < numRackModules; ++i) states[i] = graph.stateOf ((ModuleId) i);
        live = graph.isLive();
    }

    // --- modules ---
    for (const auto& s : slots)
    {
        const auto r = modBounds[(int) s.m];
        const auto st = states[(int) s.m];
        const auto col = colourForState (st);

        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (col.withAlpha (st == ModuleState::live ? 0.7f : 0.25f));
        g.drawRoundedRectangle (r, 5.0f, st == ModuleState::live ? 1.4f : 0.8f);

        // The power button, and the name shifted clear of it. Drawn before the name so the
        // name's inset can depend on whether there IS one.
        const float nameInset = hasPower (s.m) ? 30.0f : 8.0f;
        if (hasPower (s.m))
        {
            const auto pr = powerRectFor (s.m);
            const bool off = st == ModuleState::off;

            g.setColour (off ? kSub.withAlpha (0.18f) : kAccent.withAlpha (0.30f));
            g.fillRoundedRectangle (pr, 3.0f);
            g.setColour (off ? kSub.withAlpha (0.5f) : kAccent.withAlpha (0.9f));
            g.drawRoundedRectangle (pr, 3.0f, 0.8f);

            // The word, not just a colour. "Off" beside a dim module is unambiguous;
            // a dim button on a dim module is two shades of the same guess.
            g.setFont (juce::FontOptions (7.5f));
            g.drawText (off ? "Off" : "On", pr, juce::Justification::centred);
        }

        g.setColour (st == ModuleState::off ? kSub.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (moduleName (s.m), r.withTrimmedLeft (nameInset).withTrimmedTop (4.0f)
                                       .removeFromTop (12.0f),
                    juce::Justification::topLeft);

        // Say WHY it isn't sounding, instead of just greying it out.
        g.setColour (col);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (moduleStateText (st), r.reduced (8.0f, 4.0f).removeFromTop (12.0f),
                    juce::Justification::topRight);

        // The one-line description under the name, as in the prototype. Without it the rack
        // is eleven boxes of unlabelled dials and you have to turn one to find out what the
        // module is for.
        g.setColour (kSub.withAlpha (st == ModuleState::off ? 0.35f : 0.7f));
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (moduleSub (s.m), r.withTrimmedLeft (nameInset).withTrimmedTop (16.0f)
                                      .removeFromTop (10.0f),
                    juce::Justification::topLeft);

        // The LFO draws its own moving value along the bottom edge, so its rate and depth
        // are visible rather than implied. Below the dials, not across them.
        if (s.m == ModuleId::lfo)
        {
            auto strip = r.reduced (22.0f, 0.0f).removeFromBottom (10.0f);
            g.setColour (kLine);
            g.drawLine (strip.getX(), strip.getCentreY(), strip.getRight(), strip.getCentreY());
            g.setColour (kCV);
            const float x = strip.getCentreX() + lfoDot * strip.getWidth() * 0.45f;
            g.fillEllipse (x - 2.5f, strip.getCentreY() - 2.5f, 5.0f, 5.0f);
        }
    }

    // A dial on a module that can't be heard must not look live. It still WORKS (it's a
    // host parameter and automation still lands), but it shouldn't advertise an effect on
    // your sound that the patch can't deliver — law 4 applied to the rack.
    for (auto& d : dials)
    {
        const auto st = states[(int) d.owner];
        d.slider->setEnabled (st != ModuleState::off);
        d.slider->setAlpha (st == ModuleState::live ? 1.0f
                          : st == ModuleState::off  ? 0.35f : 0.6f);
    }

    // --- jacks ---
    for (const auto& j : jacks)
    {
        bool wired = false;
        for (const auto& c : cables)
            if (c.from == j.port || c.to == j.port) { wired = true; break; }

        const auto col = j.port.jack == Jack::cv ? kCV : kAccent;
        g.setColour (wired ? col : kSub.withAlpha (0.35f));
        g.fillEllipse (j.pos.x - jackR, j.pos.y - jackR, jackR * 2.0f, jackR * 2.0f);
        g.setColour (kWell);
        g.fillEllipse (j.pos.x - 2.0f, j.pos.y - 2.0f, 4.0f, 4.0f);
    }

    drawModuleScreens (g);

    // --- knob captions and values ---
    //
    // The kit pairs every rotary with a caption and a live value:
    //     label  600 10px Chakra Petch, letter-spacing 1.5px, #7d8b9a
    //     value  500 10px IBM Plex Mono, #2ec5ff at 0.8
    //
    // Both are new. The label existed only as a tooltip and the value only as a hover
    // popup, so a rack knob showed nothing at all until you pointed at it. That was
    // survivable while the knob drew a filled value arc; the kit's knob has no arc, so
    // without this the control would say less than it used to, not more.
    for (const auto& d : dials)
    {
        if (d.textArea.isEmpty() || d.slider == nullptr) continue;

        auto area = d.textArea;
        auto labelRow = area.removeFromTop (area.getHeight() / 2);

        g.setColour (Nebula2::Theme::knobLabel);
        g.setFont (Theme::ui (9.0f, 600));
        g.drawText (d.label.toUpperCase(), labelRow, juce::Justification::centred, false);

        g.setColour (Theme::accent.withAlpha (0.8f));
        g.setFont (Theme::mono (9.0f, 500));
        g.drawText (d.slider->getTextFromValue (d.slider->getValue()),
                    area, juce::Justification::centred, false);
    }

    // --- cables ---
    for (const auto& c : cables)
    {
        const auto a = posOf (c.from), b = posOf (c.to);
        juce::Path path;
        path.startNewSubPath (a);
        // Cables sag. It's not decoration — it's what makes two cables between the same
        // columns tellable apart at a glance.
        const float sag = juce::jmin (28.0f, std::abs (b.x - a.x) * 0.35f + 10.0f);
        path.cubicTo (a.x + 22.0f, a.y + sag, b.x - 22.0f, b.y + sag, b.x, b.y);

        g.setColour ((c.isCV() ? kCV : kAccent).withAlpha (0.75f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    // --- the cable being dragged ---
    if (dragging)
    {
        const auto a = posOf (dragFrom);
        juce::Path path;
        path.startNewSubPath (a);
        const float sag = juce::jmin (28.0f, std::abs (dragPos.x - a.x) * 0.35f + 10.0f);
        path.cubicTo (a.x + 22.0f, a.y + sag, dragPos.x - 22.0f, dragPos.y + sag, dragPos.x, dragPos.y);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    // --- the strip: what just happened, and whether the rack is in circuit at all ---
    auto strip = getLocalBounds().toFloat().removeFromBottom (16.0f).reduced (4.0f, 0.0f);
    g.setColour (message.isNotEmpty() ? juce::Colours::white.withAlpha (0.8f) : kSub);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (message.isNotEmpty() ? message
                                     : (live ? "Rack in circuit"
                                             : "Nothing patched to Main Out - the dry beat is playing"),
                strip, juce::Justification::centredLeft);
}
