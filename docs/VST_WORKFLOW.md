# Nebula2 → VST — single build workflow for Claude Code

The one document to point Claude Code at when we start the plugin build. It is two things
at once: a **working agreement** (how Claude Code must operate, so we never repeat the
back-and-forth of the prototype) and a **phased roadmap** with a verification gate on
every phase. Read Part A before writing any code; execute Part C phase by phase; never
skip a gate.

---

## Decision log (read this first — it overrides Part B below)

**2026-07-15 — Architecture: Path A (JUCE, native UI rebuild).** Confirmed by the user
after Part B originally recommended Path B (iPlug2 + WebView, reusing the HTML UI).
Reason for the change: an engineering pass over the actual prototype found the custom
knobs render over *hidden native `<input type=range>` elements that remain the value
model* — meaning the same cross-WebView-engine risk Part A's rule 4 already warns about
(three different WebView engines across Mac/Windows/Linux, not one inconsistent Chromium)
would apply to the *value model itself*, not just paint. Path A avoids that risk entirely
by rebuilding the UI as native JUCE components. Cost accepted: the web UI is not reused;
budget real time for rebuilding every control (see `reference/nebula2-prototype.html` for
the design tokens and control specs to carry over, not the implementation).

**2026-07-15 — Target formats: VST3 only for now.** AU/CLAP/AAX deferred; Phase 7's
validation scope is VST3-only until this is revisited.

**2026-07-15 — Repo: private GitHub remote `Joenebula/nebula2sampz`.** (Superseded the
initial "local-only" call the same day.) Phase 0 scaffold pushed to `main`. CI
(`.github/workflows/build.yml`) runs on push; a local toolchain (CMake + Visual Studio
Build Tools) is being installed in parallel as a second verification path.

**2026-07-16 — Scope: the COMPLETE port, not a polished v1.** The user's word: "until
complete". FX-grid sequencer, Morph pad, modular rack, designed UI — all in.

**2026-07-17 — Phase 9 (Morph pad) and Phase 10 (modular rack) are done and confirmed by
ear.** Split that both follow: continuous controls are host PARAMETERS; structure is
state-chunk data. Morph pad → padX/padY automate, the four scenes are state. Rack → all 33
dials automate, the patch is state. Overrule either freely; both are cheap to change and
expensive once presets depend on them.

**2026-07-17 — Rack cable loops are REFUSED at patch time** (see `Source/dsp/RackGraph.h`
for the full reasoning). Web Audio only tolerated loops by silently inserting a 128-sample
delay; a block-based graph has no defined answer. Comb/Echo/Phaser each carry their own
feedback dial, so the musical idiom survives. ~20 lines to reverse if wanted.

