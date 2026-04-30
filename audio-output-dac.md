# Audio Output DAC -- PCM5102A (U3) Wiring Spec

This document is the schematic-side wiring spec for the PCM5102APWR stereo
audio DAC (designator **U3**) on the Tachyon board. The symbol is
already placed; this file enumerates every net, decoupling component, and
MCU pin assignment needed to wire it up in EasyEDA Pro.

Source references:
- `datasheets/PCM5102A.md` -- pinout, decoupling rules, application notes
- `hardware-design-plan.md` -- MCU pin allocation, SPI1 (OLED) / SPI2 (DAC) reservations

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
| Output buffer | OPA1642 (U16), gain ×1.68 | Lifts 2.1 VRMS DAC output to 10 Vpp Eurorack convention |
| Buffer supply | ±12 V | Direct from Eurorack rails; OPA1642 swings ground-centered |

---

## 2. STM32F405 pin allocation

I2S2 is unavailable (PB12-PB15 belong to SPI2 DAC8552 + OLED CS). The
PCM5102A is driven from **I2S3** on PB3 / PB5 / PA15 (alternate pins,
freeing PC10/PC12 for the onboard SDIO MicroSD slot).

| PCM5102A pin | Net name | STM32 pin | Peripheral / AF |
|---|---|---|---|
| 13 BCK | `I2S3_BCK` | **PB3** | SPI3/I2S3_CK (AF6) |
| 14 DIN | `I2S3_SD` | **PB5** | SPI3/I2S3_SD (AF6) |
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
| 1 | CPVDD | `+3V3_AUDIO` via C24 (100 nF) + C25 (10 µF) decoupling |
| 2 | CAPP | Flying cap to pin 4 (CAPM) -- C27 (470 nF) |
| 3 | CPGND | GND (Layer 2 plane) |
| 4 | CAPM | Flying cap to pin 2 (CAPP) -- C27 (470 nF) |
| 5 | VNEG | Decouple to GND -- C28 (1 µF) |
| 6 | OUTL | R7 470 R + C32 2.2 nF EMI filter -> U16A +IN (pin 3); buffer drives U18 |
| 7 | OUTR | R8 470 R + C33 2.2 nF EMI filter -> U16B +IN (pin 5); buffer drives U17 |
| 8 | AVDD | `+3V3_AUDIO` via C30 (100 nF) + C31 (10 µF) decoupling |
| 9 | AGND | GND (Layer 2 plane) |
| 10 | DEMP | GND (de-emphasis disabled) |
| 11 | FLT | GND (normal-latency filter) |
| 12 | SCK | GND (internal PLL mode) |
| 13 | BCK | `I2S3_BCK` -> STM32 PB3 |
| 14 | DIN | `I2S3_SD` -> STM32 PB5 |
| 15 | LRCK | `I2S3_WS` -> STM32 PA15 |
| 16 | FMT | GND (I2S format) |
| 17 | XSMT | `~MUTE` -> STM32 PC6 |
| 18 | LDOO | Decouple to GND -- C29 (1 µF) |
| 19 | DGND | GND (Layer 2 plane) |
| 20 | DVDD | `+3V3` (digital, WeAct LDO) via C26 (100 nF) |

All "GND" pins above land on the single continuous Layer 2 ground
plane defined in `pcb-design.md` §3. There is no separate AGND / DGND /
CPGND net; the PCM5102A's three ground pins (3 CPGND, 9 AGND, 19 DGND)
each get their own via to Layer 2 placed directly adjacent to the pin.

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
| C31 | 10 uF | 0805 X5R/X7R | AVDD (pin 8) -- GND | Bulk on `+3V3_AUDIO` |
| C30 | 100 nF | 0805 | AVDD (pin 8) -- GND | HF bypass, closest to pin |
| C25 | 10 uF | 0805 X5R/X7R | CPVDD (pin 1) -- GND | Bulk on `+3V3_AUDIO` |
| C24 | 100 nF | 0805 | CPVDD (pin 1) -- GND | HF bypass, closest to pin |
| C26 | 100 nF | 0805 | DVDD (pin 20) -- GND | On digital `+3V3` |
| C29 | 1 uF | 0805 X7R | LDOO (pin 18) -- GND | Required, do not omit |
| C28 | 1 uF | 0805 X7R | VNEG (pin 5) -- GND | Charge-pump output |
| C27 | 470 nF | 0805 X7R | CAPP (pin 2) <-> CAPM (pin 4) | 220 nF-1 uF range; 470 nF nominal |
| R7 | 470 R | 0805 1% | OUTL (pin 6) -> node L | Series element of DAC-side EMI LPF |
| R8 | 470 R | 0805 1% | OUTR (pin 7) -> node R | Series element of DAC-side EMI LPF |
| C32 | 2.2 nF | 0805 NP0/C0G | node L -- GND | Shunt element; ~154 kHz LPF with R7 |
| C33 | 2.2 nF | 0805 NP0/C0G | node R -- GND | Shunt element; ~154 kHz LPF with R8 |

The board already stocks 100 nF 0805 (BOM line 1) and 10 uF 0805 (BOM line
2); reuse those values to avoid adding line items. The 1 uF and 470 nF
0805 parts must be added to the BOM if not already present.

---

## 5. Output stage: U16 buffer and jacks

PCM5102A is a DirectPath part: **no DC blocking capacitors** are required
on OUTL/OUTR (`PCM5102A.md:93`). The outputs are ground-centered 2.1 VRMS
full-scale (~5.94 Vpp), which is ~4.6 dB quieter than the Eurorack 10 Vpp
convention. **U16 (OPA1642, dual)** is added as a stereo non-inverting
buffer with gain ×1.68 to lift the output to exactly 10 Vpp full-scale.

