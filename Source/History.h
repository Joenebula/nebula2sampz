#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace Nebula2
{
    // Undo for the state that ISN'T parameters.
    //
    // The APVTS has its own UndoManager, so knobs are already covered. The grid pattern,
    // the slice order, the per-slice settings, the rack patch and the morph scenes all live
    // outside it — and those are precisely what the twelve dice rewrite in one click. Rolling
    // a grid you liked and losing it with no way back is the kind of thing that makes people
    // stop pressing the interesting button.
    //
    // Snapshots, not deltas: every piece of this state already serialises to a string for
    // the project file, so a snapshot is free and cannot get out of step with a
    // hand-written inverse operation. The strings are small (a grid is ~500 bytes).
    struct Snapshot
    {
        juce::String grid, sliceOrder, sliceFx, rack, scenes;

        bool operator== (const Snapshot& o) const noexcept
        {
            return grid == o.grid && sliceOrder == o.sliceOrder && sliceFx == o.sliceFx
                && rack == o.rack && scenes == o.scenes;
        }
    };

    class History
    {
    public:
        // How many steps back you can go. 64 snapshots of a few hundred bytes is nothing,
        // and running out of undo is far more annoying than the memory is expensive.
        static constexpr int maxDepth = 64;

        // Call BEFORE mutating. Identical consecutive snapshots are dropped, so a button
        // that turns out to change nothing doesn't cost an undo step — otherwise you'd
        // press undo and appear to get nothing, twice.
        void push(const Snapshot& before);

        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }

        // `current` is the state as it is NOW, so it can be pushed to the other stack.
        // Returns false and changes nothing when there's nothing to go back to.
        bool undo(const Snapshot& current, Snapshot& out);
        bool redo(const Snapshot& current, Snapshot& out);

        void clear();

        int undoDepth() const noexcept { return (int) undoStack.size(); }
        int redoDepth() const noexcept { return (int) redoStack.size(); }

    private:
        std::vector<Snapshot> undoStack, redoStack;
    };
}