**2026-07-17 — The DAW loads from `%LOCALAPPDATA%\Programs\Common\VST3\`, not Program
Files.** Install there after every build the user is meant to try, and verify with `cmp`
against the fresh build — not by the copy command's exit code. Three merged phases once sat
invisible on disk behind a stale install while the user reported "I dont see the rack?".
Building and committing puts nothing in front of the user; only the install does.

**2026-07-17 — The editor SCROLLS (Viewport) and is resizable.** The panels total ~1330px,
taller than a laptop plugin window. Clipped-off is indistinguishable from not-there — this
was a real defect, found by the user, not cosmetics. Phase 5's designed UI should replace
the scroll with real navigation (tabs/pages), not keep stretching.

**2026-07-17 — RETIRED: the `bpm` / "Tempo" parameter.** Same reason as MIDI-learn and
WAV/MIDI export: it existed because the *browser prototype had no host*. It was created and
published to the DAW and read by **nothing** — every consumer reads `transport.bpm` from
the host playhead, so automating a Tempo lane did precisely nothing. A dead control is
worse than a missing one. If a host ever supplies no tempo, the fix is a fallback in
`Transport.h`, not a parameter pretending to be the clock. There is now a **dead-control
gate** in `Tests/TestMain.cpp`: the full published parameter list is asserted, so adding a
parameter without adding it there fails, which forces the question "what reads this?".

**2026-07-17 — CORRECTION: pluginval never actually passed at strictness 10.** The earlier
claim that it "passed at strictness 10 on both platforms, first try" was **luck, not a
pass**. Re-running it five times found 1–3 failures *per run*, on a random subset of the
bool parameters:

> `Limiter not restored on setStateInformation -- Expected 1, Actual 0.577136`

A bool cannot *be* 0.577. `juce::AudioParameterBool::setValue` stores whatever float it's
handed and `getValue()` hands it straight back; the APVTS tree holds the snapped 0/1, so a
save/restore round-trips 1.0 → 1.0, no change is detected, no listener fires, and the
parameter keeps the stale float. Nothing audible — the DSP reads the tree's atomic, which
is always 0/1 — but a real failure of a real release gate. Fixed with `SnappedBool`
(`Source/Parameters.h`): `getValue()` snaps, two lines, no re-entrancy hack. **Now 5/5 at
strictness 10.**

The lesson generalises past this bug: *a green result from a randomised tool is one sample,
not a pass.* Run it until it's boring. The same shape of mistake as the Phase 3 reverb test
that passed by racing a background loader.

**2026-07-17 — KNOWN GAP: the rack's EQ is gain-only.** The prototype's EQ lets you *move*
each band (frequency, Q, type — there's a whole drag-a-band editor). The port's rack EQ
dials **gain only, on six fixed frequencies** (35/110/420/1600/5200/9000). That's a genuine
reduction from the prototype, not a design decision. `Source/dsp/ParametricEq.{h,cpp}`
already models a movable band correctly and is the head start — it is deliberately **not
wired to anything yet** and is marked as such in its header, so it isn't mistaken for live
code or deleted as dead. Wiring it needs (a) a band editor UI and (b) moving its
`makeBandCoefficients` to `IIR::ArrayCoefficients` first, since the Ptr form allocates.

**2026-07-17 — The audio thread was allocating ~8,300 times a second.** `juce::dsp::IIR::
Coefficients`' factories are `return *new Coefficients(...)` — an allocation dressed as a
maths helper. Six per 32-sample chunk in the rack's phaser, three in the vowel, six in the
EQ, and one in `ColourChain` that ran **every block for every user even with the FX off**.
`RackModules.h` meanwhile claimed *"Real-time safe: every buffer is allocated in prepare(),
nothing here allocates or locks"* — true about buffers, false about the class. **A comment
precisely true about the thing you checked is the easiest kind to be wrong.** Fixed via
`IIR::ArrayCoefficients` (returns by value, assigned in place), coefficient storage primed
in `prepare()`, and the per-block `std::vector`s replaced with a cached process order keyed
on a topology hash. The **RT allocation detector** deferred since Phase 3 now exists in
`Tests/TestMain.cpp` (global `operator new` counting) — deferring it is what let all of this
live. It self-checks, and a mutation confirms it bites: restoring one line gives 1,920
allocations across 20 blocks.

**2026-07-17 — The false-green catalogue.** This project's recurring failure mode is not
broken code; it's **checks that appear to verify and don't**. Every entry below shipped
green for a while:

| The green | Why it was a lie |
|---|---|
| `getCurrentIRSize() > 0` | satisfied by JUCE's 1-sample placeholder IR |
| Phase 3 reverb test | passed by racing a background loader |
| "pluginval passed strictness 10, first try" | one lucky sample of a randomised tool; it failed 1–3 tests *per run* |
| `/utf-8` "fixed" the mojibake | `add_compile_options` applied it to the TEST binary; the shipping plugin never had it |
| "RackModules is real-time safe" | true about *buffers*, false about the class (~8,300 allocs/sec) |
| RT allocation detector on macOS | clang **elided** the self-check's `new`/`delete`; every RT test passed by counting nothing |
| Layout gate `given >= getStringWidthInt` | the test binary's fonts underestimate the plugin's; passed with a visibly truncated control |
| `bpm` parameter | published to the host, read by nothing |

The rules that fall out, in order of how much they'd have saved:

1. **A test that cannot fail is not a test.** Every detector needs a self-check that proves
   it still detects, and a mutation before you trust its green.
2. **Verify the artefact that ships**, not a proxy for it (inspect the `.vst3`'s bytes).
3. **A randomised tool's green is one sample.** Loop it until it's boring.
4. **Platform-independent beats accurate** for a gate — a crude character count survives a
   font substitution; `getStringWidthInt` doesn't.
5. **A comment precisely true about the thing you checked is the easiest kind to be wrong.**
6. **The user's eyes and ears find what none of this does.** Every serious defect here —
   chops cutting off, a dead Count control, an invisible Sens slider, a stale install, an
   unreadable UI — came from them looking, not from assertions.

**2026-07-17 — OPEN, UNDIAGNOSED: an intermittent macOS CRASH in pluginval.**
This is the top open issue — it outranks every remaining feature.

```
libc++abi: terminating due to uncaught exception of type std::bad_function_call
pluginval received Abort trap: 6, exiting immediately
```

What's known:
- **Intermittent.** Runs 1–4 of 5 passed; run 5 aborted. A different CI run on *identical
  code* went 5/5. The 5x loop is the only reason it was seen at all.
- **macOS only so far.** 12/12 clean locally on Windows at strictness 10.
- It appears after *"Restoring default layout"* (a bus-layout change), i.e. around
  `prepareToPlay` / the editor tests — not during audio processing.
- `std::bad_function_call` means an EMPTY `std::function` was invoked. Inspection has not
  found one: the only `std::function`s we own are `ScrollingContent::onPaint`/`onResized`
  (both null-checked), and the JUCE paths involved are either guarded
  (`NullCheckedInvocation::invoke` in `ParameterAttachment`) or default-filled
  (`AudioParameterFloat`/`Bool` fill their text lambdas when the attributes omit them).
- So it may well be in pluginval/JUCE on macOS rather than in our code — **unproven either
  way, and it must not be assumed.**

**Do not guess-fix this.** CI now runs macOS pluginval under `lldb` (`-k "thread backtrace
all"`), and uploads `pv-*.log` on failure, so the next occurrence yields a stack. Get the
stack, then fix the thing the stack names. A crash "fixed" without a stack is a crash that
comes back.

