# Hardware UI Design – Circle of Fifths Controller

## Overview
This document defines a minimal, coherent hardware UI using:
- 1x Rotary Encoder (with push button)
- 1x Potentiometer

The goal is to control harmonic content (keys, progressions, modes) in a way that aligns with music theory and user intuition.

---

## Hardware Overview

- **SSD1327 OLED** — 1.5" 128x128 4-bit grayscale SPI display for menu and status
- **Alps Alpine EC11E18244AU** — primary input, incremental rotary encoder with
  push switch for menu navigation and parameter editing
- **Alps 10K potentiometer** — secondary input for parameter selection

## Core Design Principles

- **Encoder = discrete harmonic movement**
- **Pot = continuous variation (tension / complexity)**
- **Button = context switching (mode layer)**

Avoid overlapping responsibilities between controls.

---

## Control Mapping

### 🎛 Rotary Encoder → Key (Circle of Fifths)

Each detent moves one step around the circle:

- Clockwise → +1 fifth (C → G → D → A…)
- Counterclockwise → −1 fifth (C → F → Bb → Eb…)

#### Controls:
- Global key center
- Key signature
- Diatonic chord set

#### Rationale:
- Matches the structure of the circle of fifths
- Produces musically valid transitions
- Keeps harmonic context consistent

---

### 🎚 Potentiometer → Harmonic Complexity / Progression Shape

Maps continuously to progression complexity:

| Position | Behaviour |
|--------|----------|
| 0%     | Static (I only) |
| 25%    | Simple (I–V) |
| 50%    | Pop baseline (I–V–vi–IV) |
| 75%    | Circle motion (I–IV–vii°–iii–vi–ii–V) |
| 100%   | Extended / jazz (secondary dominants, ii–V chains) |

#### Controls:
- Number of chords
- Functional movement
- Harmonic tension

#### Rationale:
- Continuous control suits a pot
- Maps naturally to “more vs less movement”
- Avoids arbitrary parameters

---

### 🔘 Encoder Push Button → Mode Switching

Short press cycles between modes.

---

## Modes

### Mode 1: Key Select (Default)

- Encoder → Key (circle of fifths)
- Pot → Progression complexity

---

### Mode 2: Scale / Mode Selection

- Encoder → Select mode:
  - Ionian (Major)
  - Dorian
  - Phrygian
  - Lydian
  - Mixolydian
  - Aeolian (Minor)
  - Locrian

- Pot → Modal intensity:
  - Degree of modal mixture
  - Borrowed chord usage

---

### Mode 3: Playback / Step Control

- Encoder → Step through progression
- Pot → Tempo control

---

## Simplified Variant (Recommended for v1)

If complexity needs to be reduced:

- Encoder → Key (circle)
- Pot → Progression complexity
- Button → Play / Stop OR regenerate progression

#### Rationale:
- Lower implementation complexity
- More robust UX
- Faster iteration

---

## Interaction Enhancements

### Encoder Acceleration

- Slow rotation → single step
- Fast rotation → skip multiple steps

#### Benefit:
- Faster navigation across keys
- Improved responsiveness

---

### Smooth Transitions

- Key changes → transpose existing progression
- Pot changes → morph progression instead of replacing

#### Benefit:
- Feels like an instrument, not a preset selector

---

## Anti-Patterns (Avoid)

### ❌ Encoder = direct chord selection
- Breaks harmonic context
- Confuses users

### ❌ Pot = key selection
- Continuous input unsuitable for discrete pitch

### ❌ Button = major/minor toggle only
- Wastes limited input bandwidth

---

## Mental Model

Think of the system as:

- **Encoder → position in harmonic space**
- **Pot → amount of movement / tension**

If both controls attempt to select discrete states, the UI becomes ambiguous.

---

## Implementation Notes (Optional)

- Represent circle of fifths as a cyclic array
- Store chord sets per key + mode
- Use interpolation or rule-based morphing for progression changes
- Maintain separation between:
  - Key state
  - Mode state
  - Progression generator

---

## Summary

This design:
- Aligns physical controls with music theory
- Minimizes cognitive load
- Scales from simple to advanced usage
- Avoids common UI pitfalls

The result is a compact but expressive harmonic control interface.

