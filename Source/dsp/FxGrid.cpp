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
