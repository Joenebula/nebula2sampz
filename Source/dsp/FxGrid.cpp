#include "FxGrid.h"
#include "../ParameterIDs.h"

#include <cmath>

namespace Nebula2
{
    const char* gridRowName(GridRow r)
    {
        switch (r)
        {
            case GridRow::Drive:   return "Drive";
            case GridRow::Crush:   return "Crush";
            case GridRow::Squeeze: return "Squeeze";
            case GridRow::Tone:    return "Tone";
            case GridRow::Width:   return "Width";
            case GridRow::Reverb:  return "Reverb";
            case GridRow::Delay:   return "Delay";
            case GridRow::Pump:      return "Pump";
            case GridRow::Resonate:  return "Resonate";
            case GridRow::PitchUp:   return "Pitch +";
            case GridRow::PitchDown: return "Pitch -";
            case GridRow::Reverse:   return "Reverse";
            case GridRow::Stutter:   return "Stutter";
            case GridRow::Shatter:   return "Shatter";
            case GridRow::Gate:      return "Gate";
            case GridRow::Haunt:     return "Haunt";
            default:               return "?";
        }
    }

    const char* gridRowPanelParamId(GridRow r)
    {
        switch (r)
        {
            case GridRow::Drive:     return ParamID::drive;
            case GridRow::Crush:     return ParamID::crush;
            case GridRow::Squeeze:   return ParamID::squeeze;
            case GridRow::Tone:      return ParamID::tone;
            case GridRow::Width:     return ParamID::width;
            case GridRow::Reverb:    return ParamID::revMix;
            case GridRow::Delay:     return ParamID::dlyMix;
            case GridRow::Pump:      return ParamID::pump;
            case GridRow::Resonate:  return ParamID::resonate;
            case GridRow::PitchUp:   return ParamID::pitchUp;
            case GridRow::PitchDown: return ParamID::pitchDown;
            case GridRow::Reverse:   return ParamID::reverse;
            case GridRow::Stutter:   return ParamID::stutter;
            case GridRow::Shatter:   return ParamID::shatter;
            case GridRow::Gate:      return ParamID::gate;
            case GridRow::Haunt:     return ParamID::haunt;
            default:                 return nullptr;
        }
    }

    const std::vector<PanelControlSpec>& extraColourControls()
    {
        // The lane knobs the Colour page grows beyond the original six. The editor builds
        // these by LOOPING this table, so "listed here" and "has a control" are one fact
        // rather than two that can disagree.
        static const std::vector<PanelControlSpec> extras = {
            { ParamID::resonate,  "Resonate", " %" },
            { ParamID::pitchUp,   "Pitch +",  " %" },
            { ParamID::pitchDown, "Pitch -",  " %" },
            { ParamID::reverse,   "Reverse",  " %" },
            { ParamID::stutter,   "Stutter",  " %" },
            { ParamID::shatter,   "Shatter",  " %" },
            { ParamID::gate,      "Gate",     " %" },
        };
        return extras;
    }

    const std::vector<const char*>& editorControlledParamIds()
    {
        static const std::vector<const char*> ids = [] {
            std::vector<const char*> v {
                // Hand-placed in PluginEditor's designed layout.
                ParamID::drive, ParamID::crush, ParamID::squeeze, ParamID::tone,
                ParamID::width, ParamID::pump, ParamID::master,
                ParamID::revMix, ParamID::revSize, ParamID::dlyMix, ParamID::dlyFb,
                ParamID::haunt,
                // Choice controls.
                ParamID::driveChar, ParamID::revChar, ParamID::dlySync, ParamID::dlyMode,
                ParamID::resoKey, ParamID::resoScale,
                ParamID::smpVol,
            };
            // ...plus everything the editor loops over, which therefore cannot fall out of step.
            for (const auto& e : extraColourControls()) v.push_back(e.paramId);
            return v;
        }();
        return ids;
    }

