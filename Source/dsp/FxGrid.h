#pragma once

#include <juce_core/juce_core.h>
#include "Resonator.h"   // ResoScale: the note lane quantises to the resonator's key/scale
#include <cmath>
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

    // Can this lane do anything at this panel setting? A cell blends from the lane's
    // NEUTRAL toward the panel amount, so when the two are equal every cell level lands on
    // the same value and painting is a no-op — whatever that number happens to be.
    //
    // This was "amount <= 0, except Tone and Width, which are never at rest". That exempted
    // exactly the two lanes whose neutral ISN'T zero — so on a fresh Init, where Tone and
    // Width sit at 100 (their own neutral), they rendered live and paintable while every
    // equally-inert lane was greyed and tagged. Identical condition, opposite answer.
    inline bool gridRowIsAtRest(GridRow r, float panelAmount) noexcept
    {
        return std::abs(panelAmount - gridRowNeutral(r)) < 0.05f;
    }

    // --- the NOTE lane ---
    //
    // A melodic lane over the same steps: each step carries a transpose in semitones, and
    // the chop landing there plays at that pitch. Unlike the effect lanes this isn't a
    // 0..3 level, so it lives beside the grid rather than in it.
    //
    // Pitches are QUANTISED to the Resonate key and scale, which the instrument already
    // owns — one source of truth for "what key is this in", rather than a second key
    // selector that could disagree with the resonator bank ringing underneath it.
    constexpr int noteLaneRange = 12;   // +/- one octave, the prototype's range

    // Snap a semitone offset to the nearest degree of `scale`. Searches outward from the
    // requested pitch, preferring DOWN on a tie — the prototype's order, and the one that
    // keeps a line from drifting sharp as you drag.
    int quantiseToScale(int semitones, ResoScale scale) noexcept;

    // Note name for a lane value, given the key. Root is A (see ParamID::resoKey — index 0
    // is A because the resonator's root is A1), so a lane value of 0 in key C reads "C".
    juce::String noteLaneName(int semitones, int keySemis);

    // Steps per beat. A CONSTANT, not numSteps/4: the sequencer clocks a step every 0.25
    // beats (see the stepAtPpq call site), so a step is always a 1/16 and a beat is always
    // four of them. Changing the step COUNT changes how many bars the pattern spans, not
    // how fast it runs — 32 steps is two bars, not a bar of 32nds.
    //
    // Both the dice and the factory patterns derived this as numSteps/4, which is only
    // right at 16. At 32 it put "on the beat" every half bar, and any pattern keyed to bar
    // boundaries (q*4) could never reach the second bar, so it painted nothing at all.
    constexpr int gridStepsPerBeat = 4;

    // How busy the dice should be. Three settings, because "more random" is not a thing you
    // can ask for — what actually varies is how many lanes join in and how often they fire.
    enum class RandomDensity { Low = 0, Mid, High, Count };

    const char* randomDensityName(RandomDensity d) noexcept;

    class FxGrid;

    // Roll a pattern.
    //
    // `eligible` is the lanes that can actually sound. The caller filters that (the panel
    // amounts live in the APVTS, which this file knows nothing about) — and it matters:
    // the prototype's dice used to fill EVERY lane, which is mush rather than a beat, and
    // casting a lane sitting at its neutral wastes a slot on something inaudible.
    //
    // Takes the RNG by reference so a test can seed it and get the same pattern twice.
    // Never returns an empty grid when anything is eligible: a dice that visibly does
    // nothing reads as broken.
    // Which lanes this roll should use. Split out from randomiseGrid because the caller now
    // has to switch these lanes ON (set their levels) BEFORE painting them - the dice used
    // to refuse when every lane was at rest, which on a fresh Init was every time.
    std::vector<GridRow> pickRandomLanes(const std::vector<GridRow>& from,
                                         RandomDensity density, juce::Random& rng);

    void randomiseGrid(FxGrid& grid, const std::vector<GridRow>& eligible,
                       RandomDensity density, juce::Random& rng);

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

        // --- the note lane ---
        // Semitone transpose per step, 0 = leave the chop alone. Separate from `cells`
        // because it is a value, not a 0..3 level, and separate from the row storage so
        // adding it cannot shift any existing lane index.
        void setNote(int step, int semitones) noexcept;
        int getNote(int step) const noexcept;
        void clearNotes() noexcept;
        bool anyNotes() const noexcept;

        juce::String notesToString() const;
        void notesFromString(const juce::String& s);

        // Serialised for the state chunk (structured data, not a flat parameter).
        juce::String toString() const;
        void fromString(const juce::String& s);

    private:
        std::array<std::array<uint8_t, (size_t) maxSteps>, (size_t) numRows> cells {};
        std::array<int8_t, (size_t) maxSteps> notes {};
        int numSteps = 16;
    };
}
