# Audio Output DAC -- PCM5102A (U3) Wiring Spec

This document is the schematic-side wiring spec for the PCM5102APWR stereo
audio DAC (designator **U3**) on the Tachyon board. The symbol is
already placed; this file enumerates every net, decoupling component, and
MCU pin assignment needed to wire it up in EasyEDA Pro.

Source references:
- `datasheets/PCM5102A.md` -- pinout, decoupling rules, application notes
- `hardware-design-plan.md` -- MCU pin allocation, SPI2/OLED/DAC reservations

---

## 1. Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Audio format | I2S (FMT = LOW) | Standard STM32 I2S peripheral output |
| Master clock | Internal PLL (SCK = GND) | No external MCLK required; saves a pin |
| Digital filter | Normal latency (FLT = LOW) | Sharp roll-off, default audio quality |
| De-emphasis | Disabled (DEMP = LOW) | Not a CD source |
| Soft mute | MCU GPIO (XSMT) | Firmware-controlled, shared global mute net |
| DVDD supply | `+3V3` (digital) | LDO regulates to 1.8 V internally; LDOO decoupled |
| AVDD / CPVDD supply | `+3V3_AUDIO` | Dedicated low-noise rail, see `power-supply.md` §4 |
| Output buffer | OPA1642 (U10), gain ×1.68 | Lifts 2.1 VRMS DAC output to 10 Vpp Eurorack convention |
| Buffer supply | ±12 V | Direct from Eurorack rails; OPA1642 swings ground-centered |

---

## 2. STM32F405 pin allocation

I2S2 is unavailable (PB12-PB15 belong to SPI2 / OLED + DAC8552). The
PCM5102A is driven from **I2S3** on PC10 / PC12 / PA15.

| PCM5102A pin | Net name | STM32 pin | Peripheral / AF |
|---|---|---|---|
| 13 BCK | `I2S3_BCK` | **PC10** | SPI3/I2S3_CK (AF6) |
| 14 DIN | `I2S3_SD` | **PC12** | SPI3/I2S3_SD (AF6) |
| 15 LRCK | `I2S3_WS` | **PA15** | SPI3/I2S3_WS (AF6) |
| 17 XSMT | `~MUTE` | **PC6** | GPIO output, push-pull |

Notes:
- **PA15** is preferred over PA4 for I2S3_WS because PA4 is the onboard
  DAC1 output -- leave it free for possible analog reuse even though the
  external DAC8552 (U6) currently provides the precision CV.
