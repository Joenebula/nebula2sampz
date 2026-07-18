#include "GridPresets.h"

namespace Nebula2
{
    namespace
    {
        // A pattern rule: given a lane, a step, steps-per-beat and the pattern length,
        // return the cell level 0..3. Straight port of the prototype's GRID_PRESETS.
        using PatternFn = int (*)(GridRow r, int c, int q, int n);

        struct NamedPattern { const char* name; PatternFn fn; };

        // Shorthand so the rules below read like the prototype's one-liners.
        inline int lvl(bool on, int level) { return on ? level : 0; }

        const NamedPattern kPatterns[] =
        {
            { "Every 4",         [](GridRow r, int c, int q, int)    { return lvl((c % q) == 0 && (r == GridRow::Drive || r == GridRow::Tone), 3); } },
            { "Off-beats",       [](GridRow r, int c, int q, int)    { return lvl((c % q) == q / 2 && (r == GridRow::Stutter || r == GridRow::Reverb), 2); } },
            { "Build-up",        [](GridRow r, int c, int q, int n)  { return r == GridRow::Tone ? juce::jmin(3, 1 + (c * 3) / juce::jmax(1, n))
                                                                                                 : lvl(r == GridRow::Stutter && c > (n * 3) / 4, 3); } },

            { "Long Sweep",      [](GridRow r, int, int, int)        { return lvl(r == GridRow::Tone, 3); } },
            { "Half Sweep",      [](GridRow r, int c, int, int n)    { return lvl(r == GridRow::Tone && c < n / 2, 3); } },
            { "Breathe",         [](GridRow r, int, int, int)        { return lvl(r == GridRow::Pump, 3); } },
            { "Drive Swell",     [](GridRow r, int c, int, int n)    { return lvl(r == GridRow::Drive && c >= n / 2, 3); } },
            { "Crush Bars",      [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Crush && ((c / juce::jmax(1, q * 4)) % 2) == 1, 3); } },
            { "Squeeze Half",    [](GridRow r, int c, int, int n)    { return lvl(r == GridRow::Squeeze && c >= n / 2, 3); } },
            { "Widen Out",       [](GridRow r, int c, int, int n)    { return lvl(r == GridRow::Width && c >= (n * 3) / 4, 3); } },

            { "Ring Out",        [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Resonate && (c % juce::jmax(1, q * 4)) == 0, 3); } },
            { "Resonant Tail",   [](GridRow r, int c, int q, int n)  { return lvl(r == GridRow::Resonate && c >= n - q * 2, 3); } },