### Signal chain (per channel)

```
DAC OUTL ──> R7  470 R ──┬──> C32 2.2 nF ──> GND   (DAC-side EMI LPF, fc ~154 kHz)
                         │
                         └──> U16.1 +IN
                               │
                               └──> U16.1 OUT ──> R16 1 k ──> U18 (A-OUT-L) tip
                                      │                  (R16 on I/O board)
                    U16.1 -IN <─ R10 6.8 k <┤
                    U16.1 -IN ─> R9  10 k ─> GND

DAC OUTR ──> R8  470 R ──┬──> C33 2.2 nF ──> GND
                         │
                         └──> U16.2 +IN
                               │
                               └──> U16.2 OUT ──> R15 1 k ──> U17 (A-OUT-R) tip
                                      │                  (R15 on I/O board)
                    U16.2 -IN <─ R12 6.8 k <┤
                    U16.2 -IN ─> R11 10 k ─> GND
```

Gain = 1 + R_FB / R_GN = 1 + 6.8 / 10 = **1.68×**.
Output full-scale: 2.1 VRMS × 1.68 = 3.53 VRMS = 9.99 Vpp ≈ ±5 V peak.

### U16 (OPA1642, dual SOIC-8) pin assignment

| OPA1642 pin | Net | Notes |
|---|---|---|
| 1 OUT_A | `AUDIO_BUF_L` -> R16 -> U18 (A-OUT-L) tip | Left channel output (R16 lives on the I/O board schematic) |
| 2 -IN_A | Junction of R10 and R9 | Inverting input feedback node |
| 3 +IN_A | node L (post DAC EMI filter) | Non-inverting input |
| 4 V- | `-12V` | Decouple per §4a |
| 5 +IN_B | node R (post DAC EMI filter) | Non-inverting input |
| 6 -IN_B | Junction of R12 and R11 | Inverting input feedback node |
| 7 OUT_B | `AUDIO_BUF_R` -> R15 -> U17 (A-OUT-R) tip | Right channel output (R15 lives on the I/O board schematic) |
| 8 V+ | `+12V` | Decouple per §4a |

### U16 buffer passives BOM

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| R10 | 6.8 k 1% | 0805 | U16.1 OUT -- U16.1 -IN | Feedback top (left) |
| R9 | 10 k 1% | 0805 | U16.1 -IN -- GND | Feedback bottom, left (gain set) |
| R12 | 6.8 k 1% | 0805 | U16.2 OUT -- U16.2 -IN | Feedback top (right) |
| R11 | 10 k 1% | 0805 | U16.2 -IN -- GND | Feedback bottom, right (gain set) |
| R16 | 1 k 1% | 0805 | U16.1 OUT -- U18 (A-OUT-L) tip | Eurorack 1 k output impedance convention. Placed on the I/O board schematic. |
| R15 | 1 k 1% | 0805 | U16.2 OUT -- U17 (A-OUT-R) tip | Eurorack 1 k output impedance convention. Placed on the I/O board schematic. |
| C36 | 100 nF | 0805 X7R | +12V -- GND at U16 pin 8 | HF bypass, closest to pin. Bulk handled by C1 at input filter. |
| C35 | 100 nF | 0805 X7R | -12V -- GND at U16 pin 4 | HF bypass, closest to pin. Bulk handled by C2 at input filter. |

Reuse the existing 100 nF and 10 uF 0805 BOM lines. The 6.8 k, 10 k, and
1 k 1 % 0805 resistors must be added to the BOM if not already present.

### Jacks

- U17/U18 sleeve -> GND (Layer 2 plane)
- U17/U18 switch (NC) terminals: leave floating unless used for jack-detect

Keep OUTL/OUTR traces short from U3 to U16 +IN, route on Layer 1 with
Layer 2 as the reference plane, and keep them clear of the I2S corridor
(BCK/LRCK/DIN). U16 should sit between U3 and the output jacks in
Zone C per `pcb-design.md` §5.

### Headroom check

OPA1642 on ±12 V can swing within ~1.5 V of either rail (rail-to-rail
output, see `OPA1642.md`), giving ~±10.5 V usable. Our 10 Vpp full-scale
(±5 V peak) leaves > 5 V of headroom on each rail — comfortably clear of
clipping under all signal conditions.

---

## 6. Grounding

Ground topology is defined globally in **`pcb-design.md`** -- Layer 2
is a single continuous GND plane, never split or cut, and partitioning
between analog and digital return currents is handled by component
placement on Layer 1, not by copper topology.

PCM5102A-specific notes on top of that:

- Pins 3 (CPGND), 9 (AGND), and 19 (DGND) are all tied to the same
  `GND` net. Each gets its own via dropping directly to Layer 2 next to
  the pin -- do not daisy-chain them through surface copper.
- Star-grounding is satisfied automatically by dropping each GND pin to
  the Layer 2 plane within < 1 mm; the plane itself *is* the star.
- I2S signals (BCK/LRCK/DIN) route on Layer 1 from Zone B to U3 with
  Layer 2 directly underneath as the reference plane. Series-terminate
  at the source (see `pcb-design.md` §5 "The I2S bridge").
- OUTL / OUTR route on Layer 1 within Zone C, referenced to Layer 2,
  and stay clear of the I2S corridor.
- Place U3 at the Zone B-facing edge of Zone C so the I2S run is as
  short as possible (target ≤ 30 mm).

---

