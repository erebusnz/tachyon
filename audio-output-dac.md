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
| AVDD / CPVDD supply | `+3V3_AUDIO` (own LDO) | Dedicated low-noise rail, isolated from precision CV path |
| Precision rail | `+3V3_PREC` (own LDO) | Separate LDO for DAC8552 VDD (REF5025 runs from `+5V`, OPA1642 from ±12 V) |
| LDO part (both rails) | TPS7A2033PDBVR (TI, LCSC C2862740) | 4 uVRMS noise, 56 dB PSRR @ 1 kHz, SOT-23-5, 300 mA |

---

## 2. STM32F405 pin allocation

I2S2 is unavailable (PB12-PB15 belong to SPI2 / OLED + DAC8552). The
PCM5102A is driven from **I2S3** on PC10 / PC12 / PA15.

| PCM5102A pin | Net name | STM32 pin | Peripheral / AF |
|---|---|---|---|
| 4 BCK | `I2S3_BCK` | **PC10** | SPI3/I2S3_CK (AF6) |
| 5 DIN | `I2S3_SD` | **PC12** | SPI3/I2S3_SD (AF6) |
| 6 LRCK | `I2S3_WS` | **PA15** | SPI3/I2S3_WS (AF6) |
| 8 XSMT | `~MUTE` | **PC6** | GPIO output, push-pull |

Notes:
- **PA15** is preferred over PA4 for I2S3_WS because PA4 is the onboard
  DAC1 output -- leave it free for possible analog reuse even though the
  external DAC8552 (U6) currently provides the precision CV.
