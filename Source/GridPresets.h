#pragma once

#include <juce_core/juce_core.h>
#include "dsp/FxGrid.h"

namespace Nebula2
{
    // Grid patterns, in two flavours.
    //
    // BUILT-IN: the prototype's factory patterns, generated from a rule over (row, step).
    // Recall only — they're code, not files.
    //
    // USER: patterns you save yourself. These live as FILES ON DISK rather than inside the
    // session, so a pattern built in one project can be recalled in another. That was the
    // explicit ask ("save the grid separately... so saved grid settings can be reused"), and
    // it's the one decision here that's hard to reverse later: patterns saved into a session
    // would be invisible to every other project.

    // --- built-in patterns ---

    const juce::StringArray& builtInGridPatternNames();

    // Fills `grid` with the named pattern. False if the name isn't one of ours (in which
    // case the grid is left completely alone, not half-written).
    bool applyBuiltInGridPattern(const juce::String& name, FxGrid& grid);

    // --- user patterns on disk ---

    // ~/Documents/Nebula2/Grid Patterns (or the OS equivalent). Created on demand.
    juce::File gridPresetDirectory();

    // A pattern name has to survive becoming a filename. Strips path separators and the
    // characters Windows rejects, collapses whitespace, and caps the length — so a name
    // like "1/2 time: *hard*" can't escape the preset folder or produce a file that fails
    // to write. Returns an empty string if nothing usable is left.
    juce::String sanitiseGridPresetName(const juce::String& name);

    // Saves under `name` (overwrites an existing one of the same name). False if the name
    // sanitises to nothing or the write fails.
    bool saveGridPreset(const juce::String& name, const FxGrid& grid);

    // Loads into `grid`. False if it isn't there or the file is unreadable — and on false
    // the grid is untouched, so a bad load can't silently wipe what you had.
    bool loadGridPreset(const juce::String& name, FxGrid& grid);

    bool deleteGridPreset(const juce::String& name);

    // Saved pattern names, sorted, without extensions.
    juce::StringArray listGridPresets();

    // The file a name maps to. Exposed so tests can point at a scratch directory rather
    // than writing into the real preset folder.
    juce::File gridPresetFileFor(const juce::String& name);

    // Tests only: redirect the preset folder. Passing an invalid File restores the default.
    void setGridPresetDirectoryForTesting(const juce::File& dir);
}
