#include "RackGraph.h"

namespace Nebula2
{
    namespace
    {
        struct ModuleDef { const char* slug; const char* name; bool in, out, cv; };

        // Jack map, straight from the prototype's markup. The LFO has an out but no in:
        // it makes CV, it doesn't process audio. `src` has out only; `out` has in only.
        const ModuleDef defs[numRackModules] =
        {
            { "src", "Beat",       false, true,  false },
            { "eq",  "EQ",         true,  true,  false },
            { "flt", "Ladder",     true,  true,  true  },
            { "phs", "Phaser",     true,  true,  false },
            { "cho", "Chorus",     true,  true,  false },
            { "cmb", "Comb",       true,  true,  true  },
            { "fld", "Wavefolder", true,  true,  true  },
            { "vow", "Vowel",      true,  true,  true  },
            { "ech", "Echo",       true,  true,  false },
            { "lfo", "LFO",        false, true,  false },
            { "out", "Main Out",   true,  false, false },
        };

        int idx(ModuleId m) noexcept { return (int) m; }
    }

    const char* moduleSlug(ModuleId m) noexcept
    {
        return m == ModuleId::count ? "" : defs[idx(m)].slug;
    }

    const char* moduleName(ModuleId m) noexcept
    {
        return m == ModuleId::count ? "" : defs[idx(m)].name;
    }

    ModuleId moduleFromSlug(juce::StringRef slug) noexcept
    {
        for (int i = 0; i < numRackModules; ++i)
            if (juce::String (slug) == juce::String (defs[i].slug)) return (ModuleId) i;
        return ModuleId::count;
    }

    bool moduleExists(juce::StringRef slug) noexcept
    {
        return moduleFromSlug(slug) != ModuleId::count;
    }

    bool hasJack(ModuleId m, Jack j) noexcept
    {
        if (m == ModuleId::count) return false;
        const auto& d = defs[idx(m)];
        switch (j)
        {
            case Jack::in:  return d.in;
            case Jack::out: return d.out;
            case Jack::cv:  return d.cv;
        }
        return false;
    }

    Port parsePort(juce::StringRef id) noexcept
    {
        const juce::String s (id);
        const int colon = s.indexOfChar (':');
        if (colon <= 0) return {};

        Port p;
        p.module = moduleFromSlug (s.substring (0, colon));
        const auto jackTxt = s.substring (colon + 1);
        if      (jackTxt == "in")  p.jack = Jack::in;
        else if (jackTxt == "out") p.jack = Jack::out;
        else if (jackTxt == "cv")  p.jack = Jack::cv;
        else return {};

        return p.valid() ? p : Port{};
    }

    juce::String portToString(const Port& p)
    {
        if (p.module == ModuleId::count) return {};
        const char* j = p.jack == Jack::in ? "in" : p.jack == Jack::out ? "out" : "cv";
        return juce::String (moduleSlug (p.module)) + ":" + j;
    }

    const char* patchResultText(PatchResult r) noexcept
    {
        switch (r)
        {
            case PatchResult::ok:             return "Patched";
            case PatchResult::badPort:        return "That jack doesn't exist";
            case PatchResult::wrongDirection: return "Cables run out -> in";
            case PatchResult::duplicate:      return "Already patched";
            case PatchResult::wouldLoop:      return "That would feed back into itself";
            case PatchResult::cvIntoAudio:    return "CV can't drive an audio input";
            case PatchResult::audioIntoCV:    return "Only the LFO can drive a CV input";
        }
        return "";
    }

    const char* moduleStateText(ModuleState s) noexcept
    {
        switch (s)
        {
            case ModuleState::live:      return "LIVE";
            case ModuleState::noPathOut: return "NO PATH OUT";
            case ModuleState::idle:      return "IDLE";
            case ModuleState::off:       return "OFF";
        }
        return "";
    }

    RackGraph::RackGraph() { bypassed.fill (false); }

