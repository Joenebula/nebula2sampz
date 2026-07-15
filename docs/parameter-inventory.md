# Nebula2 — Parameter inventory (Phase 1 groundwork)

Extracted 2026-07-15 from `reference/nebula2-prototype.html` to scope the Phase 1
parameter + state spine. This is the complete list of user-controllable parameters that
must become host-automatable plugin parameters and/or state-chunk fields.

**Headline finding:** the prototype's project save (`projectData()`, line 5717) and undo
`snapshot()` (line 5668) persist only a *subset* of state. The **modular rack
(`dialVals`), the Morph pad corner scenes (`padCorners`), and the parametric EQ (`EQB`)
are NOT written to the project file** and are NOT restored by `applyProject()`
(lines 5773–5814). In-session they are live/automatable; they persist only via separate
rack-patch/localStorage/perf-snapshot mechanisms. For the VST, this save shape is **not**
authoritative — everything below belongs in the plugin's state chunk, and the continuous
values should be host-automatable. See Section C decision 1.

Two backbone maps in the prototype:
- `CONTROL_DEFAULTS` (line 9100) — 21 continuous "double-click-to-reset" sliders only.
- `MIDI_SLIDERS` (line 9035) — 30 host-mappable pill sliders (adds the 8 Morph knobs +
  padGlide/padSize; excludes rack dials, which have their own learn path).

---

## Section A — Flat parameters

### A1. Transport / Global — markup 1060, 1149, 1162–1165; defaults 9101; persist 5717/5775

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `bpm` | Tempo | 40 | 220 | 0.5 BPM | 120 | — | var `playBpm`. Tap/½/2× write here (9150). In a plugin, host tempo normally drives this. |
| `swing` | Swing | 0 | 0.6 | 0.02 | 0 | — | Stored 0–0.6; displayed as %. |
| `fade` | Declick | 0.5 | 20 | 0.1 ms | 3 | — | var `fadeMs`. |
| `master` | Master vol | 0 | 1 | 0.01 | 0.9 | — | var `masterVol`. |
| `sens` | Sensitivity | 0 | 1 | 0.02 | 0.5 | — | Transient detect; var `sensitivity`. |
| `pitchFollow` | Pitch follow | 0 | 100 | 1 % | 0 | — | — |
| `limiterOn` | Limiter | — | — | bool | true | — | migrate default true (2170). |
| `division` | Grid division | — | — | enum | 16 | 8, 16, 32 | Segmented `divSeg` (1145). |
| `bars` | Bars | — | — | int stepper | 1 | (1,2,4,…) | No slider; range undeclared — see C5. |
| `sliceMode` | Slice mode | — | — | enum | "grid" | grid, transient | `data-mode` (1136). |
| `fitMode` | Slice fit | — | — | enum | "gate" | gate, stretch, free | `fitSeg` (1156). |

**Count: 11**

### A2. FX grid amounts (Colour block) — markup 1499–1527; serialized `currentFX()` 6908; setter `setFXValues()` 6892; defaults 9100

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `drive` | Drive | 0 | 100 | 1 % | 0 | — | char-dependent curve (2259). |
| `crush` | Crush | 0 | 100 | 1 % | 0 | — | bit/rate crush (2281). |
| `squeeze` | Squeeze | 0 | 100 | 1 % | 0 | — | compressor. |
| `tone` | Tone | 0 | 100 | 1 % | 100 | — | tilt/filter. |
| `pump` | Pump | 0 | 100 | 1 % | 0 | — | sidechain-style. |
| `width` | Width | 0 | 200 | 1 % | 100 | — | mid/side. |
| `reso` | Resonate | 0 | 100 | 1 % | 0 | — | tuned by `resoKey`/`resoScale`. |
| `shatter` | Shatter | 0 | 100 | 1 % | 0 | — | — |
| `ratchet` | Ratchet | 0 | 100 | 1 % | 0 | — | — |
| `resoKey` | Key | — | — | enum (semitone) | 0 (A) | 0,2,3,5,7,8,10 (A,B,C,D,E,F,G) | `keySeg` (1524). Non-contiguous. |
| `resoScale` | Scale | — | — | enum | "minor" | major, minor, phryg, fifths | `scaleSeg` (1519). |
| `char`/`driveChar` | Drive character | — | — | enum | "tube" | tube, fuzz, fold | `charSeg` (1514). |
| `fxOn` | FX enable | — | — | bool | on | — | — |

