# PCB Design -- Stackup and Layout Strategy

This document defines the PCB stackup and the mixed-signal layout
strategy for the Tachyon Eurorack module. It is the authoritative
source for:

- Layer count and stackup
- Ground plane topology (one plane, no splits)
- Power pour regions
- Component placement zones
- Signal routing guidance

Rail definitions, part selections, and per-IC decoupling live in
`power-supply.md` and `audio-output-dac.md`; this document describes how
those rails and ICs are arranged on the physical board.

**Naming convention:** "Layer 1 / Layer 2 / Layer 3 / Layer 4" refers to
the PCB stackup layers. Bare designators `L1`, `L2`, `L3` refer to
**component** reference designators (the two input ferrites and the buck
inductor, respectively, per `power-supply.md`).

---

## 1. Why 4 layers

A 2-layer board cannot deliver acceptable EMI or analog performance for
this design: the TPS54202 buck switch-node loop, the STM32F405 clock
lines, the I2S traces to the PCM5102A, and the precision reference /
DAC8552 rail all have to coexist within ~50 mm of each other. On 2
layers every fast return current loops through the air as a radiator,
and the precision analog rail shares its reference with the buck return.

A 4-layer board solves this with two devices:

1. **A continuous, unbroken ground plane** directly underneath every
   signal trace, containing HF return currents locally.
2. **A dedicated power pour layer**, so each rail gets its own copper
   region sized to its load, without running as a trace.

**6 layers would be overkill.** None of the signals on this board are
fast enough to need stripline routing, none of the ICs need tighter
power delivery than a 4-layer pour can give, and the cost jump
(roughly 5-6x at prototype quantity from JLCPCB) buys nothing audible
or measurable for this IC lineup. 4-layer is the correct target.

---

## 2. Stackup

Target fab: JLCPCB or equivalent, standard 1.6 mm 4-layer FR4, 1 oz
copper on all layers.

```
┌──────────────────────────────────────────────────┐
│ Layer 1  TOP     -- components + signal routing  │   0.035 mm Cu
├──────────────────────────────────────────────────┤
│         prepreg    ~0.2 mm                       │
├──────────────────────────────────────────────────┤
│ Layer 2  GND     -- solid, continuous ground     │   0.035 mm Cu
├──────────────────────────────────────────────────┤
│         core       ~1.065 mm                     │
├──────────────────────────────────────────────────┤
│ Layer 3  PWR     -- power pour regions           │   0.035 mm Cu
├──────────────────────────────────────────────────┤
│         prepreg    ~0.2 mm                       │
├──────────────────────────────────────────────────┤
│ Layer 4  BOTTOM  -- components + signal routing  │   0.035 mm Cu
└──────────────────────────────────────────────────┘
                  total ~1.6 mm
```

Notes on the stackup:

- **Layer 1 - Layer 2 separation (~0.2 mm)** is deliberately much thinner
  than the core. Layer 1 signals are referenced tightly to the Layer 2
  ground plane with low loop inductance. This is where the fast / noisy
  signals should route (buck switch node loop, I2S, SPI).
- **Layer 2 and Layer 3 are separated by the core (~1.065 mm)**. This is
  fine -- the distance between GND and PWR planes is not performance-
  critical here; bulk decoupling comes from the explicit ceramic caps,
  not from plane capacitance.
- **Layer 4 - Layer 3 separation (~0.2 mm)** mirrors Layer 1 - Layer 2,
  so bottom-side signals are referenced to the Layer 3 power pour
  regions. This is acceptable for slow signals (encoders, front-panel
  wiring, OLED cable, CV jacks) but **not** for fast signals -- those
  belong on Layer 1 over the GND plane.
- Fab will set the exact prepreg/core thicknesses to hit 1.6 mm; the
  ratios above are the standard JLCPCB "JLC04161H-7628" stackup. Do
  not request a custom stackup unless there is a specific reason.

---

## 3. Ground plane (Layer 2)

**Rule 1: Layer 2 is one single, continuous copper pour. It is never
split, cut, moated, or partitioned.**

This is the single most important layout rule on the board. The old
mixed-signal dogma of "split AGND from DGND and join at a star point"
is wrong for modern layouts and has been repeatedly debunked (Henry
Ott, Howard Johnson, Rick Hartley). A split plane forces return
currents to detour around the cut, which *increases* loop area and
radiated EMI rather than reducing it.

**Why one plane works:** HF return current does not take the path of
least resistance -- it takes the path of least inductance, which is
directly underneath the signal trace on the adjacent plane. This means
a continuous plane automatically separates return currents by
placement: the buck's return current stays under the buck, the audio
DAC's return current stays under the audio DAC, and they do not mix
even though the copper is electrically one net.

**Consequences:**

- There is no "AGND net" and no "DGND net" in the schematic. Everything
  is `GND`.
- There is no star-point tie between analog and digital grounds. There
  is nothing to star.
- The `power-supply.md` rule about "tie AGND at the LDO output cap" is
  **obsolete** with this stackup -- one continuous plane handles it.