    PatchResult RackGraph::canPatch(const Port& from, const Port& to) const noexcept
    {
        if (! from.valid() || ! to.valid())  return PatchResult::badPort;
        if (from.jack != Jack::out)          return PatchResult::wrongDirection;
        if (to.jack == Jack::out)            return PatchResult::wrongDirection;
        if (from.module == to.module)        return PatchResult::wouldLoop;

        // The LFO makes control voltage, not audio. Letting it into an audio input would
        // "work" (it's just a signal) and sound like a fault — so it's refused by name.
        const bool fromIsCV = (from.module == ModuleId::lfo);
        if (fromIsCV && to.jack == Jack::in)  return PatchResult::cvIntoAudio;
        if (! fromIsCV && to.jack == Jack::cv) return PatchResult::audioIntoCV;

        for (const auto& c : cables)
            if (c.from == from && c.to == to) return PatchResult::duplicate;

        // Would this close an audio loop? Walk back from `from` and see if `to` already
        // reaches it. CV cables can't form an audio loop, so they skip this.
        if (to.jack == Jack::in)
        {
            std::array<bool, numRackModules> seen {};
            seen.fill (false);
            seen[idx (from.module)] = true;

            for (int pass = 0; pass < numRackModules; ++pass)
            {
                bool grew = false;
                for (const auto& c : cables)
                {
                    if (c.isCV()) continue;
                    if (seen[idx (c.to.module)] && ! seen[idx (c.from.module)])
                    {
                        seen[idx (c.from.module)] = true;
                        grew = true;
                    }
                }
                if (! grew) break;
            }
            if (seen[idx (to.module)]) return PatchResult::wouldLoop;
        }

        return PatchResult::ok;
    }

    PatchResult RackGraph::addCable(const Port& from, const Port& to)
    {
        const auto r = canPatch (from, to);
        if (r == PatchResult::ok) cables.push_back ({ from, to });
        return r;
    }

    bool RackGraph::removeCable(const Port& from, const Port& to)
    {
        for (auto it = cables.begin(); it != cables.end(); ++it)
            if (it->from == from && it->to == to) { cables.erase (it); return true; }
        return false;
    }

    void RackGraph::removeCablesTouching(ModuleId m)
    {
        for (auto it = cables.begin(); it != cables.end();)
            it = (it->from.module == m || it->to.module == m) ? cables.erase (it) : it + 1;
    }

    void RackGraph::clear() { cables.clear(); }

    bool RackGraph::isWired(ModuleId m) const noexcept
    {
        for (const auto& c : cables)
            if (c.from.module == m || c.to.module == m) return true;
        return false;
    }

    void RackGraph::setBypassed(ModuleId m, bool b) noexcept
    {
        if (m != ModuleId::count) bypassed[idx (m)] = b;
    }

    bool RackGraph::isBypassed(ModuleId m) const noexcept
    {
        return m != ModuleId::count && bypassed[idx (m)];
    }

    std::array<bool, numRackModules> RackGraph::reach(bool forward) const noexcept
    {
        std::array<bool, numRackModules> seen {};
        seen.fill (false);
        seen[idx (forward ? ModuleId::src : ModuleId::out)] = true;

        for (int pass = 0; pass < numRackModules; ++pass)
        {
            bool grew = false;
            for (const auto& c : cables)
            {
                if (c.isCV()) continue;               // CV is control, not audio
                const int a = idx (forward ? c.from.module : c.to.module);
                const int b = idx (forward ? c.to.module   : c.from.module);
                if (seen[a] && ! seen[b]) { seen[b] = true; grew = true; }
            }
            if (! grew) break;
        }
        return seen;
    }

    ModuleState RackGraph::stateOf(ModuleId m) const noexcept
    {
        if (m == ModuleId::count) return ModuleState::idle;
        if (bypassed[idx (m)])    return ModuleState::off;

        const auto fwd  = reach (true);
        const auto back = reach (false);

        // A CV source is live when the module it drives is live — the LFO never carries
        // audio, so the two audio sweeps can never find it.
        if (m == ModuleId::lfo)
        {
            for (const auto& c : cables)
            {
                if (! c.isCV() || c.from.module != ModuleId::lfo) continue;
                const int d = idx (c.to.module);
                if (fwd[d] && back[d] && ! bypassed[d]) return ModuleState::live;
            }
            return isWired (m) ? ModuleState::noPathOut : ModuleState::idle;
        }

        if (fwd[idx (m)] && back[idx (m)]) return ModuleState::live;
        return isWired (m) ? ModuleState::noPathOut : ModuleState::idle;
    }