    const std::vector<GridRow>& gridDisplayOrder()
    {
        // ONLY lanes whose effect exists — see the header. Add a row here when its DSP
        // lands, never before, or you ship a lane that paints and does nothing.
        static const std::vector<GridRow> order = {
            GridRow::Tone,      // the prototype labels this lane "Filter"
            GridRow::Drive, GridRow::Crush, GridRow::Squeeze, GridRow::Pump, GridRow::Width,
            GridRow::Resonate, GridRow::PitchUp, GridRow::PitchDown,
            GridRow::Reverse, GridRow::Stutter, GridRow::Shatter, GridRow::Gate,
            GridRow::Reverb, GridRow::Delay, GridRow::Haunt
            // Every lane in the prototype now has an effect behind it.
        };
        return order;
    }

    int gridPanelHeight()
    {
        return (int) gridDisplayOrder().size() * gridLaneHeight + gridNoticeHeight;
    }

    const char* randomDensityName(RandomDensity d) noexcept
    {
        switch (d)
        {
            case RandomDensity::Low:  return "Low";
            case RandomDensity::Mid:  return "Mid";
            case RandomDensity::High: return "High";
            default:                  return "?";
        }
    }

    std::vector<GridRow> pickRandomLanes(const std::vector<GridRow>& from,
                                         RandomDensity density, juce::Random& rng)
    {
        // The cast sizes match randomiseGrid's own table below, deliberately: the caller now
        // switches these lanes ON before painting, so if the two lists disagreed the dice
        // would turn up lanes it never paints, or paint lanes it never turned up.
        int castMin = 3, castMax = 4;
        switch (density)
        {
            case RandomDensity::Low:  castMin = 1; castMax = 2; break;
            case RandomDensity::High: castMin = 5; castMax = 7; break;
            case RandomDensity::Mid:
            default:                  break;
        }

        std::vector<GridRow> pool (from);
        // Without replacement. Picking with replacement gives a cast that is quietly
        // smaller than the density asked for, which is the bug this whole function exists
        // to avoid repeating.
        for (int i = (int) pool.size() - 1; i > 0; --i)
            std::swap (pool[(size_t) i], pool[(size_t) rng.nextInt (i + 1)]);

        const int want = juce::jlimit (0, (int) pool.size(),
                                       castMin + rng.nextInt (juce::jmax (1, castMax - castMin + 1)));
        pool.resize ((size_t) want);
        return pool;
    }

    void randomiseGrid(FxGrid& grid, const std::vector<GridRow>& eligible,
                       RandomDensity density, juce::Random& rng)
    {
        grid.clearAll();
        if (eligible.empty()) return;      // nothing can sound; leave it clean

        const int steps = grid.getNumSteps();
        if (steps <= 0) return;
        const int q = gridStepsPerBeat;

        // How many lanes join in, and how hard they play. Those two ARE what Low/Mid/High
        // means — the prototype rolled the cast size at random (2..6), which is the same
        // range with no way to ask for one end of it.
        int castMin = 0, castMax = 0;
        float densMin = 0.0f, densMax = 0.0f;
        switch (density)
        {
            case RandomDensity::Low:  castMin = 1; castMax = 2; densMin = 0.08f; densMax = 0.18f; break;
            case RandomDensity::High: castMin = 5; castMax = 7; densMin = 0.18f; densMax = 0.36f; break;
            case RandomDensity::Mid:
            default:                  castMin = 3; castMax = 4; densMin = 0.10f; densMax = 0.22f; break;
        }

        // Draw the cast WITHOUT replacement, or a lane can be picked twice and the pattern
        // is quietly thinner than the density asked for.
        std::vector<GridRow> bag = eligible;
        const int want = juce::jmin((int) bag.size(),
                                    castMin + rng.nextInt(juce::jmax(1, castMax - castMin + 1)));
        std::vector<GridRow> cast;
        for (int i = 0; i < want && ! bag.empty(); ++i)
        {
            const int pick = rng.nextInt((int) bag.size());
            cast.push_back(bag[(size_t) pick]);
            bag.erase(bag.begin() + pick);
        }

        int total = 0;
        for (auto row : cast)
        {
            // Each lane picks a CHARACTER, so the result is rhythmic rather than a wash of
            // unrelated hits. The prototype's four styles, kept.
            const float style = rng.nextFloat();
            const float dens  = densMin + rng.nextFloat() * (densMax - densMin);

            for (int c = 0; c < steps; ++c)
            {
                const bool onBeat    = (c % q) == 0;
                const bool offBeat   = (c % q) == q / 2;
                const bool lastOfBar = (c % (q * 4)) == (q * 4 - 1);

                bool hit = false;
                if (style < 0.34f)      hit = onBeat  && rng.nextFloat() < dens * 3.0f;
                else if (style < 0.62f) hit = offBeat && rng.nextFloat() < dens * 3.0f;
                else if (style < 0.82f) hit = lastOfBar || rng.nextFloat() < dens * 0.8f;
                else                    hit = rng.nextFloat() < dens;

                if (hit)
                {
                    grid.setCell((int) row, c, 1 + rng.nextInt(3));
                    ++total;
                }
            }
        }

        // Never hand back an empty grid — indistinguishable from a broken button.
        if (total == 0 && ! cast.empty())
        {
            const auto row = cast[(size_t) rng.nextInt((int) cast.size())];
            for (int c = 0; c < steps; c += q) grid.setCell((int) row, c, 3);
        }
    }

