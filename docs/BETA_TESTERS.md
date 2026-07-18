# Nebula2 — beta tester guide

A breakbeat instrument: load a loop, it chops it, then you wreck it.

Install `Nebula2.vst3` into your VST3 folder and rescan. Windows:
`C:\Users\<you>\AppData\Local\Programs\Common\VST3\` (or `C:\Program Files\Common Files\VST3\`).

---

## Read this bit first

**Don't build a track you want to keep yet.** The plugin's release identity isn't set, and
when it is, its VST3 ID changes. Projects saved against this build will then show Nebula2 as
a **missing plugin** — not corrupted, just unfound, and that instance's settings go with it.
Experiment freely; don't commit a session to it.

**Known crash.** Roughly 1 run in 45 under a validator that hammers the plugin with repeated
audio-setup changes. In normal playing you shouldn't see it. If you get a crash **when
changing your audio device, sample rate or buffer size**, that's probably this one — say so
and don't worry about reproducing it precisely. Anything else, please do report exactly what
you did.

---

## Making a sound in 30 seconds

1. **SAMPLE** tab → **Load Sample…** → pick a breakbeat. (Or drag a file onto the plugin.)
2. Press **▶ Play** in the header. That's an in-app audition — it doesn't start your DAW.
   The moment your DAW's transport rolls, the DAW takes over.
3. Turn something on the COLOUR panel.

### The note map

| Notes | What they play |
|---|---|
| **B4** (83) | the whole break |
| **C5** (84) and up | slice 1, 2, 3… |

Chops are gated by note length — hold a 16th, get a 16th. That's what makes it a slicer
rather than a one-shot player.

---

## The pages

**SAMPLE** — load, slice, rearrange, and the Colour and Space blocks.

- **Slice**: Grid = equal chops, Transient = chop where the drums actually hit (Sens sets
  how eager it is). **Count** is how many.
- **Shuffle** rearranges which slice each pad plays. **Suggest** does the same but listens
  first — it works out which chops are kicks, snares and hats and puts drums where drums
  belong. **Reset Order** puts it back.
- The numbers on the waveform are the **pad** that plays each slice. They scramble when you
  shuffle; teal means that slice has moved. A dot means you've shaped that slice.
- **Click a chop** to select it, then set its Level, Pan, Pitch or Reverse. **Shape** applies
  one amplitude curve to every chop as it fires — try **Punch**.

**GRID** — 16 lanes of step-sequenced effects, plus a **Note** strip on top for melody.

- A lane's knob (on SAMPLE) sets the **ceiling**; the painted cell scales it. A lane whose
  knob is at rest shows greyed with its value, because it genuinely can't do anything —
  turn its knob up first.
- **Randomise** rolls a pattern at Low/Mid/High density, and only uses lanes that can
  actually sound. **Pattern** has 27 factory patterns and anything you've saved.
- The **Note** strip: drag to draw a melody. Pitches snap to the Key and Scale set by
  Resonate on the SAMPLE page.

**MORPH** — a pad that blends four scenes of its own effect chain (filter, drive, flanger,
phaser, shatter, width). It does **not** touch your Colour knobs. **New Scenes** rolls four
fresh corners — that's also the only way to see what it's blending between.

**RACK** — patch modules together. **Randomise** builds a chain that's guaranteed to make
sound.

---

## Things worth knowing

- **Right-click any knob** for MIDI learn, clear, or reset to default.
- **Undo / Redo** in the header cover the grid, notes, slice arrangement, rack patch and
  morph scenes — the things the dice rewrite. Knobs use your DAW's own undo.
- Grid patterns you save go to `Documents/Nebula2/Grid Patterns` and are available in every
  project.
- If something looks like it should make a sound and doesn't, the plugin will usually tell
  you why in red. If it doesn't, that's a bug worth reporting.

---

## What's most useful to report

In rough order:

1. **Anything that sounds wrong** — clicks, dropouts, a control that does nothing, a chop
   cutting off early.
2. **Anything that looks like it works and doesn't.** This has been the recurring bug class
   in this project: controls that were real, tested, and unreachable.
3. **Crashes** — what you were doing, and whether you'd just changed audio settings.
4. **Anything you expected to find and couldn't.**

Rough edges in the layout are known — the UI is functional, not finished.
