# Harmony Engine — note generation, modes, and progression sequencer

This document specifies how the Tachyon turns a **harmonic source** into the
**wavetable audio voice** and the **CV/gate outputs**. It replaces the ad-hoc
"CV VCO vs Arp" peer-screens (which let two note sources fight over the
oscillator) with one layered pipeline.

Source references / modules this builds on:
- `Core/IO/wt_osc.c` — single-cycle wavetable oscillator (the audio voice)
- `Core/Music/arp.c` — arpeggiator (step generator)
- `Core/UI/circle_of_fifths.c` — the key wheel rendering + geometry
- `Core/IO/cv_out.c`, `gate-output.md` — `CV-OUT-A`, `GATE-A/B` (1 V/oct + gates)
- `Core/IO/clock_in.c`, `clock-input.md` — external clock / internal tempo
- `Core/IO/analog_in.c`, `user-interface.md` §3 — CV-IN-A/B, panel pot, encoder

---

## 1. Concept

Everything is one pipeline:

```
   ┌──────────────────────┐     ┌───────────────┐     ┌────────────────────┐
   │   HARMONIC SOURCE    │     │    ENGINE     │     │      OUTPUTS       │
   │  (produces the live  │ ──▶ │ (renders the  │ ──▶ │  audio wavetable   │
   │  root / key / chord) │     │  harmony)     │     │  CV-OUT + gates    │
   └──────────────────────┘     └───────────────┘     └────────────────────┘
        CV-IN                       Direct                 1 V/oct CV
        Key wheel  (+ seq)          Arp                    gate per step
        Chord wheel(+ seq)          Chord (poly, future)
```

- Exactly **one** harmonic source is active at a time (this is what makes the
  modes mutually exclusive — the old bug was the arp running *globally* and
  colliding with the CV VCO).
- The **engine** decides how the current harmony becomes sound and CV.
- **Mode = (source × engine)** and is **persistent**: a selected mode keeps
  running while you visit config screens (Audio Cfg, Arp Cfg), like an
  instrument — it is not bound to whichever screen is on the OLED.

---

## 2. Mode matrix (source × engine)

