#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace Nebula2
{
    // The modular rack's BRAIN: which modules are in the signal path, and why.
    //
    // This file is deliberately pure logic — no audio, no UI. The prototype's rack bug was
    // that it walked a single straight line from the Beat, so anything on a branch or
    // reached by a second cable was wrongly shown as dead. Its own fix was two reachability
    // sweeps; this is that fix, ported and tested rather than re-derived.
    //
    // The rule a module must satisfy to be LIVE: audio can reach it FROM the beat AND
    // escape from it TO the main out. Reachable-but-trapped is a real, common patching
    // mistake, and it earns its own state (NoPathOut) rather than being lumped in with
    // "idle" — law 4: a control that cannot act must SAY so.
    //
    // ---------------------------------------------------------------------------------
    // ARCHITECTURAL DECISION — CYCLES ARE REJECTED AT PATCH TIME.
    //
    // The browser prototype tolerated a cable loop only because Web Audio silently drops a
    // 128-sample delay into any cycle it finds. A block-based JUCE graph has no such
    // luxury: a loop either needs a whole-block delay (hidden latency, and it changes the
    // sound) or it has no defined answer. So `addCable` refuses a connection that would
    // close a loop, and says why.
    //
    // This costs little: the modules that actually want feedback (Comb, Echo, Phaser) each
    // carry their OWN feedback dial, so the musical idiom is already covered. What's
    // rejected is only cross-module cable feedback.
    //
    // Overrule this freely if you want loops — the alternative is a one-block delay in the
    // cycle, which is maybe 20 lines. It's cheap to change; it is NOT cheap to change once
    // presets exist that depend on one behaviour or the other.
    // ---------------------------------------------------------------------------------

    // Module ids match the prototype's, so presets and muscle memory carry over.
    //   src = the beat (source)   out = main out   lfo = CV only, never audio
    enum class ModuleId { src, eq, flt, phs, cho, cmb, fld, vow, ech, lfo, out, count };

    inline constexpr int numRackModules = (int) ModuleId::count;

    const char* moduleSlug(ModuleId m) noexcept;      // "flt"
    const char* moduleName(ModuleId m) noexcept;      // "Ladder"
    const char* moduleSub(ModuleId m) noexcept;       // "filter / cv" - the line beneath it
    bool moduleExists(juce::StringRef slug) noexcept;

    // --- rack dials: ONE table, built from, not asserted about ---
    //
    // RackView loops this to create its knobs, so "the list" and "what is on screen" cannot
    // disagree. They did: phsDepth, echWow and three of the six EQ bands were read by the
    // DSP every block with no control anywhere in the editor, so they sat at their defaults
    // forever. Exactly the seven-orphaned-grid-lanes bug, in a different panel.
    struct RackDialDef { ModuleId owner; const char* paramId; const char* label; };
    const std::vector<RackDialDef>& rackDialDefs();

    // Every rack parameter the audio thread reads. The test pairs this against the table
    // above (plus the two header dropdowns) so a new DSP parameter with no control fails.
    const std::vector<const char*>& rackDspParamIds();

    // The rack params reachable through the header dropdowns rather than a dial.
    const std::vector<const char*>& rackDropdownParamIds();

    // The EQ's params, reached by dragging nodes on the curve rather than by any dial.
    const std::vector<const char*>& eqEditorParamIds();
    ModuleId moduleFromSlug(juce::StringRef slug) noexcept;   // ModuleId::count if unknown

    enum class Jack { in, out, cv };

    // Not every module has every jack. Patching to a jack that isn't there is a bug, not a
    // silent no-op — the graph refuses it.
    bool hasJack(ModuleId m, Jack j) noexcept;

    struct Port
    {
        ModuleId module = ModuleId::count;
        Jack     jack   = Jack::in;

        bool valid() const noexcept { return module != ModuleId::count && hasJack(module, jack); }
        bool operator== (const Port& o) const noexcept { return module == o.module && jack == o.jack; }
    };

    Port parsePort(juce::StringRef id) noexcept;          // "flt:cv" -> Port
    juce::String portToString(const Port& p);             // Port -> "flt:cv"

    struct Cable
    {
        Port from;      // always an `out` jack
        Port to;        // an `in` (audio) or `cv` (control) jack

        bool isCV() const noexcept { return to.jack == Jack::cv; }
        bool operator== (const Cable& o) const noexcept { return from == o.from && to == o.to; }
    };

    // Why a patch was refused. The UI shows this verbatim — a rejected cable that just
    // vanishes with no reason is the exact silent failure this project keeps hunting.
    enum class PatchResult { ok, badPort, wrongDirection, duplicate, wouldLoop, cvIntoAudio, audioIntoCV };
    const char* patchResultText(PatchResult r) noexcept;

    // What a module is doing right now.
    //   live      — audio reaches it AND escapes to the out
    //   noPathOut — it's patched, but the path dead-ends: wired, and silent
    //   idle      — not patched into anything
    //   off       — bypassed by its own power button
    enum class ModuleState { live, noPathOut, idle, off };
    const char* moduleStateText(ModuleState s) noexcept;

    class RackGraph;

    // Build a random patch.
    //
    // Guaranteed LIVE: audio always reaches the out. A rack dice that hands back a patch
    // making no sound is indistinguishable from a broken button — and the rack is the one
    // block where "patched but silent" is a state you can genuinely land in, so the dice is
    // not allowed to produce it.
    //
    // A CHAIN, not a random tangle: modules in series from the source to the out, with the
    // LFO sometimes driving one of them. Cables drawn between arbitrary jacks would mostly
    // be refused (loops, CV into audio) or dead-end, which is a lot of rolling to arrive at
    // silence.
    //
    // RNG by reference so a seed reproduces a patch.
    void randomiseRack(RackGraph& graph, juce::Random& rng);

    class RackGraph
    {
    public:
        RackGraph();

        // --- patching ---
        PatchResult canPatch(const Port& from, const Port& to) const noexcept;
        PatchResult addCable(const Port& from, const Port& to);
        bool removeCable(const Port& from, const Port& to);
        void removeCablesTouching(ModuleId m);
        void clear();

        const std::vector<Cable>& getCables() const noexcept { return cables; }
        bool isWired(ModuleId m) const noexcept;

        // --- bypass ---
        void setBypassed(ModuleId m, bool b) noexcept;
        bool isBypassed(ModuleId m) const noexcept;

        // --- the question that matters ---
        ModuleState stateOf(ModuleId m) const noexcept;

        // Is the rack actually in circuit? If nothing reaches the main out, the dry beat
        // must still reach the speakers — the prototype's rule, and the honest one: a rack
        // you haven't patched yet should not silence your track.
        bool isLive() const noexcept;

        // Audio modules in the order they must be processed. Empty if the rack isn't live.
        // Guaranteed acyclic (addCable refuses loops), so a plain topological order exists.
        //
        // Returns a std::vector, so it ALLOCATES — do not call it from the audio thread.
        // RackEngine keeps a preallocated copy and refreshes it only when the patch
        // changes. Kept for the UI and for tests, where allocating is free.
        std::vector<ModuleId> processOrder() const;

        // The audio-thread version: fills a caller-owned buffer, allocates nothing.
        // Returns how many entries were written.
        int processOrderInto(ModuleId* dest, int maxLen) const noexcept;

        // Which module's CV drives `m`, if any. (Only the LFO emits CV today, but the
        // graph doesn't hard-code that — it reads the cables.)
        std::vector<ModuleId> cvSourcesFor(ModuleId m) const;

        // Audio-thread version: is `m` driven by a NON-bypassed CV source? That's the only
        // question the engine actually asks, and asking it directly allocates nothing.
        bool hasLiveCV(ModuleId m) const noexcept;

        // A cheap value that changes whenever the patch changes, so the engine can tell
        // when its cached process order is stale without comparing whole graphs.
        std::size_t topologyHash() const noexcept;

        // --- state (a patch is part of the song, so it must survive save/load) ---
        juce::String toString() const;
        static RackGraph fromString(juce::StringRef s);

    private:
        std::vector<Cable> cables;
        std::array<bool, numRackModules> bypassed {};

        // dir==fwd: everything audio can reach starting at src.
        // dir==back: everything that can reach out.  CV cables are skipped — CV is control,
        // not audio, and treating it as audio makes the LFO look like a signal path.
        std::array<bool, numRackModules> reach(bool forward) const noexcept;
    };
}
