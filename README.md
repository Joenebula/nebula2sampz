# Nebula2 VST

Native VST3 port of the Nebula2 web-audio breakbeat instrument. Built with JUCE + CMake.

Read [`CLAUDE.md`](CLAUDE.md) and [`docs/VST_WORKFLOW.md`](docs/VST_WORKFLOW.md) before
making changes — they carry the working agreement and the phased roadmap this project
follows.

## Build

Requires CMake 3.22+ and a C++17 compiler (Visual Studio 2022 Build Tools on Windows).

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The VST3 binary lands under `build/Nebula2_artefacts/Release/VST3/`.

## Status

Phase 0 (repo scaffold, empty plugin) — see `docs/VST_WORKFLOW.md` Part C for progress.
