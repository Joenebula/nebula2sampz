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
    if (s != lastStep) { lastStep = s; repaint(); }
}

float GridView::panelAmountFor(int row) const
{
    auto& v = processorRef.getValueTreeState();
    const char* id = nullptr;
    switch ((Nebula2::GridRow) row)
    {
        case Nebula2::GridRow::Drive:   id = Nebula2::ParamID::drive;   break;
        case Nebula2::GridRow::Crush:   id = Nebula2::ParamID::crush;   break;
        case Nebula2::GridRow::Squeeze: id = Nebula2::ParamID::squeeze; break;
        case Nebula2::GridRow::Tone:    id = Nebula2::ParamID::tone;    break;
        case Nebula2::GridRow::Width:   id = Nebula2::ParamID::width;   break;
        case Nebula2::GridRow::Reverb:  id = Nebula2::ParamID::revMix;  break;
        case Nebula2::GridRow::Delay:   id = Nebula2::ParamID::dlyMix;  break;
        case Nebula2::GridRow::Pump:    id = Nebula2::ParamID::pump;    break;
        case Nebula2::GridRow::Gate:    id = Nebula2::ParamID::gate;    break;
        case Nebula2::GridRow::Haunt:   id = Nebula2::ParamID::haunt;   break;
        default: return 0.0f;
    }
    auto* raw = v.getRawParameterValue(id);
    return raw != nullptr ? raw->load() : 0.0f;
}

bool GridView::rowIsStarved(int row) const
{
    // Tone/Width rest at 100 and act by moving AWAY from it, so they're never starved.
    const auto r = (Nebula2::GridRow) row;
    if (r == Nebula2::GridRow::Tone || r == Nebula2::GridRow::Width) return false;
    return panelAmountFor(row) <= 0.0f;
}

int GridView::rowAt(juce::Point<int> pos) const
{
    // The view lists only IMPLEMENTED lanes (gridDisplayOrder), so a click maps from the
    // visual index back to the lane's storage row.
    const auto& order = Nebula2::gridDisplayOrder();
    const int n = (int) order.size();
    const int h = n > 0 ? getHeight() / n : 0;
    if (h <= 0) return -1;
    const int i = pos.y / h;
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
    const int rowH  = juce::jmax(1, getHeight() / juce::jmax(1, rows));
    const int gridW = getWidth() - labelW;
    if (steps <= 0 || gridW <= 0 || rows <= 0) return;

    const int playing = processorRef.getCurrentGridStep();

    for (int i = 0; i < rows; ++i)
    {
        const int r = (int) order[(size_t) i];    // storage row for this visual lane
        const int y = i * rowH;
        const bool starved = rowIsStarved(r);

        // Row label. A starved row says so — it cannot sound, so don't pretend.
        g.setColour(starved ? kSub.withAlpha(0.4f) : kSub);
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(Nebula2::gridRowName((Nebula2::GridRow) r), 4, y, labelW - 22, rowH,
                   juce::Justification::centredLeft);
        if (starved)
        {
            g.setColour(juce::Colour(0xffff6a4d).withAlpha(0.85f));
            g.setFont(juce::FontOptions(8.5f));
            g.drawText("0%", labelW - 20, y, 18, rowH, juce::Justification::centredRight);
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
                // Painted cells on a starved row render dim — visible, but visibly unable
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
}
