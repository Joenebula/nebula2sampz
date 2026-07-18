#include "History.h"

namespace Nebula2
{
    void History::push(const Snapshot& before)
    {
        // A no-op action shouldn't cost an undo step. Without this, pressing Clear on an
        // already-empty grid would stack identical snapshots, and undo would appear to do
        // nothing however many times you pressed it.
        if (! undoStack.empty() && undoStack.back() == before) return;

        undoStack.push_back(before);

        // Oldest goes first when we hit the cap.
        if ((int) undoStack.size() > maxDepth)
            undoStack.erase(undoStack.begin());

        // A NEW action invalidates the redo branch — you can't redo forward into a future
        // that no longer follows from the present.
        redoStack.clear();
    }

    bool History::undo(const Snapshot& current, Snapshot& out)
    {
        if (undoStack.empty()) return false;
        out = undoStack.back();
        undoStack.pop_back();
        redoStack.push_back(current);
        return true;
    }

    bool History::redo(const Snapshot& current, Snapshot& out)
    {
        if (redoStack.empty()) return false;
        out = redoStack.back();
        redoStack.pop_back();
        undoStack.push_back(current);
        return true;
    }

    void History::clear()
    {
        undoStack.clear();
        redoStack.clear();
    }
}
