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

    // The rack's layout, as the user specified it - RAGGED rows, not a fixed 4x3 grid:
    //
    //   Main Out   Beat Out
    //   Parametric EQ
    //   Ladder     LFO       Comb
    //   Phaser     Chorus    Space Echo
    //   Wavefolder Vowel
    //
    // Each row splits the full width between however many modules it holds, so the EQ gets
    // the whole width to itself. That is not decoration: it is the only module with a
    // draggable response curve, and the curve needs the room.
    //
    // The grid was 4x3 with every cell the same size, which gave the EQ a quarter of a row
    // to draw five bands and a frequency axis in.
    struct Row { ModuleId m[4]; int n; };
    const Row rows[] =
    {
        { { ModuleId::out, ModuleId::src },                                     2 },
        { { ModuleId::eq },                                                     1 },
        { { ModuleId::flt, ModuleId::lfo, ModuleId::cmb },                      3 },
        { { ModuleId::phs, ModuleId::cho, ModuleId::ech },                      3 },
        { { ModuleId::fld, ModuleId::vow },                                     2 },
    };
    constexpr int numRows = (int) (sizeof (rows) / sizeof (rows[0]));

    // Every module in layout order. The loops that only need "each module" walk this rather
    // than each keeping its own copy of the row structure - the layout is stated once.
    std::vector<ModuleId> allModules()
    {
        std::vector<ModuleId> v;
        for (int ri = 0; ri < numRows; ++ri)
            for (int ci = 0; ci < rows[ri].n; ++ci)
                v.push_back (rows[ri].m[ci]);
        return v;
    }

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
    // A ROUND button, as the design draws it - not the little "On" text pill this had.
    // 26px, sitting in the module's top-left with the title beside it.
    if (! hasPower (m)) return {};
    const auto b = modBounds[(int) m];
    return { b.getX() + modPadX, b.getY() + 12.0f, powerD, powerD };
}

juce::Rectangle<float> RackView::diceRectFor (ModuleId m) const
{
    // Every effect module carries its own randomise button, top-right. The design has one
    // on each; this build had none, so per-module randomising was not reachable at all.
    if (! hasPower (m)) return {};
    const auto b = modBounds[(int) m];
    return { b.getRight() - modPadX - diceD, b.getY() + 12.0f, diceD, diceD };
}

float RackView::moduleHeightFor (ModuleId m) noexcept
{
    // headH covers the round power button and the title beside it; the jack row runs along
    // the bottom of every module that has one.
    float h = headH + modPadB + jackInset * 2.0f;

    if (m == ModuleId::eq) return h + eqCurveH;      // the curve IS its content
    if (hasScreen (m))     h += screenH + 6.0f;

    // Does it own any dials? Asked of the ONE dial table, so a module that gains a knob
    // gets taller automatically instead of squashing the row it sits in.
    for (const auto& d : rackDialDefs())
        if (d.owner == m) { h += (float) Nebula2::Theme::knobSize + capH + 6.0f; break; }

    return h;
}

int RackView::preferredHeight()
{
    float total = 0.0f;
    for (int ri = 0; ri < numRows; ++ri)
    {
        float rowMax = 0.0f;
        for (int ci = 0; ci < rows[ri].n; ++ci)
            rowMax = juce::jmax (rowMax, moduleHeightFor (rows[ri].m[ci]));
        total += rowMax + 8.0f;      // the gap between rows
    }
    return (int) total + 18;         // the status strip along the bottom
}

bool RackView::hasScreen (ModuleId m) noexcept
{
    // The five modules with something to draw. Everything that lays out a module asks THIS
    // rather than each guessing, because the screen and the dials were both claiming the
    // same strip: the LFO's sine, the Wavefolder's curve and the Vowel's formants all drew
    // straight over their own knobs.
    return m == ModuleId::src || m == ModuleId::out || m == ModuleId::lfo
        || m == ModuleId::fld || m == ModuleId::vow;
}

