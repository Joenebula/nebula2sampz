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