    bool RackGraph::isLive() const noexcept
    {
        const auto fwd = reach (true);
        return fwd[idx (ModuleId::out)];
    }

    std::vector<ModuleId> RackGraph::processOrder() const
    {
        std::vector<ModuleId> order;
        if (! isLive()) return order;

        const auto fwd  = reach (true);
        const auto back = reach (false);

        // Kahn's algorithm over the live audio sub-graph only. Anything not on a path from
        // src to out is not processed — it can't be heard, so spending CPU on it is waste.
        std::array<int, numRackModules> indegree {};
        indegree.fill (0);
        auto inPlay = [&] (ModuleId m)
        {
            const int i = idx (m);
            return fwd[i] && back[i] && m != ModuleId::src && m != ModuleId::out;
        };

        for (const auto& c : cables)
        {
            if (c.isCV()) continue;
            if (inPlay (c.from.module) && inPlay (c.to.module)) ++indegree[idx (c.to.module)];
        }

        std::vector<ModuleId> ready;
        for (int i = 0; i < numRackModules; ++i)
            if (inPlay ((ModuleId) i) && indegree[i] == 0) ready.push_back ((ModuleId) i);

        while (! ready.empty())
        {
            const auto m = ready.front();
            ready.erase (ready.begin());
            order.push_back (m);

            for (const auto& c : cables)
            {
                if (c.isCV() || c.from.module != m) continue;
                if (! inPlay (c.to.module)) continue;
                if (--indegree[idx (c.to.module)] == 0) ready.push_back (c.to.module);
            }
        }
        return order;
    }

    std::vector<ModuleId> RackGraph::cvSourcesFor(ModuleId m) const
    {
        std::vector<ModuleId> v;
        for (const auto& c : cables)
            if (c.isCV() && c.to.module == m) v.push_back (c.from.module);
        return v;
    }

    juce::String RackGraph::toString() const
    {
        // "flt:out>out:in;src:out>flt:in|flt,cmb"   cables | bypassed
        juce::StringArray cs;
        for (const auto& c : cables)
            cs.add (portToString (c.from) + ">" + portToString (c.to));

        juce::StringArray bs;
        for (int i = 0; i < numRackModules; ++i)
            if (bypassed[i]) bs.add (moduleSlug ((ModuleId) i));

        return cs.joinIntoString (";") + "|" + bs.joinIntoString (",");
    }

    RackGraph RackGraph::fromString(juce::StringRef s)
    {
        // Malformed input falls back to an empty rack — never garbage, never a crash. A
        // bad cable is dropped on its own; it doesn't take the rest of the patch with it.
        RackGraph g;
        const juce::String str (s);
        if (str.isEmpty()) return g;

        const auto bar = str.indexOfChar ('|');
        const auto cablePart  = bar >= 0 ? str.substring (0, bar) : str;
        const auto bypassPart = bar >= 0 ? str.substring (bar + 1) : juce::String();

        juce::StringArray cs;
        cs.addTokens (cablePart, ";", "");
        for (const auto& c : cs)
        {
            const auto arrow = c.indexOfChar ('>');
            if (arrow <= 0) continue;
            const auto from = parsePort (c.substring (0, arrow).trim());
            const auto to   = parsePort (c.substring (arrow + 1).trim());
            if (from.valid() && to.valid()) g.addCable (from, to);   // re-validated, so a
                                                                    // saved loop can't sneak in
        }

        juce::StringArray bs;
        bs.addTokens (bypassPart, ",", "");
        for (const auto& b : bs)
        {
            const auto m = moduleFromSlug (b.trim());
            if (m != ModuleId::count) g.setBypassed (m, true);
        }
        return g;
    }
}
