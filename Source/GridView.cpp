#include "GridView.h"
#include "ParameterIDs.h"

namespace
{
    const juce::Colour kWell   { 0xff05070d };
    const juce::Colour kCell   { 0xff1a2238 };
    const juce::Colour kAccent { 0xff3fe0d4 };
    const juce::Colour kSub    { 0xff9aa3bd };
    const juce::Colour kBeat   { 0xff242c55 };
}

GridView::GridView(Nebula2AudioProcessor& p) : processorRef(p)
{
    startTimerHz(30);
}

void GridView::timerCallback()
{
    const int s = processorRef.getCurrentGridStep();

    // Track the ON state so the "Grid is OFF" notice appears/vanishes the instant you tick
    // the box - when the grid is off there's no playhead moving to trigger a repaint.
    auto* on = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::gridOn);
    const bool nowOn = on != nullptr && on->load() > 0.5f;

    if (s != lastStep || nowOn != lastOn) { lastStep = s; lastOn = nowOn; repaint(); }
}

float GridView::panelAmountFor(int row) const
{
    auto& v = processorRef.getValueTreeState();
    // The mapping lives in FxGrid, not here - see gridRowPanelParamId(). This was a second
    // copy of that switch, and a lane can only be proved reachable against one list.
    const char* id = Nebula2::gridRowPanelParamId((Nebula2::GridRow) row);
    if (id == nullptr) return 0.0f;
    auto* raw = v.getRawParameterValue(id);
    return raw != nullptr ? raw->load() : 0.0f;
}

bool GridView::rowIsStarved(int row) const
{
    // The rule lives in FxGrid so it can be tested - see gridRowIsAtRest(). A lane is
    // starved when its knob sits ON its neutral, whatever that neutral happens to be.
    return Nebula2::gridRowIsAtRest((Nebula2::GridRow) row, panelAmountFor(row));
}

int GridView::laneHeight() const
{
    // ONE definition, used by both paint() and rowAt(). They each had their own copy of
    // this arithmetic; the moment the two disagree, cells light up under the cursor and
    // the click edits a different lane.
    const int n = (int) Nebula2::gridDisplayOrder().size();
    if (n <= 0) return 0;
    // The note strip and the notice strip both come off the top/bottom before the lanes
    // get their share - miss either and the lanes drift out of step with the hit test.
    return juce::jmax(1, (getHeight() - noteRowHeight - Nebula2::gridNoticeHeight) / n);
}

int GridView::rowAt(juce::Point<int> pos) const
{
    // The view lists only IMPLEMENTED lanes (gridDisplayOrder), so a click maps from the
    // visual index back to the lane's storage row.
    const auto& order = Nebula2::gridDisplayOrder();
    const int n = (int) order.size();
    const int h = laneHeight();
    if (h <= 0) return -1;
    // Clicks in the note strip are not lane clicks.
    if (pos.y < noteRowHeight) return -1;
    const int i = (pos.y - noteRowHeight) / h;
    return (i >= 0 && i < n) ? (int) order[(size_t) i] : -1;
}

int GridView::stepAt(juce::Point<int> pos) const
{
    const int steps = processorRef.getGrid().getNumSteps();
    const int w = getWidth() - labelW;
    if (w <= 0 || steps <= 0 || pos.x < labelW) return -1;
    const int s = ((pos.x - labelW) * steps) / w;
    return (s >= 0 && s < steps) ? s : -1;
}

void GridView::mouseDown(const juce::MouseEvent& e)
{
    // The note strip first: it owns the top of the view, and a click there is a pitch, not
    // a cell. Right-click clears the step, matching the effect lanes' erase.
    if (e.getPosition().y < noteRowHeight)
    {
        const int step = stepAt(e.getPosition());
        if (step < 0) return;
        if (e.mods.isRightButtonDown()) { processorRef.getGrid().setNote(step, 0); repaint(); }
        else { editingNotes = true; setNoteFromMouse(e.getPosition()); }
        return;
    }

    const int row = rowAt(e.getPosition());
    const int step = stepAt(e.getPosition());
    if (row < 0 || step < 0) return;

    auto& g = processorRef.getGrid();
    if (e.mods.isRightButtonDown()) { paintLevel = 0; }
    else
    {
        // Cycle 0 -> 3 -> 2 -> 1 -> 0: full first, since that's the usual intent.
        const int cur = g.getCell(row, step);
        paintLevel = (cur == 0) ? 3 : cur - 1;
    }
    g.setCell(row, step, paintLevel);
    repaint();
}

