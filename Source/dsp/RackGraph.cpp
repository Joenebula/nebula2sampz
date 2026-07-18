#include "RackGraph.h"
#include "../ParameterIDs.h"

namespace Nebula2
{
    namespace
    {
        struct ModuleDef { const char* slug; const char* name; const char* sub; bool in, out, cv; };

        // Jack map, straight from the prototype's markup. The LFO has an out but no in:
        // it makes CV, it doesn't process audio. `src` has out only; `out` has in only.
        //
        // `sub` is the line under the name. The prototype separates its parts with a middle
        // dot; that is a raw non-ASCII byte in a string literal, which is exactly what
        // rendered as mojibake in the rack status line, so these use a slash. CI now
        // rejects the alternative outright.
        const ModuleDef defs[numRackModules] =
        {
            { "src", "Beat Out",      "the mix",            false, true,  false },
            // This line was "3-band" until the draggable nodes actually existed, because
            // the UI must not advertise an interaction that does nothing. They exist now.
            { "eq",  "Parametric EQ", "drag nodes / wheel = Q", true, true, false },
            { "flt", "Ladder",        "filter / cv",        true,  true,  true  },
            { "phs", "Phaser",        "6-stage sweep",      true,  true,  false },
            { "cho", "Chorus",        "3-voice",            true,  true,  false },
            { "cmb", "Comb",          "tuned / metallic",   true,  true,  true  },
            { "fld", "Wavefolder",    "destruction",        true,  true,  true  },
            { "vow", "Vowel",         "formant / it talks", true,  true,  true  },
            { "ech", "Space Echo",    "tape delay",         true,  true,  false },
            { "lfo", "LFO",           "cv source",          false, true,  false },
            { "out", "Main Out",      "-> speakers",        true,  false, false },
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

    const char* moduleSub(ModuleId m) noexcept
    {
        return m == ModuleId::count ? "" : defs[idx(m)].sub;
    }

    // Labels are the prototype's, which name what a knob DOES: Fold not Drive, Bias not
    // Sym, Repeats not Fb, Decay not Fb.
    const std::vector<RackDialDef>& rackDialDefs()
    {
        static const std::vector<RackDialDef> v =
        {
            { ModuleId::flt, ParamID::fltCut,   "Cut"     },
            { ModuleId::flt, ParamID::fltRes,   "Res"     },

            { ModuleId::lfo, ParamID::lfoRate,  "Rate"    },
            { ModuleId::lfo, ParamID::lfoDepth, "Depth"   },

            { ModuleId::phs, ParamID::phsRate,  "Rate"    },
            { ModuleId::phs, ParamID::phsDepth, "Depth"   },   // had no control
            { ModuleId::phs, ParamID::phsFb,    "Fdbk"    },
            { ModuleId::phs, ParamID::phsMix,   "Mix"     },

            { ModuleId::cho, ParamID::choRate,  "Rate"    },
            { ModuleId::cho, ParamID::choDepth, "Depth"   },
            { ModuleId::cho, ParamID::choMix,   "Mix"     },

            { ModuleId::cmb, ParamID::cmbTune,  "Tune"    },
            { ModuleId::cmb, ParamID::cmbFb,    "Decay"   },
            { ModuleId::cmb, ParamID::cmbMix,   "Mix"     },

            { ModuleId::fld, ParamID::fldDrive, "Fold"    },
            { ModuleId::fld, ParamID::fldSym,   "Bias"    },
            { ModuleId::fld, ParamID::fldMix,   "Mix"     },

            { ModuleId::vow, ParamID::vowMorph, "Vowel"   },
            { ModuleId::vow, ParamID::vowSharp, "Sharp"   },
            { ModuleId::vow, ParamID::vowMix,   "Mix"     },

            { ModuleId::ech, ParamID::echTime,  "Time"    },
            { ModuleId::ech, ParamID::echFb,    "Repeats" },
            { ModuleId::ech, ParamID::echWow,   "Wow"     },   // had no control
            { ModuleId::ech, ParamID::echMix,   "Mix"     },

            // The EQ has NO dials. Its five bands are dragged on the response curve
            // instead - see eqEditorParamIds(), which is how the reachability test knows
            // those twenty parameters have a control.
            { ModuleId::out, ParamID::outLvl,   "Level"   },
        };
        return v;
    }

    // Choice params, so they are dropdowns in the rack header rather than dials.
    const std::vector<const char*>& rackDropdownParamIds()
    {
        static const std::vector<const char*> v = { ParamID::fltType, ParamID::lfoShape };
        return v;
    }

    // The EQ's parameters, reached by dragging nodes on the response curve rather than by
    // any dial. Listed here so the reachability test can still account for every one of
    // them: a bespoke editor is a control, but only if it genuinely covers what it claims.
    // If a band is added to the DSP and not to EqCurve, this list is where it fails.
    const std::vector<const char*>& eqEditorParamIds()
    {
        static const std::vector<const char*> v = []
        {
            std::vector<const char*> ids;
            for (int i = 0; i < ParamID::numEqBands; ++i)
            {
                ids.push_back (ParamID::eqFreq[i]);
                ids.push_back (ParamID::eqGain[i]);
                ids.push_back (ParamID::eqQ[i]);
                ids.push_back (ParamID::eqOn[i]);
            }
            return ids;
        }();
        return v;
    }

    // Must stay in the order PluginProcessor::readRackDials() unpacks them.
    const std::vector<const char*>& rackDspParamIds()
    {
        static const std::vector<const char*> v =
        {
            ParamID::fltCut,  ParamID::fltRes,   ParamID::fltType,
            ParamID::lfoRate, ParamID::lfoDepth, ParamID::lfoShape,
            ParamID::phsRate, ParamID::phsDepth, ParamID::phsFb,  ParamID::phsMix,
            ParamID::choRate, ParamID::choDepth, ParamID::choMix,
            ParamID::cmbTune, ParamID::cmbFb,    ParamID::cmbMix,
            ParamID::fldDrive, ParamID::fldSym,  ParamID::fldMix,
            ParamID::vowMorph, ParamID::vowSharp, ParamID::vowMix,
            ParamID::echTime, ParamID::echFb,    ParamID::echWow, ParamID::echMix,
            ParamID::outLvl,
        };
        return v;
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

    // --- the audio-thread versions: same answers, no heap ---

    bool RackGraph::hasLiveCV(ModuleId m) const noexcept
    {
        for (const auto& c : cables)
            if (c.isCV() && c.to.module == m && ! bypassed[idx (c.from.module)])
                return true;
        return false;
    }

    int RackGraph::processOrderInto(ModuleId* dest, int maxLen) const noexcept
    {
        if (dest == nullptr || maxLen <= 0 || ! isLive()) return 0;

        const auto fwd  = reach (true);
        const auto back = reach (false);

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

        // Fixed-size ready queue instead of a vector: there can never be more entries than
        // there are modules, so the bound is known at compile time.
        std::array<ModuleId, numRackModules> ready {};
        int readyHead = 0, readyTail = 0, count = 0;

        for (int i = 0; i < numRackModules; ++i)
            if (inPlay ((ModuleId) i) && indegree[i] == 0) ready[readyTail++] = (ModuleId) i;

        while (readyHead < readyTail && count < maxLen)
        {
            const auto m = ready[readyHead++];
            dest[count++] = m;

            for (const auto& c : cables)
            {
                if (c.isCV() || c.from.module != m) continue;
                if (! inPlay (c.to.module)) continue;
                if (--indegree[idx (c.to.module)] == 0 && readyTail < numRackModules)
                    ready[readyTail++] = c.to.module;
            }
        }
        return count;
    }

    std::size_t RackGraph::topologyHash() const noexcept
    {
        // Cheap and order-sensitive. It only has to CHANGE when the patch does; it does not
        // have to be collision-proof, because a collision costs one block of a stale
        // process order, not a wrong answer forever.
        std::size_t h = 1469598103934665603ull;
        auto mix = [&h] (std::size_t v) { h ^= v; h *= 1099511628211ull; };
        for (const auto& c : cables)
        {
            mix ((std::size_t) idx (c.from.module) * 8 + (std::size_t) c.from.jack);
            mix ((std::size_t) idx (c.to.module)   * 8 + (std::size_t) c.to.jack);
        }
        for (int i = 0; i < numRackModules; ++i)
            if (bypassed[i]) mix ((std::size_t) (i + 1) * 7919);
        return h;
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

namespace Nebula2
{
    void randomiseRack(RackGraph& graph, juce::Random& rng)
    {
        graph.clear();
        for (int i = 0; i < numRackModules; ++i)
            graph.setBypassed((ModuleId) i, false);   // a dice that leaves modules off is a dud

        // The audio modules that can sit in a chain. src and out are the endpoints; lfo is
        // CV only and is patched separately.
        const ModuleId pool[] = { ModuleId::eq,  ModuleId::flt, ModuleId::phs, ModuleId::cho,
                                  ModuleId::cmb, ModuleId::fld, ModuleId::vow, ModuleId::ech };
        constexpr int poolSize = (int) (sizeof(pool) / sizeof(pool[0]));

        std::vector<ModuleId> bag(pool, pool + poolSize);
        const int want = 2 + rng.nextInt(3);      // 2..4 in the chain: enough to hear, not mud

        std::vector<ModuleId> chain;
        for (int i = 0; i < want && ! bag.empty(); ++i)
        {
            const int pick = rng.nextInt((int) bag.size());
            chain.push_back(bag[(size_t) pick]);
            bag.erase(bag.begin() + pick);
        }

        // src -> chain... -> out. Every cable is checked: a refusal here would mean the
        // chain silently has a hole in it, and the rack would be patched but dead.
        ModuleId prev = ModuleId::src;
        for (auto m : chain)
        {
            if (graph.addCable({ prev, Jack::out }, { m, Jack::in }) != PatchResult::ok)
                continue;                          // skip it rather than break the chain
            prev = m;
        }
        graph.addCable({ prev, Jack::out }, { ModuleId::out, Jack::in });

        // Sometimes let the LFO drive something. Only onto a module that HAS a cv jack —
        // patching to a jack that isn't there is refused, and a silently-dropped cable is
        // exactly the failure this graph was built to make impossible.
        if (rng.nextFloat() < 0.6f && ! chain.empty())
        {
            std::vector<ModuleId> cvTargets;
            for (auto m : chain)
                if (hasJack(m, Jack::cv)) cvTargets.push_back(m);

            if (! cvTargets.empty())
                graph.addCable({ ModuleId::lfo, Jack::out },
                               { cvTargets[(size_t) rng.nextInt((int) cvTargets.size())], Jack::cv });
        }
    }
}
