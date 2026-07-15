# Working agreement — Nebula2 VST port

This project ports **Nebula2** (a browser-based breakbeat instrument — chopped-sample
slicer + synthesised drum machine, shared FX grid + modular rack, mixer console) into a
native VST3 instrument plugin, built with **JUCE (C++)**, native UI rebuilt in JUCE
components (Path A — the web UI is not reused; see `reference/` for the prototype and
`docs/VST_WORKFLOW.md` for why).

The full phased roadmap and working agreement this project follows is in
[`docs/VST_WORKFLOW.md`](docs/VST_WORKFLOW.md) — **read it before writing any code.**
That document is the source of truth for process (Part A), the architecture decision
record (Part B — superseded: this project uses Path A, JUCE native UI, not Path B), and
the phase-by-phase roadmap with verification gates (Part C). This file exists so the
agreement travels with the repo; if the two ever disagree, `docs/VST_WORKFLOW.md` is
authoritative and this file should be updated to match.

## The non-negotiables (see docs/VST_WORKFLOW.md Part A for the full list and the failures behind each one)

1. **Verify before claiming done.** Every change ends with a check Claude Code can run
   (compile, unit test, headless render) — paste the result. If a check needs a human
   (does it load in the DAW, does it sound right), print a `MANUAL CHECK` block and wait.
2. **Delete what a replacement supersedes, in the same commit.**
3. **Mock non-trivial UI before building it** — a static mockup, approved, then wired.
4. **Own the pixels.** No platform look-and-feel defaults for anything visual that matters;
   render controls with JUCE's `LookAndFeel` custom-drawn, not native OS widget chrome.
5. **One design system, enforced** — a shared `LookAndFeel_V4` subclass with tokens for
   colour/size/radius/font; two controls doing the same job are the same component.
6. **Small, labelled commits, each behind a green CI gate.** One phase = one branch = one PR.
7. **State is a contract.** Every parameter, from day one: saves/loads with the preset, is
   host-automatable, is MIDI-learnable, enters undo.
8. **DSP correctness is testable without ears** — magnitude/phase response, gain at DC/
   Nyquist, no NaN/denormal, no clip on worst-case input, as unit tests.
9. **Ask when a decision is architectural, not cosmetic** — print a `DECISION NEEDED`
   prompt with options + a recommendation, don't guess.

## Current toolchain status (update as it changes)

- CMake: being installed (2026-07-15).
- Visual Studio Build Tools (C++ desktop workload): being installed (2026-07-15).
- GitHub remote: `https://github.com/Joenebula/nebula2sampz` (private). Phase 0 scaffold
  pushed to `main` (2026-07-15). CI workflow (`.github/workflows/build.yml`) runs on push
  — its first run may still be building JUCE via FetchContent; check Actions for the result.
- Phase 0's AUTO gate ("CI green, plugin binary produced") is NOT yet proven — don't report
  Phase 0 complete until either CI is green or a local build has actually run and its output
  (or failure) has been observed and pasted.

## Reference material

- `reference/nebula2-prototype.html` — the original web prototype this is ported from.
  Verified DSP math to port (not the UI): RBJ biquad EQ, drum modal synthesis, IR reverb
  noise-shaping, offline WAV render, MIDI Type-1 writer.
- `docs/VST_WORKFLOW.md` — the phased roadmap (read first).