            { "Backbeat Verb",   [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Reverb && (c % juce::jmax(1, q * 2)) == q, 3); } },
            { "Snare Delay",     [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Delay && (c % juce::jmax(1, q * 4)) == q * 2, 3); } },
            { "Dub Throws",      [](GridRow r, int c, int q, int)    { return (r == GridRow::Delay  && (c % juce::jmax(1, q * 8)) == q * 7) ? 3
                                                                           : lvl(r == GridRow::Reverb && (c % juce::jmax(1, q * 8)) == q * 3, 2); } },
            { "Haunt Swell",     [](GridRow r, int c, int, int n)    { return lvl(r == GridRow::Haunt && c >= n / 2, 2); } },

            { "Stutter Ends",    [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Stutter && (c % juce::jmax(1, q * 4)) == q * 4 - 1, 3); } },
            { "Rolling Fills",   [](GridRow r, int c, int q, int n)  { return lvl(r == GridRow::Stutter && c >= n - q, 3); } },
            { "Gate Chop",       [](GridRow r, int c, int, int)      { return lvl(r == GridRow::Gate && (c % 2) == 1, 2); } },
            { "Half-time Gate",  [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Gate && (c % q) != 0, 3); } },
            { "Shatter Bursts",  [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Shatter && (c % juce::jmax(1, q * 4)) == q * 3, 3); } },
            { "Dissolve",        [](GridRow r, int c, int, int n)    { return r == GridRow::Shatter ? juce::jmin(3, (c * 4) / juce::jmax(1, n)) : 0; } },

            { "Reverse Hits",    [](GridRow r, int c, int q, int)    { return lvl(r == GridRow::Reverse && (c % juce::jmax(1, q * 4)) == q * 2, 3); } },
            { "Octave Jumps",    [](GridRow r, int c, int q, int)    { return (r == GridRow::PitchUp   && (c % juce::jmax(1, q * 4)) == 0)     ? 3
                                                                           : lvl(r == GridRow::PitchDown && (c % juce::jmax(1, q * 4)) == q * 2, 3); } },
            { "Descend",         [](GridRow r, int c, int, int n)    { return r == GridRow::PitchDown ? juce::jmin(3, 1 + (c * 2) / juce::jmax(1, n)) : 0; } },
            { "Riser",           [](GridRow r, int c, int, int n)    { return r == GridRow::Tone    ? juce::jmin(3, 1 + (c * 3) / juce::jmax(1, n))
                                                                           : r == GridRow::PitchUp ? lvl(c > (n * 4) / 5, 2)
                                                                           : lvl(r == GridRow::Stutter && c > (n * 9) / 10, 3); } },

            // Each lane gets one slot of the bar, in display order — a tour of everything
            // that's wired, which doubles as a quick "is this lane doing anything?" check.
            { "Everything Once", [](GridRow r, int c, int, int n)
                {
                    const auto& order = gridDisplayOrder();
                    int idx = -1;
                    for (size_t i = 0; i < order.size(); ++i) if (order[i] == r) idx = (int) i;
                    if (idx < 0 || order.empty()) return 0;
                    const int slotLen = juce::jmax(1, n / (int) order.size());
                    return lvl((c / slotLen) == idx, 3);
                } },
        };

        juce::File& testDirOverride()
        {
            static juce::File dir;      // invalid by default = use the real folder
            return dir;
        }
    }

    const juce::StringArray& builtInGridPatternNames()
    {
        static const juce::StringArray names = []
        {
            juce::StringArray n;
            for (const auto& p : kPatterns) n.add(p.name);
            return n;
        }();
        return names;
    }

    bool applyBuiltInGridPattern(const juce::String& name, FxGrid& grid)
    {
        const NamedPattern* found = nullptr;
        for (const auto& p : kPatterns)
            if (name == p.name) { found = &p; break; }

        if (found == nullptr) return false;      // leave the grid alone entirely

        const int n = grid.getNumSteps();
        const int q = gridStepsPerBeat;

        grid.clearAll();
        for (int row = 0; row < FxGrid::numRows; ++row)
            for (int c = 0; c < n; ++c)
                grid.setCell(row, c, found->fn((GridRow) row, c, q, n));

        return true;
    }

    void setGridPresetDirectoryForTesting(const juce::File& dir)
    {
        testDirOverride() = dir;
    }

    juce::File gridPresetDirectory()
    {
        if (testDirOverride() != juce::File())
        {
            testDirOverride().createDirectory();
            return testDirOverride();
        }

        auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                       .getChildFile("Nebula2")
                       .getChildFile("Grid Patterns");
        dir.createDirectory();
        return dir;
    }

    juce::String sanitiseGridPresetName(const juce::String& name)
    {
        // Everything a filename can't survive: path separators (which would let a name
        // write outside the preset folder), the characters Windows rejects outright, and
        // control characters.
        juce::String out;
        for (auto c : name)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"'
                || c == '<' || c == '>' || c == '|' || c < 32)
                continue;
            out += juce::String::charToString(c);
        }

        out = out.trim();
        while (out.contains("  ")) out = out.replace("  ", " ");

        // Leading dots make hidden files on unix and confuse Windows; trailing dots and
        // spaces are silently stripped by Windows, so "Foo." and "Foo" would collide.
        while (out.startsWithChar('.')) out = out.substring(1).trim();
        while (out.endsWithChar('.') || out.endsWithChar(' ')) out = out.dropLastCharacters(1);

        return out.substring(0, 64);
    }

    juce::File gridPresetFileFor(const juce::String& name)
    {
        const auto safe = sanitiseGridPresetName(name);
        if (safe.isEmpty()) return {};
        return gridPresetDirectory().getChildFile(safe + ".n2grid");
    }

    bool saveGridPreset(const juce::String& name, const FxGrid& grid)
    {
        const auto file = gridPresetFileFor(name);
        if (file == juce::File()) return false;
        return file.replaceWithText(grid.toString());
    }

    bool loadGridPreset(const juce::String& name, FxGrid& grid)
    {
        const auto file = gridPresetFileFor(name);
        if (file == juce::File() || ! file.existsAsFile()) return false;

        const auto text = file.loadFileAsString().trim();
        if (text.isEmpty() || ! text.contains(":")) return false;   // don't wipe on garbage

        grid.fromString(text);
        return true;
    }

    bool deleteGridPreset(const juce::String& name)
    {
        const auto file = gridPresetFileFor(name);
        return file != juce::File() && file.existsAsFile() && file.deleteFile();
    }

    juce::StringArray listGridPresets()
    {
        juce::StringArray names;
        for (const auto& f : gridPresetDirectory().findChildFiles(juce::File::findFiles, false, "*.n2grid"))
            names.add(f.getFileNameWithoutExtension());
        names.sort(true);
        return names;
    }
}