**Count: 13**

### A3. Space (reverb / delay / haunt) — markup 1562–1602; serialized `currentSpace()` 6135; setter `setSpace()` 6122

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `revMix` | Reverb mix | 0 | 100 | 1 % | 0 | — | — |
| `revSize` | Reverb size | 0 | 100 | 1 % | 50 | — | — |
| `dlyMix` | Delay mix | 0 | 100 | 1 % | 0 | — | — |
| `dlyFb` | Delay feedback | 0 | 92 | 1 % | 40 | — | Slider max 92 (see C4). |
| `haunt` | Haunt drone | 0 | 100 | 1 % | 0 | — | — |
| `revChar` | Reverb character | — | — | enum | "hall" | room, hall, plate, cathedral, reverse | `revSeg` (1591). |
| `dlySync` | Delay sync | — | — | enum | "1/8" | 1/16, 1/8T, 1/8, 1/8., 1/4 | `syncSeg` (1564). |
| `dlyMode` | Delay mode | — | — | enum | "pingpong" | digital, pingpong, dub, warp | `dlySeg` (1580). |
| `ghost` | Space source | — | — | enum | "all" | all, kick, snare, hat, tonal | `ghostSeg` (1598). |

**Count: 9**

### A4. Morph pad knobs & motion — markup 1436–1465; engine `applyMorph()` 2826, `padApply()` 8726; defaults `MORPH_DEF` 2744

Each knob **edits the currently-selected corner scene** of `padCorners` (a 4×8 matrix,
Section B) and displays the live bilinear blend. Host/MIDI-mappable via `MIDI_SLIDERS`.
See C2 — these do not map cleanly to one automatable value.

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `mCut` | Morph Filter | 120 | 16000 | 10 Hz (log) | 16000 | — | — |
| `mRes` | Morph Reso | 0.5 | 18 | 0.1 | 0.7 | — | engine clamps 0.5–20. |
| `mDrv` | Morph Drive | 0 | 100 | 1 % | 0 | — | — |
| `mSpc` | Morph Space | 0 | 100 | 1 % | 0 | — | pad's own delay+reverb. |
| `mPhs` | Morph Phaser | 0 | 100 | 1 % | 0 | — | — |
| `mFlg` | Morph Flanger | 0 | 100 | 1 % | 0 | — | — |
| `mSht` | Morph Shatter | 0 | 100 | 1 % | 0 | — | tempo-locked gate. |
| `mWid` | Morph Width | 0 | 200 | 1 % | 100 | — | mid/side. |
| `padGlide` | Glide | 0 | 100 | 1 % | 25 | — | var `padGlide` (8690). |
| `padSize` | Size | 0 | 100 | 1 % | 70 | — | motion travel radius. |
| `padMotion` | Motion | — | — | enum | "off" | off, circle, fig8, square, (drift) | confirm full set — C8. |
| `padBars` | Motion period | — | — | enum bars | 2 | 1, 2, 4, 8 | `data-bars` (1461). |
| `padOn` | Pad enable | — | — | bool | false | — | — |
| `padX` / `padY` | Pad position | 0 | 1 | — | — | — | dot position (8688); NOT currently a slider — C2. |

**Count: 13 knobs + padX/padY**

### A5. Envelope / Mod lane (scalar parts) — markup 1247–1254; serialized `currentEnv()` 6906

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `modDepth` | Mod depth | 0 | 100 | 1 % | 100 | — | — |
| `modTarget` | Mod target | — | — | enum | "filter" | filter, pitch, volume, drive, shatter | `<select>` (1247). |
| `ampOn` | Amp env enable | — | — | bool | false | — | `ampShape` array = Section B. |
| `modOn` | Mod lane enable | — | — | bool | false | — | `modLane` array = Section B. |
| `gridOn` | FX grid enable | — | — | bool | false | — | cells = Section B. |

**Count: 5**

### A6. Mixer strips — vars line 2735; serialized `mix:{}` 5719

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `smpVol` | Sample layer vol | 0 | 1.5 | — | 1 | — | no `<input range>`; range from code — C9. |
| `drmVol` | Drum layer vol | 0 | 1.5 | — | 1 | — | — |
| `smpMute` | Sample mute | — | — | bool | false | — | — |

**Count: 3**