    float gridRowNeutral(GridRow r)
    {
        switch (r)
        {
            case GridRow::Tone:    return 100.0f;   // open
            case GridRow::Width:   return 100.0f;   // unchanged
            default:               return 0.0f;     // silent / no send
        }
    }

    int FxGrid::stepAtPpq(double ppq, int numStepsIn, double beatsPerStep) noexcept
    {
        if (numStepsIn <= 0 || beatsPerStep <= 0.0) return 0;
        const double raw = std::floor(ppq / beatsPerStep);
        long long s = (long long) raw;
        int step = (int) (s % (long long) numStepsIn);
        if (step < 0) step += numStepsIn;          // negative ppq (host pre-roll) must wrap sanely
        return step;
    }

    float FxGrid::blend(float neutral, float panelAmount, int cell) noexcept
    {
        const int c = juce::jlimit(0, 3, cell);
        return neutral + (panelAmount - neutral) * ((float) c / 3.0f);
    }

    void FxGrid::setNumSteps(int n) noexcept
    {
        numSteps = juce::jlimit(1, maxSteps, n);
    }

    void FxGrid::setCell(int row, int step, int level) noexcept
    {
        if (row < 0 || row >= numRows || step < 0 || step >= maxSteps) return;
        cells[(size_t) row][(size_t) step] = (uint8_t) juce::jlimit(0, 3, level);
    }

    int FxGrid::getCell(int row, int step) const noexcept
    {
        if (row < 0 || row >= numRows || step < 0 || step >= maxSteps) return 0;
        return (int) cells[(size_t) row][(size_t) step];
    }

    void FxGrid::clearAll() noexcept
    {
        for (auto& r : cells) r.fill(0);
    }

    void FxGrid::clearRow(int row) noexcept
    {
        if (row < 0 || row >= numRows) return;
        cells[(size_t) row].fill(0);
    }

    bool FxGrid::rowHasAnyCells(int row) const noexcept
    {
        if (row < 0 || row >= numRows) return false;
        for (int s = 0; s < numSteps; ++s) if (cells[(size_t) row][(size_t) s] > 0) return true;
        return false;
    }

    float FxGrid::amountFor(GridRow row, float panelAmount, int step) const noexcept
    {
        return blend(gridRowNeutral(row), panelAmount, getCell((int) row, step));
    }