juce::Rectangle<float> RackView::screenAreaFor (ModuleId m) const
{
    if (! hasScreen (m)) return {};

    // A strip under the name and its subtitle. Height is fixed rather than a fraction of
    // the module, so the dial row below it starts at a predictable place.
    auto r = modBounds[(int) m].reduced (14.0f, 0.0f).withTrimmedTop (30.0f);
    return r.withHeight (juce::jmin (screenH, juce::jmax (12.0f, r.getHeight() * 0.42f)));
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
    const float pad = 4.0f;

    // Rows are sized to their TALLEST module, not to h/numRows. Equal rows gave the same
    // slice to a module with four knobs and to one with none, which is why the captions
    // ended up drawn over the knobs.
    float needed = 0.0f;
    float rowHeights[numRows] = {};
    for (int ri = 0; ri < numRows; ++ri)
    {
        for (int ci = 0; ci < rows[ri].n; ++ci)
            rowHeights[ri] = juce::jmax (rowHeights[ri], moduleHeightFor (rows[ri].m[ci]));
        needed += rowHeights[ri] + 8.0f;
    }

    // If the host has given us less than we need, shrink every row in proportion rather
    // than letting the last one fall off the bottom.
    const float scale = needed > 0.0f ? juce::jmin (1.0f, h / needed) : 1.0f;

    float y = 0.0f;
    for (int ri = 0; ri < numRows; ++ri)
    {
        const auto& row = rows[ri];
        const float cw = w / (float) juce::jmax (1, row.n);
        const float rowH = rowHeights[ri] * scale + 8.0f;

        for (int ci = 0; ci < row.n; ++ci)
        {
            const auto m = row.m[ci];
            juce::Rectangle<float> r ((float) ci * cw + pad, y + pad,
                                      cw - pad * 2.0f, rowH - pad * 2.0f);
            modBounds[(int) m] = r;

            // Jacks in the BOTTOM CORNERS, as the design draws them - in on the left, out on
            // the right, each with its label. They were unlabelled dots halfway up the side
            // edges, which is neither where the design puts them nor legible as a socket.
            const float jy = r.getBottom() - jackInset;
            if (hasJack (m, Jack::in))
                jacks.push_back ({ { m, Jack::in },  { r.getX() + jackInset, jy } });
            if (hasJack (m, Jack::cv))
                jacks.push_back ({ { m, Jack::cv },  { r.getX() + jackInset + 42.0f, jy } });
            if (hasJack (m, Jack::out))
                jacks.push_back ({ { m, Jack::out }, { r.getRight() - jackInset, jy } });
        }

        y += rowH;
    }

    // Dials sit in the module's middle, between the jack columns. Clicking a module body
    // toggles bypass, so the dials must not sit under that gesture — they're inset, and
    // mouseDown checks for a jack/dial before treating a click as a power toggle.
    for (const auto sm : allModules())
    {
        std::vector<Dial*> mine;
        for (auto& d : dials)
            if (d.owner == sm) mine.push_back (&d);
        if (mine.empty()) continue;

        // Start BELOW the screen when there is one. This was withTrimmedTop(16), which put
        // the dial row straight through the middle of the scope/curve/formant drawings.
        const float top = hasScreen (sm) ? (screenAreaFor (sm).getBottom() - modBounds[(int) sm].getY() + 6.0f)
                                         : headH;
        // Clear of the jack row along the bottom, or the knob captions would print over IN
        // and OUT.
        auto r = modBounds[(int) sm].reduced (modPadX, 0.0f).withTrimmedTop (top)
                                     .withTrimmedBottom (jackInset * 2.0f);
        const float w = r.getWidth() / (float) mine.size();
        for (size_t i = 0; i < mine.size(); ++i)
        {
            auto cell = r.withX (r.getX() + w * (float) i).withWidth (w).reduced (2.0f, 0.0f);

            // Reserve the bottom strip for the caption and value, as the kit does. The knob
            // takes what's left rather than the whole cell; drawing the text over the knob
            // is the only other way to fit it, and it is unreadable.
            auto cellI = cell.toNearestInt();

            // A FIXED strip, not height/3. Two 9pt lines need about 24px; height/3 in a
            // squeezed row came out around 12, so the caption and the value were drawn on
            // top of each other and over the knob - "LEVEL" through "100".
            const int textH = juce::jmin ((int) capH, juce::jmax (14, cellI.getHeight() / 2));
            mine[i]->textArea = cellI.removeFromBottom (textH);

            // The SHARED knob size, centred - not "whatever fits the cell". Rack knobs came
            // out smaller than Space knobs, which came out smaller than Colour knobs, purely
            // because each panel sized them from leftover room. Knobs that differ in size
            // read as differing in importance.
            const int s = juce::jmin (Nebula2::Theme::knobSize,
                                      juce::jmin (cellI.getWidth(), cellI.getHeight()));
            mine[i]->slider->setBounds (cellI.withSizeKeepingCentre (s, s));
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
    for (const auto sm : allModules())
        if (modBounds[(int) sm].contains (p)) return sm;
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

    // The dice: randomise THIS module's dials. Checked before the power toggle because it
    // sits inside the header strip, and a button that draws but does nothing is the exact
    // failure this project keeps shipping - it is wired in the same commit that draws it.
    if (hasPower (m) && diceRectFor (m).contains (p))
    {
        juce::Random rng;
        auto& vts = processorRef.getValueTreeState();
        for (const auto& d : rackDialDefs())
        {
            if (d.owner != m) continue;
            if (auto* prm = vts.getParameter (d.paramId))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (rng.nextFloat());
                prm->endChangeGesture();
            }
        }
        message = juce::String (moduleName (m)) + " randomised";
        messageTicks = 40;
        repaint();
        return;
    }

    if (hasPower (m))
    {
        const auto header = modBounds[(int) m].withHeight (headH);
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
    for (const auto sm : allModules())
    {
        const auto r = modBounds[(int) sm];
        const auto st = states[(int) sm];
        const auto col = colourForState (st);

        g.setColour (kPanel);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (col.withAlpha (st == ModuleState::live ? 0.7f : 0.25f));
        g.drawRoundedRectangle (r, 5.0f, st == ModuleState::live ? 1.4f : 0.8f);

        const bool off = st == ModuleState::off;

        // --- ROUND power button, top-left ---
        // The design draws a circle with a power glyph and a glowing ring, not a text pill.
        if (hasPower (sm))
        {
            const auto pr = powerRectFor (sm);
            const auto pc = pr.getCentre();
            const float pR = pr.getWidth() * 0.5f;

            if (! off)
            {
                g.setColour (kAccent.withAlpha (0.22f));
                g.fillEllipse (pr.expanded (3.0f));
            }

            g.setColour (Nebula2::Theme::well);
            g.fillEllipse (pr);
            g.setColour (off ? kSub.withAlpha (0.35f) : kAccent);
            g.drawEllipse (pr.reduced (0.5f), off ? 1.0f : 1.4f);

            // The glyph: a ring broken at the top with a stem through the gap.
            const float gR = pR * 0.44f;
            juce::Path glyph;
            glyph.addCentredArc (pc.x, pc.y + 1.0f, gR, gR, 0.0f,
                                 juce::degreesToRadians (35.0f),
                                 juce::degreesToRadians (325.0f), true);
            g.setColour (off ? kSub.withAlpha (0.5f) : Nebula2::Theme::accentLit);
            g.strokePath (glyph, juce::PathStrokeType (1.5f));
            g.drawLine (pc.x, pc.y - gR - 2.0f, pc.x, pc.y + 0.5f, 1.5f);
        }

        // --- dice, top-right ---
        if (hasPower (sm))
        {
            const auto dr = diceRectFor (sm);
            g.setColour (Nebula2::Theme::card3);
            g.fillRoundedRectangle (dr, 6.0f);
            g.setColour (kAccent.withAlpha (off ? 0.2f : 0.45f));
            g.drawRoundedRectangle (dr.reduced (0.5f), 6.0f, 1.0f);

            // Five pips - a die face reads instantly at this size where a glyph would not.
            g.setColour (off ? kSub.withAlpha (0.4f) : Nebula2::Theme::ink);
            const auto dc = dr.getCentre();
            const float o = dr.getWidth() * 0.22f, pr2 = 1.5f;
            for (auto pt : { juce::Point<float> (dc.x - o, dc.y - o), { dc.x + o, dc.y - o },
                             { dc.x, dc.y }, { dc.x - o, dc.y + o }, { dc.x + o, dc.y + o } })
                g.fillEllipse (pt.x - pr2, pt.y - pr2, pr2 * 2.0f, pr2 * 2.0f);
        }

        // --- title: CAPS, bold, beside the power button ---
        const float titleX = hasPower (sm) ? modPadX + powerD + 10.0f : modPadX;
        const float titleW = r.getWidth() - titleX - (hasPower (sm) ? diceD + modPadX + 90.0f : modPadX);

        g.setColour (off ? kSub.withAlpha (0.45f) : Nebula2::Theme::ink);
        g.setFont (Theme::ui (13.0f, 700).withExtraKerningFactor (0.06f));
        g.drawText (juce::String (moduleName (sm)).toUpperCase(),
                    juce::Rectangle<float> (r.getX() + titleX, r.getY() + 12.0f,
                                            juce::jmax (20.0f, titleW), powerD),
                    juce::Justification::centredLeft, false);

        // --- caption on the RIGHT, mono caps - not stacked under the title ---
        {
            const float capX = r.getRight() - modPadX - (hasPower (sm) ? diceD + 8.0f : 0.0f) - 96.0f;
            g.setColour (kSub.withAlpha (off ? 0.3f : 0.6f));
            g.setFont (Theme::mono (8.0f, 500).withExtraKerningFactor (0.08f));
            g.drawText (juce::String (moduleSub (sm)).toUpperCase(),
                        juce::Rectangle<float> (capX, r.getY() + 12.0f, 96.0f, powerD),
                        juce::Justification::centredRight, false);
        }

        // Say WHY it isn't sounding. Under the caption so it never collides with it.
        g.setColour (col);
        g.setFont (Theme::mono (7.5f, 500));
        g.drawText (moduleStateText (st),
                    juce::Rectangle<float> (r.getRight() - modPadX - 110.0f, r.getY() + 12.0f + powerD,
                                            110.0f, 11.0f),
                    juce::Justification::centredRight, false);

        // The LFO draws its own moving value along the bottom edge, so its rate and depth
        // are visible rather than implied. Below the dials, not across them.
        if (sm == ModuleId::lfo)
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

        // A wired jack glows, as in the design - the socket reads as connected before you
        // trace the cable.
        if (wired)
        {
            g.setColour (col.withAlpha (0.28f));
            g.fillEllipse (j.pos.x - jackR * 2.2f, j.pos.y - jackR * 2.2f,
                           jackR * 4.4f, jackR * 4.4f);
        }

        g.setColour (wired ? col : kSub.withAlpha (0.35f));
        g.fillEllipse (j.pos.x - jackR, j.pos.y - jackR, jackR * 2.0f, jackR * 2.0f);
        g.setColour (kWell);
        g.fillEllipse (j.pos.x - 2.0f, j.pos.y - 2.0f, 4.0f, 4.0f);

        // LABELLED. The design writes IN and OUT beside the sockets; unlabelled dots make
        // you learn which side is which by trying it.
        const char* lab = j.port.jack == Jack::in ? "IN"
                        : j.port.jack == Jack::out ? "OUT" : "CV";
        const bool onRight = j.port.jack == Jack::out;

        g.setColour (wired ? col.withAlpha (0.9f) : kSub.withAlpha (0.45f));
        g.setFont (Theme::mono (7.5f, 500).withExtraKerningFactor (0.1f));
        g.drawText (lab,
                    juce::Rectangle<float> (onRight ? j.pos.x - 44.0f : j.pos.x + 9.0f,
                                            j.pos.y - 6.0f, 35.0f, 12.0f),
                    onRight ? juce::Justification::centredRight : juce::Justification::centredLeft,
                    false);
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