void GridView::mouseDrag(const juce::MouseEvent& e)
{
    // Drawing a melody: keep editing notes for the whole drag, even once the pointer
    // wanders below the strip. Re-testing the row each time would drop you into painting
    // effect cells mid-gesture.
    if (editingNotes) { setNoteFromMouse(e.getPosition()); return; }

    const int row = rowAt(e.getPosition());
    const int step = stepAt(e.getPosition());
    if (row < 0 || step < 0) return;
    processorRef.getGrid().setCell(row, step, paintLevel);   // drag paints at one level
    repaint();
}

void GridView::paint(juce::Graphics& g)
{
    g.fillAll(kWell);

    auto& grid = processorRef.getGrid();
    const auto& order = Nebula2::gridDisplayOrder();
    const int steps = grid.getNumSteps();
    const int rows  = (int) order.size();
    const int rowH  = laneHeight();
    const int gridW = getWidth() - labelW;
    if (steps <= 0 || gridW <= 0 || rows <= 0) return;

    const int playing = processorRef.getCurrentGridStep();

    drawNoteRow(g, grid, steps, gridW, playing);

    for (int i = 0; i < rows; ++i)
    {
        const int r = (int) order[(size_t) i];    // storage row for this visual lane
        const int y = noteRowHeight + i * rowH;
        const bool starved = rowIsStarved(r);

        // Row label. A starved row says so - it cannot sound, so don't pretend.
        g.setColour(starved ? kSub.withAlpha(0.4f) : kSub);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(Nebula2::gridRowName((Nebula2::GridRow) r), 4, y, labelW - 28, rowH,
                   juce::Justification::centredLeft);
        if (starved)
        {
            // Show the lane's ACTUAL setting, not a hardcoded "0%". Tone and Width rest at
            // 100, so the old fixed text would have called a lane sitting at 100 a zero the
            // moment they stopped being exempt from this tag.
            g.setColour(juce::Colour(0xffff6a4d).withAlpha(0.85f));
            g.setFont(juce::FontOptions(8.5f));
            g.drawText(juce::String(juce::roundToInt(panelAmountFor(r))) + "%",
                       labelW - 26, y, 24, rowH, juce::Justification::centredRight);
        }

        for (int s = 0; s < steps; ++s)
        {
            const int x0 = labelW + (s * gridW) / steps;
            const int x1 = labelW + ((s + 1) * gridW) / steps;
            juce::Rectangle<int> cell(x0 + 1, y + 1, juce::jmax(1, x1 - x0 - 2), rowH - 2);

            const int level = grid.getCell(r, s);
            const bool onBeat = (s % 4) == 0;

            g.setColour(onBeat ? kBeat : kCell);
            g.fillRect(cell);

            if (level > 0)
            {
                // Painted cells on a starved row render dim - visible, but visibly unable
                // to sound. You can still paint; it just tells you the truth.
                const float a = (float) level / 3.0f;
                g.setColour(starved ? kAccent.withAlpha(0.18f * a) : kAccent.withAlpha(0.30f + 0.7f * a));
                g.fillRect(cell.reduced(1));
            }

            if (s == playing)
            {
                g.setColour(juce::Colours::white.withAlpha(0.28f));
                g.fillRect(cell);
            }
        }
    }

    // SAY WHY NOTHING IS HAPPENING. A painted grid with "Grid On" unchecked looks exactly
    // like a working one - the user hit this repeatedly. The prototype is blunt about it
    // ("pad is off - the sliders own the sound") and so is this now.
    auto* on = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::gridOn);
    const bool gridOn = on != nullptr && on->load() > 0.5f;
    if (! gridOn)
    {
        auto strip = getLocalBounds().removeFromBottom(18);
        g.setColour(kWell.withAlpha(0.92f));
        g.fillRect(strip);
        g.setColour(juce::Colour(0xffff6a4d));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("Grid is OFF - painted steps won't sound. Tick \"Grid On\".",
                   strip, juce::Justification::centred);
    }
    else
    {
        // Grid is on, but a lane at 0% still can't act. Count them and say so.
        int starvedCount = 0, painted = 0;
        for (auto r : order)
        {
            if (! grid.rowHasAnyCells((int) r)) continue;
            ++painted;
            if (rowIsStarved((int) r)) ++starvedCount;
        }
        if (painted > 0 && starvedCount == painted)
        {
            auto strip = getLocalBounds().removeFromBottom(18);
            g.setColour(kWell.withAlpha(0.92f));
            g.fillRect(strip);
            g.setColour(juce::Colour(0xffff6a4d));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText("Every painted lane is at 0% - turn its knob up so the steps have something to gate.",
                       strip, juce::Justification::centred);
        }
    }
}

