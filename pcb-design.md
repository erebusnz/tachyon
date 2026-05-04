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
| `+3V3`      | WeAct 3V3 out | STM32F405, OLED connector, PCM5102A DVDD             |
| `+3V3_PREC` | U14 out       | DAC8552 VDD and REF5025 VIN                          |
| `+3V3_AUDIO`| U15 out       | PCM5102A AVDD pin 8 and CPVDD pin 1                  |
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

## 5. Component placement (backing-board)

This section describes the placement strategy for the **backing-board**
(the dense board: TPS54202 buck, LDOs, STM32 module, audio DAC, CV DAC,
references, op-amps). The io-board is panel-side and its placement is
dictated by the panel ergonomics (jack and pot positions). The
front-board has no electrical content.

### Stratified layout

The backing-board is partitioned by **horizontal stratification** down
the long axis, not by cardinal quadrants. The board is 48 × 110 mm with
the Eurorack header (P1) at the top edge.

```
┌──────────────────────────────────────────────────────┐
│  POWER ENTRY STRIP    (top, ~Y = 0 .. -12 mm)         │
│  U12 TPS54202 buck (top-left, top side)               │
│  P1 Eurorack header (top-right, BOTTOM side; cable    │
│      plugs into the back face of the module)          │
│  D1 / D2 SS14 protection diodes                       │
│  F1 / F2 PTC fuses, L1 / L2 ferrites                  │
│  C1 / C2 bulk caps, C3 / C4 HF bypass                 │
│  C5 (buck input), C6 (BOOT), C7 (FF), C8 (output),    │
│      L3 (buck inductor), R1 / R2 (FB divider)         │
├──────────────────────────────────────────────────────┤
│  UPPER ANALOG BAND    (~Y = -28 .. -48 mm)            │
│  U14 TPS7A2033 +3V3_PREC LDO                          │
│  U15 TPS7A2033 +3V3_AUDIO LDO                         │
│  U2  REF5025 precision reference                      │
│  U7  OPA1642 CV output buffers                        │
│  U16 OPA1642 audio output buffers                     │
├──────────────────────────────────────────────────────┤
│  MID ANALOG BAND      (~Y = -48 .. -65 mm)            │
│  U3  PCM5102A audio DAC                               │
│  U6  DAC8552 CV DAC                                   │
├──────────────────────────────────────────────────────┤
│  MCU REGION           (~Y = -75 .. -95 mm)            │
│  U1  WeAct STM32F405 module — on the BOTTOM side      │
│      (physically isolated from the analog parts on    │
│       the top side by the PCB itself)                 │
│  H4, H9 — IO-board headers (bottom side)              │
└──────────────────────────────────────────────────────┘

Inter-board headers H2 / H3 sit on the left / right edges between the
upper and mid analog bands.
```

The buck noise source is at one extreme of the long axis, the analog
bands sit in the middle, and the MCU module hides on the back side of
the PCB. Analog return currents stay on the top half of the GND plane;
buck switching loop currents stay near U12; MCU digital return
currents stay near U1's pin headers on the bottom side.

### Spacing rules