- **`~MUTE`** is wired as a board-wide active-low mute net. It will also be
  routed to any future analog mute (e.g. an output-stage MOSFET shunt or
  another DAC's enable). On power-up firmware must drive `~MUTE` LOW until
  I2S clocks are stable, then HIGH to unmute (`PCM5102A.md:89`).
- SCK (pin 3) is **tied to GND** so the internal PLL generates all internal
  clocks from BCK (`PCM5102A.md:48,88`). Do not route SCK to the MCU.

Update `hardware-design-plan.md` "Available GPIO" / pin-budget section to mark
PC10, PC12, PA15, and PC6 as consumed by the audio DAC.

---

## 3. Pin-by-pin connection table

| Pin | Name | Connection |
|---|---|---|
| 1 | DEMP | GND (de-emphasis disabled) |
| 2 | FLT | GND (normal-latency filter) |
| 3 | SCK | GND (internal PLL mode) |
| 4 | BCK | `I2S3_BCK` -> STM32 PC10 |
| 5 | DIN | `I2S3_SD` -> STM32 PC12 |
| 6 | LRCK | `I2S3_WS` -> STM32 PA15 |
| 7 | FMT | GND (I2S format) |
| 8 | XSMT | `~MUTE` -> STM32 PC6 |
| 9 | AGND | GND (L2 plane) |
| 10 | CPGND | GND (L2 plane) |
| 11 | CAPP | Flying cap to pin 13 (CAPM) -- C_FLY |
| 12 | CPVDD | `+3V3_AUDIO` via independent decoupling |
| 13 | CAPM | Flying cap to pin 11 (CAPP) -- C_FLY |
| 14 | VNEG | Decouple to GND -- C_VNEG |
| 15 | OUTL | Series resistor R_OUTL -> output jack U8 tip |
| 16 | AGND | GND (L2 plane) |
| 17 | OUTR | Series resistor R_OUTR -> output jack U9 tip |
| 18 | AVDD | `+3V3_AUDIO` via independent decoupling |
| 19 | DVDD | `+3V3` (digital, WeAct LDO) via 100 nF |
| 20 | LDOO | Decouple to GND -- C_LDOO |

All "GND" pins above land on the single continuous L2 ground plane
defined in `pcb-design.md` §3. There is no separate AGND / DGND / CPGND
net; the PCM5102A's three ground pins (9, 10, 16) each get their own
via to L2 placed directly adjacent to the pin.

---

## 4. Power supply topology

The PCM5102A is fed from a **dedicated low-noise LDO** to keep its
charge-pump switching noise out of the precision CV / pitch path. A
**second identical LDO** supplies the precision analog rail
(`+3V3_PREC`) used by DAC8552 VDD. Both LDOs are the same part for BOM
simplicity (see `power-supply.md` §4 for the full power tree; REF5025
runs from `+5V` directly and OPA1642 runs from ±12 V — neither sits on
`+3V3_PREC`).

```
Eurorack +12V ──> local +5V buck ─┬── LDO1 (TPS7A2033PDBVR) ──> +3V3_PREC  → DAC8552 VDD
                                  │
                                  └── LDO2 (TPS7A2033PDBVR) ──> +3V3_AUDIO → PCM5102A AVDD (pin 18), CPVDD (pin 12)

WeAct onboard 3V3 LDO ──> +3V3 (digital) → STM32, OLED, PCM5102A DVDD (pin 19)
```

**LDO part (both):** `TPS7A2033PDBVR` (TI, LCSC **C2862740**) -- SOT-23-5,
3.3 V fixed, 300 mA, 4 uVRMS noise (10 Hz-100 kHz), 56 dB PSRR @ 1 kHz.

Per-LDO support components (×2 -- one set for LDO1, one set for LDO2):

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C_LDOx_IN | 1 uF | 0603 X7R | VIN -- GND | Input cap, close to pin 1 |
| C_LDOx_OUT | 1 uF | 0603 X7R | VOUT -- GND | Output cap, close to pin 5 (ceramic-stable) |

Total `+3V3_AUDIO` current: ~22 mA (AVDD 17 mA + CPVDD 4.5 mA). Total
`+3V3_PREC` current: ~10 mA. Both well within the TPS7A2033's 300 mA.

---

## 4a. Decoupling and passives BOM (per `PCM5102A.md:91-92`)

Place every cap **as close to its pin as possible**, on the same side as U3
where practical, with short fat traces to GND.

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C_AVDD1 | 10 uF | 0805 X5R/X7R | AVDD (pin 18) -- GND | Bulk on `+3V3_AUDIO` |
| C_AVDD2 | 100 nF | 0402/0603 | AVDD (pin 18) -- GND | HF bypass, closest to pin |
| C_CPVDD1 | 10 uF | 0805 X5R/X7R | CPVDD (pin 12) -- GND | Bulk on `+3V3_AUDIO` |
| C_CPVDD2 | 100 nF | 0402/0603 | CPVDD (pin 12) -- GND | HF bypass, closest to pin |
| C_DVDD | 100 nF | 0402/0603 | DVDD (pin 19) -- GND | On digital `+3V3` |
| C_LDOO | 1 uF | 0603 X7R | LDOO (pin 20) -- GND | Required, do not omit |
| C_VNEG | 1 uF | 0603 X7R | VNEG (pin 14) -- GND | Charge-pump output |
| C_FLY | 470 nF | 0603 X7R | CAPP (pin 11) <-> CAPM (pin 13) | 220 nF-1 uF range; 470 nF nominal |
| R_OUTL | 470 R | 0603 1% | OUTL (pin 15) -> U8 tip | EMI / cable-cap LPF |
| R_OUTR | 470 R | 0603 1% | OUTR (pin 17) -> U9 tip | EMI / cable-cap LPF |

The board already stocks 100 nF 0805 (BOM line 1) and 10 uF 0805 (BOM line
2); reuse those values to avoid adding line items. The 1 uF and 470 nF
0603 parts must be added to the BOM if not already present (the LDO
input/output caps reuse the 1 uF line).

---

## 5. Output stage and jacks

PCM5102A is a DirectPath part: **no DC blocking capacitors** are required
on OUTL/OUTR (`PCM5102A.md:93`). The outputs are ground-centered 2.1 VRMS
full-scale and can drive >=1 kohm directly.

- OUTL (pin 15) -> R_OUTL 470 R -> **U8** (AudioJack2_SwitchT) tip
- OUTR (pin 17) -> R_OUTR 470 R -> **U9** (AudioJack2_SwitchT) tip
- U8/U9 sleeve -> GND (L2 plane)
- U8/U9 switch (NC) terminals: leave floating unless used for jack-detect

Keep OUTL/OUTR traces short, route on L1 with L2 as the reference
plane, and keep them clear of the I2S corridor (BCK/LRCK/DIN). The
output jacks belong in Zone C per `pcb-design.md` §5.

---

## 6. Grounding

Ground topology is defined globally in **`pcb-design.md`** -- L2 is a
single continuous GND plane, never split or cut, and partitioning
between analog and digital return currents is handled by component
placement on L1, not by copper topology.

PCM5102A-specific notes on top of that:

- Pins 9 (AGND), 10 (CPGND), and 16 (AGND) are all tied to the same
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

- [ ] Strap pins 1 (DEMP), 2 (FLT), 3 (SCK), 7 (FMT) to GND
- [ ] Wire pin 4 BCK to net `I2S3_BCK`; wire same net to STM32 PC10
- [ ] Wire pin 5 DIN to net `I2S3_SD`; wire same net to STM32 PC12
- [ ] Wire pin 6 LRCK to net `I2S3_WS`; wire same net to STM32 PA15
- [ ] Wire pin 8 XSMT to net `~MUTE`; wire same net to STM32 PC6
- [ ] Place LDO1 (TPS7A2033PDBVR) with 1 uF in + 1 uF out, output net `+3V3_PREC`
- [ ] Place LDO2 (TPS7A2033PDBVR) with 1 uF in + 1 uF out, output net `+3V3_AUDIO`
- [ ] Tie LDO1/LDO2 VIN to the +5 V rail; GND pads drop directly to L2
- [ ] Place C_AVDD1/C_AVDD2 at pin 18, tie to `+3V3_AUDIO` and GND
- [ ] Place C_CPVDD1/C_CPVDD2 at pin 12, tie to `+3V3_AUDIO` and GND
- [ ] Place C_DVDD at pin 19, tie to `+3V3` (digital) and GND
- [ ] Place C_LDOO at pin 20, tie to GND
- [ ] Place C_VNEG at pin 14, tie to GND
- [ ] Place C_FLY between pins 11 and 13
- [ ] Wire OUTL (pin 15) -> R_OUTL -> U8 tip; sleeve to GND
- [ ] Wire OUTR (pin 17) -> R_OUTR -> U9 tip; sleeve to GND
- [ ] Confirm pins 9, 10, 16 each have their own via dropping directly
      to L2 within < 1 mm of the pin (the plane is the star)
- [ ] Confirm U3 is placed at the Zone B-facing edge of Zone C with
      I2S run ≤ 30 mm -- see `pcb-design.md` §5
- [ ] Update `hardware-design-plan.md` pin allocation table to reserve
      PC10, PC12, PA15, PC6