void GridView::drawNoteRow(juce::Graphics& g, Nebula2::FxGrid& grid,
                           int steps, int gridW, int playing)
{
    const int h = noteRowHeight;

    g.setColour(kSub);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Note", 4, 0, labelW - 6, h, juce::Justification::centredLeft);

    // The key/scale this lane is quantised to - the Resonate controls, which the instrument
    // already owns. Showing it here is the difference between "why did my note move?" and
    // "of course, we're in C minor".
    auto& vts = processorRef.getValueTreeState();
    auto* keyP   = vts.getRawParameterValue(Nebula2::ParamID::resoKey);
    auto* scaleP = vts.getRawParameterValue(Nebula2::ParamID::resoScale);
    const int key = keyP != nullptr ? (int) keyP->load() : 0;
    const auto scale = (Nebula2::ResoScale) juce::jlimit(
        0, (int) Nebula2::ResoScale::Count - 1, scaleP != nullptr ? (int) scaleP->load() : 0);

    for (int s = 0; s < steps; ++s)
    {
        const int x0 = labelW + (s * gridW) / steps;
        const int x1 = labelW + ((s + 1) * gridW) / steps;
        juce::Rectangle<int> cell(x0 + 1, 1, juce::jmax(1, x1 - x0 - 2), h - 2);

        g.setColour((s % 4) == 0 ? kBeat : kCell);
        g.fillRect(cell);

        const int semis = grid.getNote(s);
        if (semis != 0)
        {
            // A bar whose height reads the pitch, drawn from the middle so up is up and
            // down is down - a number alone makes a melody impossible to read at a glance.
            const float mid = (float) h * 0.5f;
            const float frac = (float) semis / (float) Nebula2::noteLaneRange;
            const float top = semis > 0 ? mid - frac * mid * 0.8f : mid;
            const float bot = semis > 0 ? mid : mid - frac * mid * 0.8f;
            g.setColour(kAccent.withAlpha(0.75f));
            g.fillRect((float) cell.getX(), top, (float) cell.getWidth(), juce::jmax(1.5f, bot - top));

            if (cell.getWidth() >= 18)
            {
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.setFont(juce::FontOptions(8.5f));
                g.drawText(Nebula2::noteLaneName(semis, key), cell, juce::Justification::centred);
            }
        }

        if (s == playing)
        {
            g.setColour(juce::Colours::white.withAlpha(0.28f));
            g.fillRect(cell);
        }
    }

    // The key, right-aligned in the label column's spare width.
    g.setColour(kAccent.withAlpha(0.7f));
    g.setFont(juce::FontOptions(8.0f));
    g.drawText(Nebula2::noteLaneName(0, key) + " " + Nebula2::resoScaleName(scale),
               4, h / 2, labelW - 8, h / 2 - 1, juce::Justification::bottomLeft);
}

void GridView::setNoteFromMouse(juce::Point<int> pos)
{
    const int step = stepAt(pos);
    if (step < 0) return;

    auto& grid = processorRef.getGrid();

    // Vertical position picks the pitch; it's then SNAPPED to the key/scale, so you can
    // drag freely and still land in the key rather than having to hit exact pixels.
    const float mid = (float) noteRowHeight * 0.5f;
    const float frac = (mid - (float) pos.y) / (mid * 0.8f);
    const int raw = juce::roundToInt(frac * (float) Nebula2::noteLaneRange);

    auto* scaleP = processorRef.getValueTreeState().getRawParameterValue(Nebula2::ParamID::resoScale);
    const auto scale = (Nebula2::ResoScale) juce::jlimit(
        0, (int) Nebula2::ResoScale::Count - 1, scaleP != nullptr ? (int) scaleP->load() : 0);

    grid.setNote(step, Nebula2::quantiseToScale(raw, scale));
    repaint();
}

void GridView::mouseUp(const juce::MouseEvent&)
{
    editingNotes = false;
}