### A7. Rack modules — markup 1657–1711 (`data-dial`/`data-min`/`data-max`/`data-val`); state `dialVals` 7162; setter `setRackDial()` 8497

NOT in `CONTROL_DEFAULTS`; NOT in project save. `data-log="1"` = logarithmic dial.

| id | Name | Min | Max | Step/Unit | Default | Enum values | Notes |
|---|---|---|---|---|---|---|---|
| `out.lvl` | Main Out level | 0 | 150 | % | 100 | — | — |
| `flt.cut` | Ladder cutoff | 40 | 14000 | Hz (log) | 6000 | — | — |
| `flt.res` | Ladder reso | 0 | 24 | — | 4 | — | — |
| `flt.type` | Ladder mode | 0 | 2 | 1 | 0 | LP/BP/HP | discrete. |
| `lfo.rate` | LFO rate | 0.05 | 20 | Hz (log) | 1.5 | — | cv source. |
| `lfo.depth` | LFO depth | 0 | 100 | % | 50 | — | — |
| `lfo.shape` | LFO shape | 0 | 3 | 1 | 0 | sine/tri/saw/sqr | discrete. |
| `phs.rate` | Phaser rate | 0.05 | 8 | Hz (log) | 0.5 | — | — |
| `phs.depth` | Phaser depth | 0 | 100 | % | 70 | — | — |
| `phs.fb` | Phaser feedback | 0 | 90 | % | 45 | — | — |
| `phs.mix` | Phaser mix | 0 | 100 | % | 60 | — | — |
| `cho.rate` | Chorus rate | 0.05 | 6 | Hz (log) | 0.8 | — | 3-voice. |
| `cho.depth` | Chorus depth | 0 | 100 | % | 45 | — | — |
| `cho.mix` | Chorus mix | 0 | 100 | % | 50 | — | — |
| `cmb.tune` | Comb tune | 40 | 900 | Hz (log) | 180 | — | — |
| `cmb.fb` | Comb decay | 0 | 97 | % | 80 | — | — |
| `cmb.mix` | Comb mix | 0 | 100 | % | 55 | — | — |
| `fld.drive` | Wavefolder fold | 0 | 100 | % | 35 | — | — |
| `fld.sym` | Wavefolder bias | −50 | 50 | — | 0 | — | — |
| `fld.mix` | Wavefolder mix | 0 | 100 | % | 70 | — | — |
| `vow.morph` | Vowel select | 0 | 4 | — | 0 | A,E,I,O,U | discrete letters. |
| `vow.sharp` | Vowel sharpness | 2 | 24 | — | 9 | — | formant Q. |
| `vow.mix` | Vowel mix | 0 | 100 | % | 70 | — | — |
| `ech.time` | Space echo time | 20 | 1200 | ms (log) | 320 | — | — |
| `ech.fb` | Space echo repeats | 0 | 95 | % | 55 | — | — |
| `ech.wow` | Space echo wow | 0 | 100 | % | 25 | — | — |
| `ech.mix` | Space echo mix | 0 | 100 | % | 45 | — | — |

**Count: 27 dials**

Rack toggles (`data-pwr`, markup 1664–1708) + globals: `pwr:eq`, `pwr:flt`, `pwr:lfo`,
`pwr:phs`, `pwr:cho`, `pwr:cmb`, `pwr:fld`, `pwr:vow`, `pwr:ech` (all bool, default on),
plus `rackMute` (1615) and `rackGridOn` (1633, default false; cells = Section B).
**Count: 11 toggles**

### A8. Parametric EQ bands (rack `eq`) — `EQB` array line 7170

6 bands, each `{on, type, f (20–20000 Hz log), g (−18..+18 dB, EQ_GMAX=18), q}`.
Defaults: f = [35,110,420,1600,5200,9000]; g = 0; q = [0.71,0.71,1.1,1.1,1.1,0.71];
on = [false,true,true,true,true,true]; type = highpass/lowshelf/peaking×3/highshelf.
**If flattened: 6 × (on,f,g,q) ≈ 24 params.** Treated as structured by default — C1.

### A9. Drums (scalar parts) — `dmSerialize()` line 9475