    juce::String FxGrid::toString() const
    {
        juce::String out;
        out << numSteps << ":";
        for (int r = 0; r < numRows; ++r)
        {
            for (int s = 0; s < maxSteps; ++s) out << (int) cells[(size_t) r][(size_t) s];
            if (r < numRows - 1) out << ",";
        }
        return out;
    }

    void FxGrid::fromString(const juce::String& s)
    {
        clearAll();
        const int colon = s.indexOfChar(':');
        if (colon < 0) return;

        setNumSteps(s.substring(0, colon).getIntValue());

        auto rows = juce::StringArray::fromTokens(s.substring(colon + 1), ",", "");
        for (int r = 0; r < juce::jmin(rows.size(), numRows); ++r)
        {
            const auto& line = rows[r];
            for (int st = 0; st < juce::jmin(line.length(), maxSteps); ++st)
            {
                // String::operator[] gives a juce_wchar, not an int — cast before jlimit.
                const int level = (int) (line[st] - (juce::juce_wchar) '0');
                cells[(size_t) r][(size_t) st] = (uint8_t) juce::jlimit(0, 3, level);
            }
        }
    }
}

namespace Nebula2
{
    int quantiseToScale(int semitones, ResoScale scale) noexcept
    {
        const int pv = juce::jlimit(-noteLaneRange, noteLaneRange, semitones);

        const auto& degrees = resoScaleDegrees(scale);
        auto inScale = [&degrees](int v)
        {
            const int pc = ((v % 12) + 12) % 12;
            for (int d : degrees) if ((((d % 12) + 12) % 12) == pc) return true;
            return false;
        };

        // Search outward, DOWN first on a tie. The prototype's order, and the one that stops
        // a dragged line creeping sharp: preferring up would round every ambiguous pitch the
        // same direction and the whole part drifts.
        for (int dd = 0; dd <= 6; ++dd)
        {
            if (pv - dd >= -noteLaneRange && inScale(pv - dd)) return pv - dd;
            if (pv + dd <=  noteLaneRange && inScale(pv + dd)) return pv + dd;
        }
        return 0;
    }

    juce::String noteLaneName(int semitones, int keySemis)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        // Root is A: resoKey 0 means A, because the resonator bank's root is A1. Adding 9
        // rotates A to its place in a C-based name table.
        const int idx = ((9 + keySemis + (((semitones % 12) + 12) % 12)) % 12 + 12) % 12;
        return names[idx];
    }

    void FxGrid::setNote(int step, int semitones) noexcept
    {
        if (step < 0 || step >= maxSteps) return;
        notes[(size_t) step] = (int8_t) juce::jlimit(-noteLaneRange, noteLaneRange, semitones);
    }

    int FxGrid::getNote(int step) const noexcept
    {
        if (step < 0 || step >= maxSteps) return 0;
        return (int) notes[(size_t) step];
    }

    void FxGrid::clearNotes() noexcept { notes.fill(0); }

    bool FxGrid::anyNotes() const noexcept
    {
        for (int s = 0; s < numSteps; ++s) if (notes[(size_t) s] != 0) return true;
        return false;
    }

    juce::String FxGrid::notesToString() const
    {
        juce::String out;
        for (int s = 0; s < maxSteps; ++s)
        {
            if (s > 0) out << ",";
            out << (int) notes[(size_t) s];
        }
        return out;
    }

    void FxGrid::notesFromString(const juce::String& s)
    {
        clearNotes();
        if (s.isEmpty()) return;

        auto toks = juce::StringArray::fromTokens(s, ",", "");
        for (int i = 0; i < juce::jmin(toks.size(), maxSteps); ++i)
        {
            // Same rule as everywhere else here: a non-numeric token means the data is
            // corrupt, and the honest reading of corrupt data is "no notes" rather than a
            // line of zeroes that looks deliberate.
            const auto t = toks[i].trim();
            if (t.isEmpty() || ! t.containsOnly("-0123456789")) { clearNotes(); return; }
            setNote(i, t.getIntValue());
        }
    }
}