| Source \ Engine | **Direct** | **Arp** | **Chord** (future) |
|---|---|---|---|
| **CV-IN**     | **#1** CV VCO — note @ CV pitch, note passed to CV-OUT | **#2** CV root → arp → audio + CV-OUT | — |
| **Key wheel** | "play the wheel note" (freebie) | **#3** wheel → arp → audio + CV-OUT | — |
| **Chord wheel** | — | (arp the chord's notes) | **#4** chord on audio, arp on CV-OUT |

The four numbered cells are the user's four intended uses. `Key·Direct` falls
out for free; the empty cells are not required initially.

**CV-OUT / gate rule:**
- **Direct** → emit the (quantized) root on CV-OUT, gate on note change.
- **Arp** → emit each arp step on CV-OUT + gate per step (today's `arp_step_cb`).
- **Chord** → audio plays the chord (poly), arp drives CV-OUT + gate.

---

## 3. Harmonic sources

### 3.1 CV-IN
- Read `CV-IN-A` or `CV-IN-B` (short-press toggles; see CV VCO screen), 1 V/oct.
- **Quantize to nearest semitone** (chromatic — *not* scale-aware; the key/scale
  is a wheel concept and must not leak into CV mode). Hysteresis to avoid
  flutter at step boundaries. 0 V = C3.
- Produces a live **root note**. In `Direct` it is the played note; in `Arp` it
  is the arp's root.

### 3.2 Key wheel
- Circle-of-fifths selection (existing `circle_of_fifths.c`), with a
  **major / minor** option per key.
- Either a **single key**, or a **key sequence** (see §5): an ordered list of
  steps `{key, bars}` — e.g. `Am·2  Em·2  Bm·2` — advanced by the clock.
- Produces the current **key** (root + major/minor). The engine arpeggiates /
  plays the key's chord (default triad of the key root).

### 3.3 Chord wheel  *(alternative to the key wheel)*
- You first pick a key (root + major/minor) on the key wheel; the chord wheel
  then presents that key's **seven diatonic triads** (§4). Turning the wheel
  selects among them.
- Either a **single chord**, or a **chord sequence** (§5): steps `{chord, bars}`
  — e.g. `C·2  Dm·2  G·2` — advanced by the clock.
- Key-sequence and chord-sequence are **alternatives** — you sequence *keys*, or
  you sequence *chords within one key*, never both stacked.

---

## 4. Music theory (triads only, to start)

Diatonic triads of a key, in scale-degree order:

| Degree | Major key | Natural-minor key |
|---|---|---|
| I / i     | major | minor |
| ii / ii°  | minor | dim |
| iii / III | minor | major |
| IV / iv   | major | minor |
| V / v     | major | minor |
| vi / VI   | minor | major |
| vii° / VII| dim   | major |

- Major and minor are **relative** (C major ↔ A minor share notes); the
  major/minor option selects which tonic the wheel is centred on.
- **Chord-wheel layout (decision):** the seven diatonic triads of a key occupy
  **seven adjacent positions on the circle of fifths** (IV–I–V–ii–vi–iii–vii°
  for a major key). Reusing the existing wheel geometry and highlighting those
  seven positions is the proposed layout — musically natural and reuses
  `circle_of_fifths.c`. (Alternative: lay them out by scale degree I…vii°.)
- A chord = root note + a triad-interval set `{0, third, fifth}` (third = 3 or 4
  semitones, fifth = 7; dim = `{0,3,6}`). The engine expands the chord into
  notes for the arp or the poly voice.

---

## 5. Progression sequencer (shared by key-seq and chord-seq)

One mechanism, applied at two harmonic levels.

- A **step list**, each step = `{ harmony item, length in bars }`. Harmony item
  is a key (key-seq) or a diatonic chord degree (chord-seq).
- **Time signature** is chosen from **predefined options** (e.g. 4/4, 3/4, 6/8);
  it defines beats per bar.
- **Clock**: reuse the arp clock — internal BPM or external `CLK-IN`
  (`clock-input.md`). A *bar* = (beats/bar) × (clock pulses/beat). When the
  current step's bars elapse, advance to the next step; loop at the end.
- The arp (when the engine is Arp) free-runs *within* the current step's
  harmony; the sequencer only swaps the harmonic context at bar boundaries.

---

## 6. Engines

- **Direct** — one note (the root) on the wavetable voice; emit it on CV-OUT.
- **Arp** — existing `arp.c`: expand the current key/chord into a note set,
  arpeggiate per the arp params (dir, octave, length, tempo, clock). Each step
  drives the wavetable + CV-OUT + a gate. Today's `arp_step_cb` already does
  this; it gets fed a root/note-set from the active source instead of only the
  key-select root.
- **Chord** *(future)* — play all chord notes at once on the audio voice
  (**requires polyphony**; the oscillator is mono today — a real dependency to
  flag), while the arp drives CV-OUT.

---

## 7. Outputs
- **Audio** — wavetable voice (`wt_osc`), selected/loaded via Audio Cfg.
- **CV-OUT-A** — 1 V/oct, the arp step or the passed-through Direct note
  (`cv-output-dac.md` range/scaling).
- **GATE-A/B** — per arp step (Arp), or per note change (Direct).

---

## 8. UI & controls

Controls: rotary encoder (rotate + short/long press, `user-interface.md` §2)
**and** the panel pot (`user-interface.md` §3). The sequencer editor uses
**both** — the encoder to navigate/select steps and turn the wheel, the pot for
a second continuous axis (proposed: step **length in bars**, or scrubbing the
harmony quickly). Exact split to be finalised while prototyping the editor.

- **Consistent model everywhere:** rotate = move selection, push = act / enter
  edit (toggles flip in place; values bracket-edit) — already applied to Menu,
  Audio Cfg, and Arp.
- **Screens (proposed):** Mode select (sets source × engine), Key wheel, Chord
  wheel, Sequence editor, Arp Cfg, Audio Cfg. Selecting a mode is persistent;
  config screens don't stop it.
- **Mutual exclusivity** falls out of "one active source": the arp no longer
  runs globally — it runs only when the engine is Arp.

---

## 9. Phased build plan

- **P1 — routing core + modes 1–3.** Persistent `mode = (source, engine)`;
  source = CV-IN | Key wheel, engine = Direct | Arp. Fix the exclusivity bug
  (arp runs only in Arp engine), add **CV-IN → arp root** (#2), and **Direct →
  CV-OUT passthrough** (#1). No sequencer yet (single key/chord).
- **P2 — harmony selection.** Key wheel major/minor; chord wheel showing the
  key's diatonic triads (single chord, no sequence).
- **P3 — progression sequencer.** The `{item, bars}` step list with predefined
  time signatures, clocked; key-seq and chord-seq (alternatives).
- **P4 — chord engine (#4) + polyphony.** Poly wavetable voice for chords; arp
  on CV-OUT. (Biggest unknown — depends on a poly oscillator.)

---

## 10. Open questions / to finalise

1. **Chord-wheel layout** — circle-of-fifths adjacency (proposed) vs scale-degree order.
2. **Pot vs encoder split** in the sequence editor (which axis is which).
3. **Bar definition** in clock pulses — exact mapping of beats↔arp steps↔CLK-IN.
4. **Sequence limits** — max steps, max bars/step, edit/insert/delete/reorder UX.
5. **Polyphony** for the Chord engine — voice count, mixing/headroom on the
   mono PCM5102A path (the oscillator and `audio.c` render are mono today).
6. **CV-OUT range/tuning** for very low/high arp notes (clamp to the DAC range).
7. **Default / power-on mode** and whether the last mode is persisted.

---

## 11. Known issues

- **CV-IN flutters between C3 and B2 when the jack is unplugged.** Unplugged,
  the switched jack normals the input to ~0 V, which *should* quantize to a
  steady C3 (midi 48). But the CV-IN is uncalibrated and `cv-input.md` notes a
  zero-offset of ~50 mV referred to the input ≈ **0.6 semitone** — enough to sit
  the idle reading on the **B/C step boundary (midi 47.5)**, where ADC noise
  tips it across. The quantizer's 0.6-semitone hysteresis isn't enough at the
  boundary, and the software average was removed (P1/CV-VCO) to kill the
  glissando, so nothing damps it. **Fix directions:** calibrate the per-channel
  CV-IN zero-offset (store the constant per `cv-input.md`/`calibration.md`) so
  0 V reads exactly midi 48; and/or widen the deadband / add a slow idle filter
  that only engages when the input is near a step centre. Low priority — only
  affects an *unpatched* CV input.