- Partitioning is done by **component placement on Layer 1**, not by
  copper topology on Layer 2. See Section 5.

**Exceptions:** none. Do not cut Layer 2. If you think you need to cut
Layer 2, the answer is to move a component instead.

**Stitching vias:** place generous GND-to-GND stitching vias between
Layer 1 / Layer 4 ground pours and Layer 2, especially:

- Around the TPS54202 package (≥ 6 vias on its GND pad / thermal pad)
- Under the PCM5102A and DAC8552 GND pins
- Along the edge of the board (a via fence ~2 mm inside the board
  outline, ~5 mm pitch, for return-current containment at the edge)
- On the ground return of every decoupling cap (one via per cap, as
  close to the GND pad as possible)

---

## 4. Power plane (Layer 3)

Layer 3 is **not** a single power plane -- it is a set of **poured
regions**, each dedicated to one rail. The regions are placed directly
underneath the ICs they feed, so power delivery is vertical (through
vias) rather than horizontal (through traces).

### Regions

| Region      | Rail          | Footprint under...                                   |
|-------------|---------------|------------------------------------------------------|
| `+5V`       | Buck output   | From TPS54202 output to WeAct VIN and both LDO VINs                  |
| `+3V3`      | WeAct 3V3 out | STM32F405, OLED connector, 74AHCT1G125, PCM5102A DVDD|
| `+3V3_PREC` | U14 out       | DAC8552 VDD and REF5025 VIN                          |
| `+3V3_AUDIO`| U15 out       | PCM5102A AVDD pin 18 and CPVDD pin 12                |
| `+12V`      | P1 (post D1 / L1 ferrite) | OPA1642 V+ (U7 pin 8)                    |
| `-12V`      | P1 (post D2 / L2 ferrite) | OPA1642 V- (U7 pin 4)                    |

### Pour rules

- **Each region is a solid pour**, not a trace. Use polygon pours, not
  wide tracks.
- **Regions do not overlap.** The boundaries are determined by the
  placement of the ICs each region feeds -- if two ICs on different
  rails are adjacent, the boundary runs between them.
- **No region may cross under a sensitive IC that belongs to a different
  rail.** Specifically: `+5V` (noisy, carries buck output ripple) must
  not pass under the PCM5102A, REF5025, or DAC8552. The `+3V3_AUDIO` and
  `+3V3_PREC` regions sit there instead.
- **Gap between regions:** 0.5 mm minimum, to avoid fab over-etch
  joining two rails. The gap is not a slot in the *ground* plane
  (Layer 2) -- Layer 2 remains continuous beneath the power-region
  boundary.
- **Delivery vias:** each IC power pin gets its own via from Layer 1
  pad to Layer 3 region, placed as close to the pin as possible. Do not
  share one via between multiple power pins.
- **Bulk caps go to the same vias.** The decoupling cap for a given IC
  sits next to that IC on Layer 1 and shares the same local via stack
  down to Layer 3 (power) and Layer 2 (ground).

### What about the inter-region gaps on Layer 3?

The gaps between power regions on Layer 3 are **thin slivers of bare
FR4** (no copper). Signals on Layer 4 that cross these gaps would see a
return-path discontinuity on Layer 3 -- but this is not a problem in
practice because:

1. Layer 4 signals are referenced primarily to Layer 3 for slow
   signals, but the fast signals (I2S, SPI, buck switch loop) are on
   Layer 1 over Layer 2 (which is continuous and has no gaps).
2. Slow signals (encoders, CV jacks, OLED ribbon) have edge rates too
   low to care about return-path discontinuities on the other reference
   plane.

If you find yourself needing to route a fast signal on Layer 4 across
a power-region boundary, move it to Layer 1 instead.

---

## 5. Component placement zones

Partitioning is enforced by **where components sit on Layer 1**, not by
cutting copper. Divide the board into three conceptual zones:

```
┌────────────────────────────────┬───────────────────────────┐
│                                │                           │
│  ZONE A: POWER                 │  ZONE B: DIGITAL          │
│  (top-left, near bus header)   │  (top-right)              │
│                                │                           │
│  P1, D1, D2, U12 + L3 (buck    │  WeAct STM32F405 module   │
│    inductor)                   │  OLED connector           │
│  C5 / C8                       │  74AHCT1G125 buffer       │
│  R7 / R8, C7                   │  USB (if exposed)         │
│  F1 / F2, L1 / L2 ferrites     │  encoders / buttons       │
│  input bulk caps C1 / C2       │                           │
│  C3 / C4 HF bypass             │                           │
│                                │                           │
├────────────────────────────────┴───────────────────────────┤
│                                                            │
│  ZONE C: ANALOG                                            │
│  (bottom, far from the buck)                               │
│                                                            │
│  TPS7A2033 U14 → +3V3_PREC                                 │
│  TPS7A2033 U15 → +3V3_AUDIO                                │
│  REF5025, DAC8552, OPA1642                                 │
│  PCM5102A                                                  │
│  output jacks (audio + CV)                                 │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

### Placement rules

- **The TPS54202 goes in the corner of Zone A furthest from Zone C.**
  Specifically: maximize the distance from the buck switch node (SW
  pin + L3 inductor loop) to the PCM5102A and REF5025. 30 mm
  minimum is a reasonable target for a Eurorack-sized board.
- **The STM32F405 (WeAct module) sits in Zone B.** Its clock crystal
  and any high-speed peripheral pins should face *toward* Zone B's
  interior, not toward Zone C.
- **The PCM5102A sits at the edge of Zone C closest to the STM32.** I2S
  traces (MCLK, BCK, LRCK, DATA) are the fastest signals in Zone C,
  so they need to be as short as possible to minimize their exposure.
  Route them on Layer 1 with Layer 2 directly underneath as reference.
- **The REF5025 and DAC8552 sit in the opposite corner of Zone C from
  the PCM5102A.** This keeps the precision DC reference as far as
  possible from the I2S clock edges.
- **The OPA1642 (U7) sits between the DAC8552 and the CV output jacks,** so
  its output traces to the jacks are short and its input traces from
  the DAC are short.
- **Both TPS7A2033 LDOs sit at the Zone C border, on the side facing
  Zone A.** They receive +5V from Zone A and deliver `+3V3_PREC` /
  `+3V3_AUDIO` into Zone C. Placing them on the border minimizes the
  length of noisy +5V copper inside Zone C.
- **Decoupling caps:** every IC's decoupling cap must be on the same
  side of the board as the IC, within 2 mm of the power pin it
  decouples, with its GND via directly adjacent to the cap pad.

### The I2S bridge

The I2S signals cross from Zone B to Zone C. This is the one place
on the board where a fast digital signal enters analog territory.
Mitigations:

1. **Route on Layer 1 only.** Layer 2 directly beneath gives a clean
   reference.
2. **Keep the traces short** -- under 30 mm if possible, under 50 mm
   at the absolute maximum.
3. **Series termination** at the source (STM32 or 74AHCT1G125): place
   a 33 Ω resistor on each I2S line within 5 mm of the driver. This
   damps ringing and slows edge rates to reduce radiation.
4. **Ground-flanked routing.** Run a ground pour on Layer 1 on both
   sides of the I2S bundle, stitched to Layer 2 every ~3 mm with vias.
   This turns the traces into a crude coplanar waveguide and contains
   the return current locally.
5. **No other signals in the I2S corridor.** The strip of board
   between Zone B and the PCM5102A is reserved for I2S and ground.

---

## 6. Signal routing guidance

### Where each signal class belongs

| Signal class                       | Preferred layer        | Reference plane    | Notes                      |
|------------------------------------|------------------------|--------------------|----------------------------|
| Buck switch-node loop              | Layer 1                | Layer 2 GND        | Tightest possible loop     |
| I2S (MCLK, BCK, LRCK, DATA)        | Layer 1                | Layer 2 GND        | Series-terminated, flanked |
| SPI to DAC8552 (SCK, MOSI, CS)     | Layer 1                | Layer 2 GND        | Short as possible          |
| Precision analog (Vref → DAC, DAC → OPA1642 → jacks) | Layer 1 | Layer 2 GND | Kept inside Zone C        |
| Audio output (PCM5102A → jacks)    | Layer 1                | Layer 2 GND        | Kept inside Zone C         |
| Encoder / button / slow digital    | Layer 1 or Layer 4     | Layer 2 or Layer 3 | No restrictions            |
| Power delivery                     | Layer 3 pour           | --                 | Vertical via drops         |
| Front-panel wiring (OLED cable, jacks) | Layer 1 or Layer 4 | --                 | Kept away from buck        |

### Layer transitions (vias)

- Minimize Layer 1 ↔ Layer 4 transitions for fast signals. Each via is
  an impedance discontinuity and each transition changes the reference
  plane (Layer 2 → Layer 3).
- When a fast signal must transition, place a stitching via
  (GND ↔ GND, Layer 2 ↔ local Layer 1 / Layer 4 pour) within 1 mm of
  the signal via, so the return current has a nearby path to hop to the
  new reference plane.
- Slow signals (encoders, etc.) do not need stitching vias on transitions.

### Trace widths (minimum)

| Net             | Minimum width | Reason                            |
|-----------------|---------------|-----------------------------------|
| +12V, -12V      | 0.5 mm        | Bus rail current + reverse-diode  |
| +5V             | 0.6 mm        | Full board digital + LDO inputs   |
| +3V3 (digital)  | 0.4 mm        | MCU + OLED load                   |
| +3V3_AUDIO      | 0.3 mm        | ~25 mA load                       |
| +3V3_PREC       | 0.3 mm        | ~10 mA load                       |
| GND return      | n/a           | Uses Layer 2 plane, not traces    |
| Signal          | 0.15 mm       | JLCPCB minimum for 1 oz           |
| I2S / SPI       | 0.2 mm        | Slight over-minimum for impedance |

Power rails are preferably delivered as Layer 3 pours, not traces; the
widths above apply only where a pour is not feasible (e.g. bridging
around a component).

---