**2026-07-17 — Prototype-divergence audit of the rack DSP.** Compared all 9 modules to the
reference. Comb, echo, vowel, the phaser core, the mix law and the feedback clamps are
faithful. **Fixed:** 11 wrong dial defaults (a "fresh rack" didn't match the prototype), and
the wavefolder CV (folded into drive-amount → barely moved; now a multiplicative pre-gain
like the prototype, ~12× at drive 35 / half LFO). **Left for the user's EAR — changing these
is a judgement I can't make headless:**

- **Chorus wet level.** The port multiplies the summed voices by an extra `*0.5` the
  prototype lacks (~2.6 dB quieter, slightly different pan law). The `*0.5` may be
  deliberate headroom against clipping three summed voices — matching the prototype exactly
  could clip. Try both by ear.
- **Filter resonance.** The port uses a JUCE SVF (`resonance = fltRes * 0.4`, clamped 4.0)
  where the prototype uses a Web Audio biquad lowpass (`Q` up to 24, in dB). The `0.4` is a
  unit reconciliation, not an equivalence, and the prototype's most extreme
  self-oscillation range isn't reachable. Default now matches (4); the *scale* is a guess.
- **Wavefolder oversampling.** Prototype shaper is `4x` oversampled (anti-aliased); the port
  folds per-sample (more aliasing on hard folds). Same curve/fold-count/formula.
- **Minor:** comb/echo damping Q (0.707 vs the prototype's 1.0), LFO triangle phase (free-run
  offset, inaudible), and range differences (`cmb.tune` 20-2000 vs 40-900; `ech.time` has no
  grid-snap). None change default behaviour.
- **Verify yourself:** the phaser's dry tap — in the prototype the dry path may include the
  fed-back signal (`pIn` is the summing node); the port's dry is the clean input. Subtle at
  high feedback.

**2026-07-17 — Divergences in the APPROVED Colour/Space blocks — USER DECISION NEEDED.**
An audit of the older, always-on effects (which the user has heard and approved) found real
differences from the prototype. Unlike the rack (new code, fixed on sight), these change
audio the user already blessed — so they are FLAGGED, not silently changed. Verified by
reading both sources; each line notes what fixing it would do.

- **D1 — Colour chain ORDER differs (most consequential).** Prototype: `drive → squeeze →
  tone → crush → width`. Port: `drive → crush → width → squeeze → tone`. So the port's Tone
  lowpass filters the crush grit (prototype leaves it raw), and the compressor reacts to the
  crushed signal (prototype to the clean one). Fixing = reorder ColourChain (split the
  Saturator's bundled drive/crush/width). Recommend fixing — the port order was an accident
  of bundling, not a design choice — but it WILL change the Colour sound.
- **D4 — Reverb send missing ×1.35** (`SpaceProcessor.cpp:100`): at equal Mix the port's
  reverb is ~2.6 dB quieter than the prototype. Trivial to match.
- **D5 — Delay send missing ×1.1**: ~0.8 dB quieter. Trivial to match.
- **D6 — Delay damping 4200 Hz symmetric** vs the prototype's **5200 Hz on the L→R path
  only**: the port's echoes are darker and damp both directions. Fixing brightens echoes and
  changes the stereo decay.
- **D7 — Ping-pong routing:** prototype feeds the LEFT line only (true one-sided start) and
  pans wet ±0.75; the port feeds each channel its own line with no panning. Different stereo
  image. The larger structural item alongside D1.
- **D3 — Reverb "Size" is a MISSING FEATURE**, not a bug: the port has no size param and
  hard-codes a 2.2 s tail (prototype: 0.25–6.7 s via a Size knob, default ~2.0 s). Same
  class as the other documented omissions (pump/duck, dub & warp delay modes, haunt drone).
- **D2 — the reverse reverb: the port is CORRECT and the prototype was the buggy one** (its
  reverse swelled a *growing* envelope to a net decay). Already the documented fix; no action.

**Faithful, verified identical:** the drive curves (tube/fuzz/fold), crush
(bits/rate/reconstruction), width mid/side, compressor mappings, tone filter, drive
pre/post staging, delay sync→time table, feedback clamp, and all Colour/Space defaults.

**2026-07-17 — Ported the PUMP (tempo-synced duck).** The groove feature the port had
dropped (deferred since Phase 3/4). Per-beat sidechain-style duck: gain slams to
`1 - pump·0.85` on the beat, exponentially recovers to 1 by phase 0.85 — the prototype's
`scheduleDuck` shape. Sits between Tone and crush in the Colour chain (its prototype
placement), sample-accurate off the host ppq, additive and OFF by default. `pump` param +
COLOUR-panel knob. `pumpGain()` is pure and tested; RT-safe; in the dead-control gate.

**2026-07-18 — Delay modes done (Ping-Pong / Dub / Warp) + the D7 stereo topology.** The
user chose this and blessed the sound change. The delay is now the prototype's real
architecture: send feeds the LEFT line only, taps hard-panned ±0.75, damping on the L→R
feedback path only. Ping-Pong therefore sounds different (wider) than the old symmetric
version — that's D7, approved. `dlyMode` param + DELAY-section dropdown. RT-safe across mode
changes. So D6 and D7 are now both resolved.

**Remaining prototype feature omissions (still open, in rough priority):** the rack module
visual screens (EQ curve / vowel formants / fold curve / scope — modules have dials but no
meters); the "haunt" drone; and the movable-band EQ (needs a drag-band UI). The first and
third are UI-heavy — verifiable only via the user's screenshots (see
[[self-checking-verifiers]]).

Everything below this point is the **original planning document**, written before the
above decisions, and is being updated in place as phases complete. Part B's recommendation
of Path B is kept for the record of *why* it was considered, not as current guidance —
follow the decision log above where the two disagree.

---

## Part A — Working agreement (read first, applies to every task)

These rules exist because the web prototype cost us dozens of avoidable round-trips.
Each rule maps to a real failure we hit.

1. **Verify before you claim done. Prompt me when you can't.**
   Every change ends with a check. If the check is something Claude Code *can* run
   (compile, unit test, headless render, byte-parse), run it and paste the result.
   If it is something Claude Code *cannot* observe (does it sound right, does it load
   in the DAW, does the fader feel right), **stop and print a `MANUAL CHECK` block**:
   what to do, what "pass" looks like, and wait. Do not mark the task complete off an
   unverifiable assumption. *(Prototype pain: shipped "fixed" faders three times that
   the browser rendered wrong; only screenshots caught it.)*

2. **When you add a replacement, delete what it replaced — in the same commit.**
   New control supersedes an old one → remove the old element, its CSS/handlers, and
   its state. Grep for the old id/class afterward to prove nothing dangles. *(Pain:
   the header SMP/DRM buttons and BUS meter lived on for many turns after the mixer
   console replaced them.)*

3. **Mock the UI before building it.** For any non-trivial UI change, produce a
   static mockup (image or throwaway HTML) and get a yes before wiring it. One
   approved mock beats five live iterations. *(Pain: "V1 but slimmer" took 6 turns
   because we iterated live instead of agreeing the target first.)*

4. **Own the pixels — don't trust host/browser primitives for anything visual.**
   Native range inputs, `writing-mode`, `appearance:slider-vertical`, canvas
   `roundRect` all silently vary by engine version. For plugin UI, render controls
   yourself (custom draw + pointer math). *(Pain: vertical sliders rendered
   horizontal in the desktop build's older Chromium.)* In JUCE terms: subclass
   `LookAndFeel_V4` and draw every control yourself; don't lean on default component
   painting for anything the user stares at.

5. **One design system, enforced.** Colors, sizes, corner radius, fonts, control
   look = tokens in one place. Two controls doing the same job must be the *same*
   component. Before finishing a UI task, diff it against the token list. *(Pain: M/S
   buttons and meters existed in two styles simultaneously.)*

6. **Keep a canonical file and don't trust lagging mirrors.** The editor/agent view is
   ground truth; sandbox mounts can lag or truncate large files. If a verification
   tool disagrees with the editor on file contents, trust the editor and re-sync
   before acting. *(Pain: a mount showed the 2 MB file as truncated; a "recovery"
   nearly corrupted a good file.)*

7. **Small, labelled commits with a green gate each.** Every phase below is its own
   branch → PR. CI must pass (build + tests) before merge. Never stack an unverified
   change on another.

8. **State is a contract.** Every parameter/control, from day one: saves & loads with
   the preset, is automatable by the host, is MIDI-learnable, enters undo. Adding a
   control means wiring all four — not a later pass.

9. **DSP correctness is testable without ears.** Filters, EQ, dynamics, saturation:
   assert magnitude/phase response, gain at DC/Nyquist, no NaN/denormal, no clip on
   worst-case input, in unit tests. Ears are the *final* check, not the only one.
   *(We proved the parametric EQ curve and crush anti-alias this way in the prototype.)*

10. **Ask when a decision is architectural, not cosmetic.** Framework, threading
    model, how the DSP is shared with the UI — these get a `DECISION NEEDED` prompt
    with options + a recommendation, not a guess.

---

## Part B — The architecture decision (settled — see Decision log above)

The prototype is HTML + Web Audio API. A plugin runs inside a DAW process where Web
Audio does **not** exist — the audio callback is native and real-time. So the DSP must
leave the browser. Three viable paths were considered:

| Path | UI | DSP | Pros | Cons |
|---|---|---|---|---|
| **A. JUCE (C++)** — **chosen** | JUCE components (native, custom-drawn) | C++ | Industry standard, VST3/AU/AAX, best host compatibility, mature, single rendering engine everywhere | DSP rewrite in C++; UI rebuild from scratch |
| B. iPlug2 (C++) | Web UI (HTML/JS) in a WebView, C++ DSP | C++ | Keeps the HTML/CSS/JS UI, VST3/AU/CLAP/Web, lighter than JUCE | Smaller community; JS↔C++ param bridge to build; **the prototype's custom knobs sit on hidden native `<input type=range>` elements as their value model — this risk multiplies across 3 different WebView engines (WKWebView/WebView2/CEF), not 1 inconsistent Chromium** |
| C. Elementary / WASM-in-native | Web UI | JS/WASM DSP in native host | Reuse JS DSP | Real-time safety of a JS/WASM engine in the audio thread is the risk |

Reusable assets from the prototype regardless of path: the **DSP math is already
verified** (RBJ biquad coefficients + magnitude eval, crush decimator + reconstruction
LP, modal drum synthesis, offline render math, MIDI Type-1 writer). Port these as
reference implementations; keep the unit tests. The UI is **not** reused under Path A —
carry over the design tokens and control specs, not the DOM/CSS implementation.

---

## Part C — Phased roadmap (each phase: deliverable → gate)

Gate legend: **AUTO** = Claude Code runs it. **MANUAL** = print a `MANUAL CHECK` block and wait for me.

### Phase 0 — Repo, toolchain, CI, decision
- Deliver: repo scaffold (JUCE + CMake), builds an empty plugin that loads; CI on
  GitHub Actions (build all targets); `CLAUDE.md` seeded with Part A.
- Gate **AUTO**: local build succeeds OR CI green, VST3 binary produced. **MANUAL**: load
  the empty plugin in your DAW; confirm it instantiates and makes no sound. (Claude Code
  can't open a DAW.)
- Status (2026-07-15): scaffold written and pushed to `Joenebula/nebula2sampz` `main`.
  Toolchain (CMake, VS Build Tools) installing. NOT yet built or verified — do not mark
  this phase done until CI is green or a local build has actually run and its output (or
  failure) is pasted here, AND the MANUAL DAW check has passed.

### Phase 1 — Parameter + state spine  ✅ AUTO gate met (2026-07-15)
- Deliver: parameter registry (id, range, default, unit, smoothing), preset save/load,
  host automation exposure, undo. No DSP yet — a couple of test params.
- Gate **AUTO**: round-trip test (set → save → load → equal); automation write/read test.
  **MANUAL**: automate a test param from the DAW, confirm it moves.
- Status (2026-07-15): DONE and CI-green (Build #7). Implemented as
  `AudioProcessorValueTreeState` (`Source/Parameters.cpp`, `Source/ParameterIDs.h`), state
  save/load via `getStateInformation`/`setStateInformation` (ValueTree ↔ binary), undo via
  an attached `UndoManager`. Verified by `Tests/TestMain.cpp` (a `ctest` step in CI):
  parameter existence, automation write/read, and a save→load→save byte round-trip — all
  passing on Windows + macOS. **MANUAL (pending):** automate a param from the DAW — batch
  this with the Phase 0 DAW load check.
- **Scope decision (2026-07-15):** the spine is seeded with *representative* params (one of
  each shape: linear float, log-skewed float, choice, bool). The remaining ~90 from
  `docs/parameter-inventory.md` are added **per phase, alongside the DSP they drive** — not
  bulk-added now, which would create ~90 inert params controlling nothing (a "looks wired
  but isn't" surface the working agreement forbids). `ParameterIDs.h` is the running
  registry; each Phase 3 effect adds its own params there when its DSP lands.
- Open decisions from the inventory (Section C of `parameter-inventory.md`) — resolve as
  each param group is added: the Morph pad's 4-scene/X-Y model (architectural), enum→
  StringList encodings, and a few range/unit confirmations. Rack dials WILL be
  host-automatable in the VST (the prototype excluded them; we include them).

### Phase 2 — Audio graph skeleton + transport  ✅ AUTO gate met, one item deferred (2026-07-15)
- Deliver: real-time-safe audio callback, sample+drum layer buses, master limiter,
  transport sync to host tempo/PPQ. No effects yet.
- Gate **AUTO**: no allocations/locks in the callback (thread-sanitizer / RT-check);
  denormal + NaN guard test; null-input silence test. **MANUAL**: pass audio through,
  confirm clean bypass and host-tempo lock.
- Status (2026-07-15): DONE, CI-green (Build #10). `MasterProcessor` (smoothed gain →
  `juce::dsp::Limiter` → brickwall clamp), `Transport.h` (pure host tempo/PPQ read),
  preallocated `sampleBus`/`drumBus` summed to output, lock-free param reads via cached
  atomics. Tests (ctest): master silence/no-clip/finite/mute + transport parse/defaults.
- **DEFERRED (honest gap):** the "no allocations/locks in the callback" RT-detector is
  NOT auto-verified. The callback is allocation-free *by design* (buffers preallocated in
  `prepareToPlay`, no heap ops, no locks), but wiring a reliable allocation-detector test
  needs a local build to iterate on — do it once the toolchain is installed. The
  denormal/NaN guard is currently trivial (no sources yet to produce them); it becomes a
  real test in Phase 3 when the buses carry signal. **MANUAL (pending):** audio
  passthrough + host-tempo lock in a DAW — batch with the other pending DAW checks.
- Note: the prototype's step sequencer is a browser `setInterval(scheduler,25)`
  look-ahead scheduler (100ms window) with an **independent second copy** for the drum
  machine. This needs a full rewrite as a sample-accurate, block-based scheduler driven
  by the native audio callback — it is its own porting task, not covered by "port the
  DSP" in Phase 3.

### Phase 3 — Port the DSP (reuse verified math)  ✅ DSP porting complete (2026-07-16)
- Status: all DSP modules ported to `Source/dsp/` and CI-green on `main`, 92 behaviour
  assertions in `Tests/TestMain.cpp`: ParametricEq (RBJ magnitude), Saturator (drive
  curves + oversampling, crush ZOH+recon-LP, width), Colour (compressor GR, tone tilt),
  Delay (echo timing, ping-pong cross-feed), Reverb (seeded IR + convolution engine),
  DrumSynth (all 8 modal voices), TempoDetect (the BB140/077 regression), Slicer
  (zero-snap, grid + transient slicing, tempo ratio).
- **Deliberate deviations from the prototype** (both documented in code):
  1. Reverse reverb now genuinely swells — the prototype grew-then-reversed, which
     cancelled to a plain decay (its "reverse" never reversed).
  2. Reverb IR RNG is **seeded** — the prototype used `Math.random()`, so its reverb was
     not reproducible across renders/presets.
  3. Drive is **oversampled 2x** (the browser WaveShaper aliased).
- **Since assembled (Phase 4):** all of the above is now wired into the audio graph with
  live params. The drive pre/post gain staging is restored; the granular OLA stretch is in.
  Still deferred: the "pump" tempo-synced duck (belongs with the scheduler, not a static
  block).

### Phase 4 — MIDI: input, learn, mapping  ✅ done, with one deliverable retired (2026-07-16)
- Delivered: MIDI note input → drum voices (GM map from the prototype's own exporter) and
  → sample slices (note 84 / C5 upward, clear of the drum notes). Sample-accurate: both
  layers render in sub-blocks split at MIDI event positions. Note-off gates chops (a
  slicer behaviour); drums stay one-shot.
- **RETIRED: "MIDI learn (right-click a control → arm → next CC binds)".** That deliverable
  exists because the *browser prototype had no host*. A VST3 gets this from the DAW —
  Cubase's Quick Controls / Remote Control Editor map any CC to any exposed parameter, and
  every parameter here is properly exposed (they already appear in automation lanes).
  Building our own would duplicate the host, worse, and fight it. Revisit only if a target
  host turns out to lack CC mapping.
- **Bugs found by ear that the tests missed** (now regression-covered): chops stopped ~3/4
  through (grain read wrapped mid-grain back to the slice start); slices ignored note
  length and stole their own voices. 148 green assertions had said nothing about either —
  a standing reminder that "finite, bounded, deterministic" is a long way from "correct".

### Phase 3 (original plan text)
- Order: sample slicer/player (note: prototype uses a real granular/time-stretch
  engine — pitch/reverse/gain/pan/jitter/scatter/detune — not plain buffer playback,
  scope accordingly) → drum engine (modal voices) → EQ (parametric) →
  saturator (drive/crush/fold, oversampled) → compressor + sidechain → reverb/delay.
- **The prototype's actual DSP surface is larger than this list**: FX-grid chain
  (drive/crush/width/squeeze/tone), a bandpass resonator bank, a "Morph" section
  (flanger/phaser/shatter-gate/its own reverb send), and a **modular rack** with a real
  drag-connectable cable graph (parametric EQ, ladder filter, 6-stage phaser, 3-voice
  chorus, comb resonator, wavefolder, formant filter, "space echo", CV-driven LFOs) —
  roughly 20+ distinct modules total, not 6. Re-scope phase estimates against the full
  list, not this summary.
- Each effect: port math, port the unit tests, expose params.
- Gate **AUTO per effect**: response/curve assertions (EQ mag, filter slope, comp GR),
  anti-alias band check on crush, no-clip worst-case, offline render matches reference
  within tolerance. **MANUAL**: A/B listen vs the prototype on the same loop.

### Phase 4 — MIDI: input, learn, mapping
- Deliver: MIDI note input → drum voices & sample slices; **MIDI learn** (right-click any
  control → arm → next CC binds; persisted per-preset and globally); MPE/pitch optional.
- Gate **AUTO**: learn/bind/persist round-trip test; CC→param scaling test.
  **MANUAL**: hardware/controller CC moves the mapped control in the DAW.
- Note: the prototype binds directly to hardware via `navigator.requestMIDIAccess()` —
  wrong model for a plugin (the host routes MIDI in, the plugin doesn't open devices
  itself). This is a rebuild against the host's MIDI input, not a port of the existing
  learn code.

### Phase 5 — UI in JUCE
- Deliver: native JUCE UI, custom `LookAndFeel_V4`-drawn controls carrying over the
  prototype's design tokens (not its DOM/CSS), resizable, DPI-correct; **all controls
  custom-drawn** (rule 4).
- Gate **AUTO**: param↔UI binding round-trip test; headless/offscreen render of each
  view (no NaN sizes, no missing bindings). **MANUAL**: open the editor in the DAW; drag
  every control, confirm it moves the sound and the host param.

### Phase 6 — Presets, factory content, export
- Deliver: factory presets, user preset browser, pattern/kit content, WAV/MIDI export
  of the arrangement.
- Gate **AUTO**: preset load applies fully (diff state before/after); export byte-valid
  (parse WAV header + MIDI chunks, as verified in the prototype). **MANUAL**: presets
  recall by ear; exported files open in a DAW.
- Note: the prototype has ~10 features using unseeded `Math.random()` (reverb IR noise,
  grid "chaos" fill, rack randomizer, ratchet probability, pad scatter) vs. seeded
  `mulberry32` for drum-voice variation. Most already bake their *result* into saved
  state, so recall is fine as-is; decide explicitly (not by default) whether any of
  these need to be exactly reproducible under host automation/bounce, or are accepted
  as live-performance randomness.

### Phase 7 — Validation, formats, hardening
- Deliver: pass **pluginval** (strictness 10) for VST3; parameter/threading
  fuzz; CPU budget + Eco/HQ quality toggle; installer/signing notes.
- Gate **AUTO**: pluginval green in CI; CPU under budget on the reference project.
  **MANUAL**: run in 2–3 DAWs (Ableton/Logic/Reaper), sample-rate + buffer-size sweep,
  save/close/reopen a project, confirm full recall.

---

## Part D — Definition of done (per task and per phase)

A task is done only when: it compiles, its AUTO checks pass and are pasted, any
superseded code is removed and grep-clean, new controls save/automate/MIDI-learn/undo,
and either the MANUAL check passed or a `MANUAL CHECK` block is printed and pending.
A phase is done only when its branch is merged behind green CI.

## Part E — What to reuse from the prototype (so we don't rebuild)
- Verified DSP math + its unit tests (EQ, crush AA, drum synth, offline render, MIDI writer)
  — confirmed present and working in `reference/nebula2-prototype.html`: `OfflineAudioContext`
  render pipeline (WAV export), a real Standard MIDI File writer (MThd/MTrk byte headers,
  GM drum map), RBJ biquad coefficients, modal drum synthesis, IR noise-shaping reverb.
- The design tokens + the canonical strip spec (fader/meter/knob/M-S) — one style. Reuse
  the *spec*, not the DOM/CSS, under Path A.
- The preset-modal UX, the mixer-console layout, the drums-through-shared-FX routing model.
- The verification habits above — they are the real product of the prototype.
