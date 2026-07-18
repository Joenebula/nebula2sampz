# Nebula2 VST

Native VST3 port of the Nebula2 web-audio breakbeat instrument. Built with JUCE + CMake.

Read [`CLAUDE.md`](CLAUDE.md) and [`docs/VST_WORKFLOW.md`](docs/VST_WORKFLOW.md) before
making changes — they carry the working agreement and the phased roadmap this project
follows.

Handing a build to testers? Give them [`docs/BETA_TESTERS.md`](docs/BETA_TESTERS.md) —
note map, what each page does, and the two things they need to know before they start
(the release identity isn't set, and there is one known crash).

## Build

Requires CMake 3.22+ and a C++17 compiler (Visual Studio 2022 Build Tools on Windows).

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The VST3 binary lands under `build/Nebula2_artefacts/Release/VST3/`.

## Status

Phase 0 (repo scaffold, empty plugin) — see `docs/VST_WORKFLOW.md` Part C for progress.