| id | Name | Type | Default | Enum values | Notes |
|---|---|---|---|---|---|
| `dmSteps` | Pattern length | enum | 32 | 16, 32, 64 | — |
| `dmBrush` | Velocity brush | enum | 3 | 1,2,3 | — |
| `dmRoll` | Roll length | enum | 0 | — | — |
| `dmThruFx` | Drums through FX | bool | true | — | — |
| `dmMuted` | Drums muted | bool | false | — | — |
| `dmPatIdx` | Active pattern idx | int | 0 | — | — |

**Count: 6** (per-voice mix + pattern grids are Section B)

---

**Grand total flat params ≈ 98**, +24 more if EQ bands are flattened. The prototype's own
"automatable" subset (`MIDI_SLIDERS`) is only **30** — narrower than the full list, and it
excludes rack dials.

---

## Section B — Structured state (state-chunk, not automatable params)

- **`order`** — step arrangement; each `{s:sliceIndex, pitch, gain, rev, pan}`. Serialized
  `projectData().order` (5717), `snapshot().order` (5668); per-step `aVol`/`aPan` editors
  (markup 1280–1281) write here.
- **`gridRows` cells** — FX grid: 16 rows (`FXROWS`, 2204) × step columns, intensity 0–3.
  Serialized `currentGrid()` (6907) → `projectData().grid.rows`.
- **`ampShape`** — 32-point amp-envelope curve (init 2246). `currentEnv().ampShape` (6906).
- **`modLane`** — per-step modulation values (init 2247). `currentEnv().modLane`.
- **`padCorners`** — Morph pad 4×8 scene matrix (`padSeedVariations()` 8705): corners
  A/B/C/D each `{cut,res,drv,flg,phs,sht,wid,spc}`. **NOT serialized in the project.**
- **`EQB`** — 6 EQ band objects (7170). **NOT in project save/load.**
- **`dialVals`** — rack dial value map (7162). **NOT in project save**; persists only via
  rack patch presets (`rackPreset` 1614) + A/B perf snapshots (`perfSnapshot()` 9007).
- **Rack cables / patch graph** — jack-to-jack connections (`data-jack`, 1655+; applied
  8484–8489). Part of rack patch presets, not the project file.
- **`rackGrid` cells** — rack-grid module-gating matrix (`buildRackGridRows()` 8029).
- **`drums` (`dmSerialize()` 9475)** — `{steps,patIdx,brush,roll,thruFx,muted,
  voices:[{id,sndIdx,level,pan,send,mute,solo}]×8, pat:{voiceId → step array}}`. Serialized
  `projectData().drums` (5718).
- **`midiMap`** — CC → target map (`{"ch:cc" → {kind,id}}`, 9034). Storage key `"midi-map"`
  (`saveMidiMap()` 9039). **NOT in project file.**
- **`mix`** — `{smpVol,smpMute,drmVol}` (5719).
- **`loopIn`/`loopOut`** — loop cue points; serialized 5717.

---

## Section C — Decisions needed before Phase 1

1. **Rack, Morph pad, and EQ are absent from the prototype's project file.** For the VST,
   put all of them in the state chunk, and make the continuous ones host-automatable.
   *(Recommend: yes — the prototype's save shape is not authoritative.)*
2. **Morph knobs aren't single values** — each edits the selected corner of a 4×8 scene
   matrix; the audible value is a bilinear blend of 4 scenes at position (padX,padY).
   Expose as 8×4 scene params + padX/padY (recommend), or as macros. padX/padY are not
   currently sliders.
3. **`swing` unit** — stored 0–0.6, shown as %. Pick the VST's canonical unit.
4. **`dlyFb` max** — slider max 92, no clamp constant. Treat as 92 unless engine says else.
5. **`bars` range** — stepper, no declared max. Needs a max (powers of two implied).
6. **Enum encodings** — `flt.type`, `lfo.shape`, `vow.morph`, `resoKey` (non-contiguous
   semitones), `division`, `dmSteps` are numeric-but-discrete → VST3 `StringListParameter`s.
7. **Rack dials automatable?** The prototype excludes them from `MIDI_SLIDERS`. Recommend
   making them host-automatable in the VST.
8. **`padMotion` full enum** — confirmed off/circle/fig8/square + a drift branch; enumerate
   the full `data-mo` button set before finalizing.
9. **`smpVol`/`drmVol` max** — 0–1.5 from a code comment, not a validated control. Confirm.
10. **Per-voice drum params** (`level`, `pan`, `send`, `sndIdx` 0–49) are structured; decide
    whether any (e.g. 8 drum levels) should be automatable.
