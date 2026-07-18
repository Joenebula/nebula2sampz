#include "RackView.h"
#include "ParameterIDs.h"
#include "Theme.h"

using namespace Nebula2;

namespace
{
    const juce::Colour kWell   { 0xff05070d };
    const juce::Colour kPanel  { 0xff0d1220 };
    const juce::Colour kAccent { 0xff3fe0d4 };
    const juce::Colour kCV     { 0xffffd166 };
    const juce::Colour kDang   { 0xffff6b6b };
    const juce::Colour kSub    { 0xff9aa3bd };
    const juce::Colour kLine   { 0x22ffffff };

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

RackView::RackView(Nebula2AudioProcessor& p) : processorRef(p)
{
    startTimerHz(24);
    buildModuleDials(p.getValueTreeState());
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
    // Two or three dials per module: the ones you actually reach for while patching. The
    // rest remain full host parameters — nothing here is a control the DAW can't see.
    addDial(apvts, ModuleId::flt, ParamID::fltCut,   "Cut");
    addDial(apvts, ModuleId::flt, ParamID::fltRes,   "Res");

    addDial(apvts, ModuleId::lfo, ParamID::lfoRate,  "Rate");
    addDial(apvts, ModuleId::lfo, ParamID::lfoDepth, "Depth");

    addDial(apvts, ModuleId::phs, ParamID::phsRate,  "Rate");
    addDial(apvts, ModuleId::phs, ParamID::phsFb,    "Fb");
    addDial(apvts, ModuleId::phs, ParamID::phsMix,   "Mix");

    addDial(apvts, ModuleId::cho, ParamID::choRate,  "Rate");
    addDial(apvts, ModuleId::cho, ParamID::choDepth, "Depth");
    addDial(apvts, ModuleId::cho, ParamID::choMix,   "Mix");

    addDial(apvts, ModuleId::cmb, ParamID::cmbTune,  "Tune");
    addDial(apvts, ModuleId::cmb, ParamID::cmbFb,    "Fb");
    addDial(apvts, ModuleId::cmb, ParamID::cmbMix,   "Mix");

    addDial(apvts, ModuleId::fld, ParamID::fldDrive, "Drive");
    addDial(apvts, ModuleId::fld, ParamID::fldSym,   "Sym");
    addDial(apvts, ModuleId::fld, ParamID::fldMix,   "Mix");

    addDial(apvts, ModuleId::vow, ParamID::vowMorph, "Vowel");
    addDial(apvts, ModuleId::vow, ParamID::vowSharp, "Sharp");
    addDial(apvts, ModuleId::vow, ParamID::vowMix,   "Mix");

    addDial(apvts, ModuleId::ech, ParamID::echTime,  "Time");
    addDial(apvts, ModuleId::ech, ParamID::echFb,    "Fb");
    addDial(apvts, ModuleId::ech, ParamID::echMix,   "Mix");

    addDial(apvts, ModuleId::eq,  ParamID::eqGain1,  "110");
    addDial(apvts, ModuleId::eq,  ParamID::eqGain3,  "1.6k");
    addDial(apvts, ModuleId::eq,  ParamID::eqGain5,  "9k");

    addDial(apvts, ModuleId::out, ParamID::outLvl,   "Level");
}

void RackView::timerCallback()
{
    if (messageTicks > 0 && --messageTicks == 0) message.clear();

    const float v = processorRef.getRackLfoValue();
    if (std::abs(v - lfoDot) > 0.005f) { lfoDot = v; repaint(); return; }
    repaint();      // the state flags follow the graph, which the editor can change
}

void RackView::resized() { rebuildLayout(); }

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
            mine[i]->slider->setBounds (cell.toNearestInt());
        }
    }
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

    // Clicking a module's NAME toggles its power. Not the whole body: the body now holds
    // dials, and a stray click that silently bypassed a module would be a nasty surprise
    // mid-take.
    const auto m = moduleAt (p);
    if (m != ModuleId::count && m != ModuleId::src && m != ModuleId::out)
    {
        const auto header = modBounds[(int) m].withHeight (16.0f);
        if (! header.contains (p)) return;

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

        g.setColour (st == ModuleState::off ? kSub.withAlpha (0.45f) : juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (moduleName (s.m), r.reduced (8.0f, 4.0f).removeFromTop (12.0f),
                    juce::Justification::topLeft);

        // Say WHY it isn't sounding, instead of just greying it out.
        g.setColour (col);
        g.setFont (juce::FontOptions (8.0f));
        g.drawText (moduleStateText (st), r.reduced (8.0f, 4.0f).removeFromTop (12.0f),
                    juce::Justification::topRight);

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