- **`~MUTE`** is wired as a board-wide active-low mute net. It will also be
  routed to any future analog mute (e.g. an output-stage MOSFET shunt or
  another DAC's enable). On power-up firmware must drive `~MUTE` LOW until
  I2S clocks are stable, then HIGH to unmute (`PCM5102A.md:89`).
- SCK (pin 12) is **tied to GND** so the internal PLL generates all internal
  clocks from BCK (`PCM5102A.md:48,88`). Do not route SCK to the MCU.

Update `hardware-design-plan.md` "Available GPIO" / pin-budget section to mark
PC10, PC12, PA15, and PC6 as consumed by the audio DAC.

---

## 3. Pin-by-pin connection table

| Pin | Name | Connection |
|---|---|---|
| 1 | CPVDD | `+3V3_AUDIO` via independent decoupling |
| 2 | CAPP | Flying cap to pin 4 (CAPM) -- C_FLY |
| 3 | CPGND | GND (L2 plane) |
| 4 | CAPM | Flying cap to pin 2 (CAPP) -- C_FLY |
| 5 | VNEG | Decouple to GND -- C_VNEG |
| 6 | OUTL | R_OUTL 470 R + C_OUTL 2.2 nF EMI filter -> U10A +IN (pin 3); buffer drives U17 |
| 7 | OUTR | R_OUTR 470 R + C_OUTR 2.2 nF EMI filter -> U10B +IN (pin 5); buffer drives U18 |
| 8 | AVDD | `+3V3_AUDIO` via independent decoupling |
| 9 | AGND | GND (L2 plane) |
| 10 | DEMP | GND (de-emphasis disabled) |
| 11 | FLT | GND (normal-latency filter) |
| 12 | SCK | GND (internal PLL mode) |
| 13 | BCK | `I2S3_BCK` -> STM32 PC10 |
| 14 | DIN | `I2S3_SD` -> STM32 PC12 |
| 15 | LRCK | `I2S3_WS` -> STM32 PA15 |
| 16 | FMT | GND (I2S format) |
| 17 | XSMT | `~MUTE` -> STM32 PC6 |
| 18 | LDOO | Decouple to GND -- C_LDOO |
| 19 | DGND | GND (L2 plane) |
| 20 | DVDD | `+3V3` (digital, WeAct LDO) via 100 nF |

All "GND" pins above land on the single continuous L2 ground plane
defined in `pcb-design.md` §3. There is no separate AGND / DGND / CPGND
net; the PCM5102A's three ground pins (3 CPGND, 9 AGND, 19 DGND) each
get their own via to L2 placed directly adjacent to the pin.

---

## 4. Power supply

The PCM5102A draws two rails:

- **`+3V3_AUDIO`** → AVDD (pin 8) and CPVDD (pin 1). Sourced from a
  dedicated low-noise LDO defined in **`power-supply.md` §4** to keep
  charge-pump switching noise out of the precision CV / pitch path.
- **`+3V3` (digital)** → DVDD (pin 20). Tapped from the WeAct onboard
  3V3 LDO; see `power-supply.md` §5.

This document only specifies the per-pin decoupling at U3 (§4a). All
LDO part selection, support caps, current budget, and routing of the
upstream rails live in `power-supply.md`.

---

## 4a. Decoupling and passives BOM (per `PCM5102A.md:91-92`)

Place every cap **as close to its pin as possible**, on the same side as U3
where practical, with short fat traces to GND.

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C_AVDD1 | 10 uF | 0805 X5R/X7R | AVDD (pin 8) -- GND | Bulk on `+3V3_AUDIO` |
| C_AVDD2 | 100 nF | 0805 | AVDD (pin 8) -- GND | HF bypass, closest to pin |
| C_CPVDD1 | 10 uF | 0805 X5R/X7R | CPVDD (pin 1) -- GND | Bulk on `+3V3_AUDIO` |
| C_CPVDD2 | 100 nF | 0805 | CPVDD (pin 1) -- GND | HF bypass, closest to pin |
| C_DVDD | 100 nF | 0805 | DVDD (pin 20) -- GND | On digital `+3V3` |
| C_LDOO | 1 uF | 0805 X7R | LDOO (pin 18) -- GND | Required, do not omit |
| C_VNEG | 1 uF | 0805 X7R | VNEG (pin 5) -- GND | Charge-pump output |
| C_FLY | 470 nF | 0805 X7R | CAPP (pin 2) <-> CAPM (pin 4) | 220 nF-1 uF range; 470 nF nominal |
| R_OUTL | 470 R | 0805 1% | OUTL (pin 6) -> node L | Series element of DAC-side EMI LPF |
| R_OUTR | 470 R | 0805 1% | OUTR (pin 7) -> node R | Series element of DAC-side EMI LPF |
| C_OUTL | 2.2 nF | 0805 NP0/C0G | node L -- GND | Shunt element; ~154 kHz LPF with R_OUTL |
| C_OUTR | 2.2 nF | 0805 NP0/C0G | node R -- GND | Shunt element; ~154 kHz LPF with R_OUTR |

The board already stocks 100 nF 0805 (BOM line 1) and 10 uF 0805 (BOM line
2); reuse those values to avoid adding line items. The 1 uF and 470 nF
0805 parts must be added to the BOM if not already present.

---

## 5. Output stage: U10 buffer and jacks

PCM5102A is a DirectPath part: **no DC blocking capacitors** are required
on OUTL/OUTR (`PCM5102A.md:93`). The outputs are ground-centered 2.1 VRMS
full-scale (~5.94 Vpp), which is ~4.6 dB quieter than the Eurorack 10 Vpp
convention. **U10 (OPA1642, dual)** is added as a stereo non-inverting
buffer with gain ×1.68 to lift the output to exactly 10 Vpp full-scale.

### Signal chain (per channel)

```
DAC OUTx ──> R_OUTx 470 R ──┬──> C_OUTx 2.2 nF ──> GND   (DAC-side EMI LPF, fc ~154 kHz)
                            │
                            └──> U10x +IN
                                  │
                                  └──> U10x OUT ──> R_SERx 1 k ──> Ux jack tip
                                         │
                       U10x -IN <─ R_FBx 6.8 k <┤
                       U10x -IN ─> R_GNx 10 k ─> GND
```

Gain = 1 + R_FBx / R_GNx = 1 + 6.8 / 10 = **1.68×**.
Output full-scale: 2.1 VRMS × 1.68 = 3.53 VRMS = 9.99 Vpp ≈ ±5 V peak.

### U10 (OPA1642, dual SOIC-8) pin assignment

| OPA1642 pin | Net | Notes |
|---|---|---|
| 1 OUT_A | `AUDIO_BUF_L` -> R_SERL -> U17 tip | Left channel output |
| 2 -IN_A | Junction of R_FBL and R_GNL | Inverting input feedback node |
| 3 +IN_A | node L (post DAC EMI filter) | Non-inverting input |
| 4 V- | `-12V` | Decouple per §4a |
| 5 +IN_B | node R (post DAC EMI filter) | Non-inverting input |
| 6 -IN_B | Junction of R_FBR and R_GNR | Inverting input feedback node |
| 7 OUT_B | `AUDIO_BUF_R` -> R_SERR -> U18 tip | Right channel output |
| 8 V+ | `+12V` | Decouple per §4a |

### U10 buffer passives BOM

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| R_FBL | 6.8 k 1% | 0805 | U10A OUT -- U10A -IN | Feedback top |
| R_GNL | 10 k 1% | 0805 | U10A -IN -- GND | Feedback bottom (gain set) |
| R_FBR | 6.8 k 1% | 0805 | U10B OUT -- U10B -IN | Feedback top |
| R_GNR | 10 k 1% | 0805 | U10B -IN -- GND | Feedback bottom (gain set) |
| R_SERL | 1 k 1% | 0805 | U10A OUT -- U17 tip | Eurorack 1 k output impedance convention |
| R_SERR | 1 k 1% | 0805 | U10B OUT -- U18 tip | Eurorack 1 k output impedance convention |
| C_U10_VPOS_BULK | 10 uF | 0805 X7R | +12V -- GND at U10 pin 8 | Bulk decoupling |
| C_U10_VPOS_HF | 100 nF | 0805 X7R | +12V -- GND at U10 pin 8 | HF bypass, closest to pin |
| C_U10_VNEG_BULK | 10 uF | 0805 X7R | -12V -- GND at U10 pin 4 | Bulk decoupling |
| C_U10_VNEG_HF | 100 nF | 0805 X7R | -12V -- GND at U10 pin 4 | HF bypass, closest to pin |

Reuse the existing 100 nF and 10 uF 0805 BOM lines. The 6.8 k, 10 k, and
1 k 1 % 0805 resistors must be added to the BOM if not already present.

### Jacks

- U17/U18 sleeve -> GND (L2 plane)
- U17/U18 switch (NC) terminals: leave floating unless used for jack-detect

Keep OUTL/OUTR traces short from U3 to U10 +IN, route on L1 with L2 as
the reference plane, and keep them clear of the I2S corridor
(BCK/LRCK/DIN). U10 should sit between U3 and the output jacks in
Zone C per `pcb-design.md` §5.

### Headroom check

OPA1642 on ±12 V can swing within ~1.5 V of either rail (rail-to-rail
output, see `OPA1642.md`), giving ~±10.5 V usable. Our 10 Vpp full-scale
(±5 V peak) leaves > 5 V of headroom on each rail — comfortably clear of
clipping under all signal conditions.

---

## 6. Grounding

Ground topology is defined globally in **`pcb-design.md`** -- L2 is a
single continuous GND plane, never split or cut, and partitioning
between analog and digital return currents is handled by component
placement on L1, not by copper topology.

PCM5102A-specific notes on top of that:

- Pins 3 (CPGND), 9 (AGND), and 19 (DGND) are all tied to the same
  `GND` net. Each gets its own via dropping directly to L2 next to
  the pin -- do not daisy-chain them through surface copper.
- Star-grounding is satisfied automatically by dropping each GND pin to
  the L2 plane within < 1 mm; the plane itself *is* the star.
- I2S signals (BCK/LRCK/DIN) route on L1 from Zone B to U3 with L2
  directly underneath as the reference plane. Series-terminate at
  the source (see `pcb-design.md` §5 "The I2S bridge").
- OUTL / OUTR route on L1 within Zone C, referenced to L2, and stay
  clear of the I2S corridor.
- Place U3 at the Zone B-facing edge of Zone C so the I2S run is as
  short as possible (target ≤ 30 mm).

---

## 7. Implementation checklist (EasyEDA Pro)

Work through these in order on the schematic sheet containing U3:

- [ ] Strap pins 10 (DEMP), 11 (FLT), 12 (SCK), 16 (FMT) to GND
- [ ] Wire pin 13 BCK to net `I2S3_BCK`; wire same net to STM32 PC10
- [ ] Wire pin 14 DIN to net `I2S3_SD`; wire same net to STM32 PC12
- [ ] Wire pin 15 LRCK to net `I2S3_WS`; wire same net to STM32 PA15
- [ ] Wire pin 17 XSMT to net `~MUTE`; wire same net to STM32 PC6
- [ ] Place C_AVDD1/C_AVDD2 at pin 8, tie to `+3V3_AUDIO` and GND
- [ ] Place C_CPVDD1/C_CPVDD2 at pin 1, tie to `+3V3_AUDIO` and GND
- [ ] Place C_DVDD at pin 20, tie to `+3V3` (digital) and GND
- [ ] Place C_LDOO at pin 18, tie to GND
- [ ] Place C_VNEG at pin 5, tie to GND
- [ ] Place C_FLY between pins 2 and 4
- [ ] Place R_OUTL/R_OUTR at U3 pins 6/7; place C_OUTL/C_OUTR (2.2 nF NP0) from the post-resistor node to GND
- [ ] Place U10 (OPA1642 dual SOIC-8) between U3 and U17/U18
- [ ] Wire U10 pin 3 (+IN_A) to node L (post R_OUTL); wire U10 pin 5 (+IN_B) to node R (post R_OUTR)
- [ ] Wire feedback per channel: R_FBL 6.8 k from U10 pin 1 to pin 2; R_GNL 10 k from pin 2 to GND. Same for R channel (pins 7, 6)
- [ ] Wire U10 pin 1 -> R_SERL 1 k -> U17 tip; U10 pin 7 -> R_SERR 1 k -> U18 tip; sleeves to GND
- [ ] Tie U10 pin 8 to `+12V`, pin 4 to `-12V`; place 100 nF + 10 uF on each rail at the package
- [ ] Confirm pins 3, 9, 19 each have their own via dropping directly
      to L2 within < 1 mm of the pin (the plane is the star)
- [ ] Confirm U3 is placed at the Zone B-facing edge of Zone C with
      I2S run ≤ 30 mm -- see `pcb-design.md` §5
- [ ] Update `hardware-design-plan.md` pin allocation table to reserve
      PC10, PC12, PA15, PC6