Placement is constrained by these distance invariants. The audit script
(`run_audit.py`'s `SPACING_RULES`) enforces them automatically; keep
the two in sync.

| Constraint                       | Threshold | Rationale                                                          |
|----------------------------------|-----------|--------------------------------------------------------------------|
| `U12 ↔ U3` (buck ↔ audio DAC)    | ≥ 30 mm   | Switch-node loop must be far from the audio DAC to avoid radiative coupling. |
| `U12 ↔ U2` (buck ↔ ref)          | ≥ 30 mm   | Switch-node loop must be far from the precision Vref. |
| `U12 ↔ U16` (buck ↔ audio op-amp)| ≥ 30 mm   | Audio output buffers are as sensitive as the DAC; same rule applies. |
| `U2 ↔ U6` (REF5025 ↔ DAC8552)    | ≤ 20 mm   | `VREF_2V5` analog trace must be short (`cv-output-dac.md` §6). |

Adjacency / orientation rules that don't reduce to a single distance:

- **U12 (TPS54202) sits at the top edge** of the board so the buck
  switch-node loop closes locally on Layer 1 with C5 (input) → VIN →
  GND → C5, ≤ 5 mm² loop area target.
- **U7 (OPA1642 CV)** sits adjacent to U6 (DAC8552) so DAC → buffer
  traces are short. CV output traces from U7 then run to H3 on the
  right edge.
- **U16 (OPA1642 audio)** sits adjacent to U3 (PCM5102A) so PCM5102A
  OUTL/OUTR → buffer inputs are short. Audio output traces from U16
  run to H2 on the left edge.
- **U14 / U15 LDOs** sit between the power-entry strip and the upper
  analog band, so the noisy +5 V copper terminates at the LDO inputs
  and only the regulated `+3V3_PREC` / `+3V3_AUDIO` rails reach the
  analog bands.
- **U1 (WeAct STM32 module) on the bottom side** keeps the digital
  switching noise on the opposite face of the PCB from the analog
  ICs. The WeAct module has its own onboard 3V3 LDO and decoupling;
  no per-pin bypass caps from the main PCB are required.

### Decoupling caps

Every IC's decoupling cap must be on the same side of the board as the
IC, within 2 mm of the power pin it decouples, with its GND via directly
adjacent to the cap pad. The audit reports the centre-to-centre
distance from each cap to its IC; ≤ 8 mm centre-to-centre on 0805 caps
is the practical equivalent of the 2 mm pin-to-cap rule.

Sub-modules with onboard decoupling (currently the WeAct STM32F405
module, U1) are exempt — those rely on their internal bypass network
and only need a single bulk cap at the module's `+5 V` input pin.

### The I2S bridge

The I2S signals (`I2S3_BCK`, `I2S3_SD`, `I2S3_WS`) run from U1 (STM32,
bottom side) to U3 (PCM5102A, top side, mid analog band). This is the
one place on the board where a fast digital signal must transition
between layers and enter the analog half. Mitigations:

1. **Route on Layer 1 over Layer 2 GND** for the segment on the top
   side. Drop a stitching via to Layer 2 GND adjacent to each
   layer-transition via.
2. **Keep total trace length short** — under 30 mm preferred, under
   50 mm absolute. Centre-to-centre U1 ↔ U3 is ~28 mm; routed length
   will be slightly longer.
3. **Ground-flanked routing.** Run a ground pour on Layer 1 on both
   sides of the I2S bundle, stitched to Layer 2 every ~3 mm with vias,
   for the segment closest to the analog band.
4. **No other signals in the I2S corridor.** The strip of board
   between U1 and U3 is reserved for I2S and ground only.

There is no buffer IC in the I2S path — earlier revisions of this
document referenced a 74AHCT1G125 (U4); that part was dropped because
the PCM5102A's internal PLL handles the clock-domain transition and
the STM32 I2S drive strength is adequate at the trace lengths on this
board.

#### Series termination — not required at this trace length

Earlier revisions of this section mandated 33 Ω series termination
at the STM32 I2S output. That rule was over-conservative for the
trace lengths on this board.

Howard Johnson's rule of thumb: a trace is electrically short and
needs no termination when its length is less than `t_r × v / 6`,
where `t_r` is the source rise time and `v` is the FR4 propagation
velocity (~167 mm/ns).

| STM32F405 GPIO speed | `t_r` | Critical length | This board's I2S trace |
|----------------------|------:|----------------:|-----------------------:|
| Low (2 MHz)          | 100 ns | 2800 mm         | 30-40 mm  (90× margin) |
| Medium (25 MHz)      | 10 ns  | 280 mm          | 30-40 mm  (7× margin)  |
| Fast (50 MHz)        | 4 ns   | 110 mm          | 30-40 mm  (3× margin)  |
| High (100 MHz)       | 2 ns   | 56 mm           | 30-40 mm  (1.5× margin)|

The I2S signals are at audio rates (≤ 12.288 MHz BCK at 192 kHz / 64-fs),
so the firmware can — and should — set the I2S GPIO pins to **low or
medium speed**. At those speed settings the trace is electrically
short by a wide margin, reflections die out before edges transition,
and series termination delivers no measurable improvement (and can
slow edges enough to degrade the signal).

**Implementation note for firmware:** configure `PB3 (I2S3_BCK)`,
`PB5 (I2S3_SD)`, and `PA15 (I2S3_WS)` with
`GPIO_Speed_Level_Low` or `_Medium` in the I2S init code. Do not use
`_High` or `_Very_High`.

If a future redesign stretches the I2S trace beyond ~50 mm, or moves
to a faster receiver where edge rate must come up, revisit this
calculation. Until then the netlist correctly omits termination
resistors.

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

