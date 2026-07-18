#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace Nebula2
{
    // The prototype's step grid: rows of effects x steps, each cell 0..3.
    //
    // TWO LAWS FROM THE PROTOTYPE ARE BAKED INTO THIS DESIGN:
    //
    // 1. ONE WRITER PER PARAMETER. The prototype's most expensive bug: the grid scheduled
    //    automation on the same AudioParams the sliders assigned to, and once a param has
    //    an automation timeline a plain assignment is ignored FOREVER — so every Colour
    //    slider went dead the instant a grid row played. Here the grid NEVER writes to a
    //    parameter. It only computes a modulation which the DSP combines with the knob's
    //    value. The knob stays the single source of truth, so the bug can't exist.
    //
    // 2. A CELL IS A GATE, NOT A SECOND VOLUME. The prototype took the effect's strength
    //    from the cell alone, so Shatter at 0% still shattered. Here a cell interpolates
    //    from the effect's NEUTRAL toward YOUR panel amount: drive at 0% stays silent no
    //    matter how many cells you paint. The panel sets the ceiling; the cell scales it.
    // The prototype sequences SIXTEEN lanes. New rows are APPENDED, never reordered — the
    // serialised pattern is row-indexed, so inserting in the middle would silently reshuffle
    // every saved grid and every factory preset. Display order is a separate concern
    // (gridDisplayOrder), so the UI can group them the prototype's way regardless.
    enum class GridRow
    {
        Drive = 0, Crush, Squeeze, Tone, Width, Reverb, Delay,   // the original seven
        Pump, Resonate, PitchUp, PitchDown, Reverse, Stutter, Shatter, Gate, Haunt,
        Count
    };

    const char* gridRowName(GridRow r);

    // The PANEL parameter each lane scales — the knob that sets its ceiling. nullptr if a
    // lane has none.
    //
    // ONE definition, because two callers must never disagree: GridView reads it to find
    // the ceiling, and the test gate reads it to prove every displayed lane has a control
    // the user can actually reach. They existed as two separate switches for exactly one
    // commit, which was long enough to ship seven lanes whose "knob" was an automation
    // target and nothing else — paintable, lit, and permanently silent, because a lane
    // blends from its neutral toward a panel amount that defaulted to 0 and could not be
    // raised from inside the plugin.
    const char* gridRowPanelParamId(GridRow r);

    // Every parameter the EDITOR gives the user a control for. The gate below compares the
    // two lists, so a lane can never again be paintable without being reachable.
    //
    // What this proves: no displayed lane maps to a parameter that is absent here.
    // What it does NOT prove: that the editor really built the widget. Nothing links the
    // GUI into the test binary, so removing a knob from the editor while leaving its id
    // here would still pass. The extras below are built by ITERATING this list, so they
    // cannot drift; the older hand-placed knobs are the part taken on trust.
    const std::vector<const char*>& editorControlledParamIds();

    // The lane controls the editor lays out by looping the table (label + suffix included),
    // so adding a lane's knob is a one-line edit in one place.
    struct PanelControlSpec { const char* paramId; const char* label; const char* suffix; };
    const std::vector<PanelControlSpec>& extraColourControls();

    // How tall the grid panel must be. Lives here, not in GridView, for one reason: the
    // test binary links this file and not the GUI, and a panel height that silently stops
    // fitting its lanes is exactly the regression worth catching. It was hardcoded at 194px
    // when there were seven lanes; at sixteen each lane got under 8px and the 10pt names
    // collided into an unreadable smear.
    constexpr int gridLaneHeight   = 22;   // >= 16, or the label doesn't fit
    constexpr int gridNoticeHeight = 20;   // the bottom "why nothing is happening" strip

    int gridPanelHeight();

    // The lanes the UI actually SHOWS, in the prototype's grouping (Colour, then Space).
    //
    // This lists only lanes whose effect exists. A lane you can paint that drives nothing
    // is a dead control — the exact failure this project keeps hunting — so a row joins
    // this list the moment its DSP lands, and not before. The enum above is storage and is
    // append-only; this is presentation.
    const std::vector<GridRow>& gridDisplayOrder();

    // The value each effect sits at when a step is UNpainted (its "off" value). Tone and
    // Width are 100 (open/unchanged); everything else rests at 0.
    float gridRowNeutral(GridRow r);

    class FxGrid
    {
    public:
        static constexpr int numRows = (int) GridRow::Count;
        static constexpr int maxSteps = 32;

        // Which step is sounding, from the host's musical position. Pure + testable.
        static int stepAtPpq(double ppq, int numSteps, double beatsPerStep) noexcept;

        // neutral -> panelAmount, scaled by the cell (0..3). cell 0 = neutral (effect off
        // for this step), cell 3 = the full amount you dialled in.
        static float blend(float neutral, float panelAmount, int cell) noexcept;

        void setNumSteps(int n) noexcept;
        int getNumSteps() const noexcept { return numSteps; }

        void setCell(int row, int step, int level) noexcept;
        int getCell(int row, int step) const noexcept;
        void clearAll() noexcept;
        void clearRow(int row) noexcept;
        bool rowHasAnyCells(int row) const noexcept;

        // The effect amount for this row at this step, given the knob's value.
        float amountFor(GridRow row, float panelAmount, int step) const noexcept;

        // Serialised for the state chunk (structured data, not a flat parameter).
        juce::String toString() const;
        void fromString(const juce::String& s);

    private:
        std::array<std::array<uint8_t, (size_t) maxSteps>, (size_t) numRows> cells {};
        int numSteps = 16;
    };
}
