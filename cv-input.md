# CV Input -- 2× Modulation CV Jacks → STM32F405 ADC

This document is the schematic-side wiring spec for the two CV
modulation inputs on the Tachyon board:

- **J_CV1, J_CV2** — 3.5 mm switched mono jacks (PJ398SM or equivalent)
- **U23 — OPA1642AIDR** — dual JFET op-amp, one channel per input,
  configured as an inverting attenuator + DC-bias summing stage that
  maps bipolar Eurorack CV to the unipolar STM32 ADC range
- **PA0, PA1** — STM32F405 ADC inputs (`ADC1_IN0`, `ADC1_IN1`)

The inputs are intended for **modulation**, not V/oct pitch tracking.
The F405's internal 12-bit ADC is adequate for this (see
`hardware-design-plan.md` §"ADC — Built-in is sufficient"); if pitch-
accurate CV input is ever needed, that is a separate external-ADC
sub-spec.

Source references:
- `datasheets/OPA1642.md` — pinout, rails, input range, noise
- `datasheets/STM32F405RG.md` — ADC channel map, sample time, VREF+
- `hardware-design-plan.md` §"ADC — Built-in is sufficient" and
  §"Pin allocation"
- `power-supply.md` — `±12 V`, `+3V3_PREC`, `+5 V` rail definitions
- `cv-output-dac.md` — reference for REF5025 wiring (we tap the same
  2.5 V node here for bias generation)

---

## 1. Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Number of inputs | 2 | `CV-IN-A`, `CV-IN-B` (modulation; not V/oct pitch) |
| Nominal input range | ±5 V | Doepfer A-100 standard modulation CV range; maps to full ADC scale |
| Absolute max input | ±12 V (clipped) | Over-range is passively clipped by op-amp rails and the BAT54SLT1G at the ADC node; the stage is not damaged |
| ADC full-scale mapping | −5 V → 3.3 V, 0 V → 1.65 V, +5 V → 0 V | Inverted at the op-amp; firmware flips the sign |
| Topology | Inverting summing attenuator, DC bias via non-inverting pin | Single-stage, single supply output, canonical bipolar→unipolar mapping |
| Attenuation (voltage gain) | −0.330 | 10 V p-p input → 3.3 V p-p ADC swing; full 12-bit resolution across ±5 V |
| DC bias source | REF5025 2.5 V → 2× 10 kΩ divider → non-inverting pin | Shares the precision reference already on board; matched resistors for tempco tracking |
| Op-amp | OPA1642AIDR (dual JFET, RRO) | Same part family as U7/U10; 1 mV Vos, 5.1 nV/√Hz, high input Z suits 200 kΩ R_in |
| Op-amp rails | ±12 V | Lets the stage handle the full input range without clipping before attenuation |
| ADC sample | 12-bit, internal VREF+ = +3V3 (WeAct LDO) | F405 ADC1, single-ended, 56-cycle sample time |
| Input protection | Passive — series R_in 100 kΩ limits fault current into op-amp; BAT54SLT1G clamp on ADC node | No diodes on jack tip; op-amp handles the heavy lifting |
| Normalization | Switched jack tip → GND when unplugged | Unpatched input reads as 0 V CV (mid-scale ADC, "no modulation") |
| Anti-alias filter | 1 kΩ + 100 nF at op-amp output (fc ≈ 1.6 kHz) | Matched to CV modulation bandwidth; rejects RF, switcher noise, and op-amp broadband before the ADC sees it |

---

## 2. Topology

Each channel is an **inverting attenuator with DC bias injected on the
non-inverting pin**. The bias shifts the virtual ground away from 0 V
so the op-amp output sits at mid-rail (1.65 V) when the input is 0 V,
using only a single positive ADC rail at the downstream side.

```
                             R_f 33.0 kΩ
                   ┌──────────/\/\/\──────────┐
                   │                          │
JACK_TIP ──/\/\/\──┤−                         │
         R_in      │    OPA1642               │
         100 kΩ    │    (½ of U23)            │
                   │                          │
                   │                          ├──/\/\/\──┬──────► V_ADC (PA0 or PA1)
                   │                          │  R_ao    │
                   │                          │  1 kΩ    │
                   │                          │          │
                   │                          │          ├── C_ao 100 nF ── GND
                   │                          │          │
                   │                          │          ├── D_clamp BAT54SLT1G ── GND / +3V3
                   │                          │          │
                   │                          │          └──► STM32 PA0/PA1
                   │                          │
                   │   +12V ──┤V+             │
                   │          │               │
                   │   −12V ──┤V−             │
                   │          │               │
                   │                          │
                   └──┤+                      │
                      │                       │
                      V_BIAS  (1.2500 V nom)  │
                      │                       │
                      │                       │
       REF5025 ──────/\/\/\──┬─/\/\/\── GND   │
        2.500 V   R_b1 10.0k │  R_b2 10.0k    │
                              │                │
                              C_b 2.2 nF        │
                              │                │
                              GND              │
```

### Transfer function

For a single channel (inverting summing amp with bias on non-inv pin):

    V_out = V_bias · (1 + R_f/R_in) − (R_f/R_in) · V_in

With R_f = 33.0 kΩ, R_in = 100 kΩ:

    R_f / R_in = 0.330
    gain = 1.330 on V_bias, −0.330 on V_in

V_bias comes from the REF5025 2.5 V via a 2× 10.0 kΩ divider:

    V_bias = 2.500 · 10.0 / (10.0 + 10.0) = 1.2500 V

    V_out = 1.2500 · 1.330 − 0.330 · V_in
          = 1.6625 − 0.330 · V_in

| V_in | V_out | ADC code (12-bit, 3.3 V ref) |
|---|---|---|
| +10.00 V (over-range) | −1.64 V → clamped to 0 V by BAT54SLT1G | 0 (saturated) |
| +5.00 V  | 0.013 V → clamped to 0 V | 0 |
| +2.50 V  | 0.838 V | ~1040 |
| 0.00 V   | 1.663 V | ~2063 |
| −2.50 V  | 2.488 V | ~3087 |
| −5.00 V  | 3.313 V | ~4110 |
| −10.00 V (over-range) | +4.96 V → clamped to 3.3 V by BAT54SLT1G | 4095 (saturated) |

The nominal ±5 V range maps to very nearly the full 12-bit ADC span,
with ~17 mV of zero-offset from the ideal midpoint (about 5 LSBs at
the ADC, ~50 mV referred to the input) that calibrates out in
firmware. Using matched 10 kΩ resistors gives better tempco tracking
than mismatched values. Over-range input up to ±12 V is passively clipped by
the BAT54SLT1G at the ADC node through R_ao — the stage is not damaged
and no ADC code escapes the 0…4095 range, which is exactly the
behaviour a Eurorack user expects when they over-drive a modulation
input.

**Firmware note:** the stage is inverting, so `V_in = (1.663 − V_adc)
/ 0.330`. Store the inversion and the two calibration constants
(zero-offset, full-scale gain) in the same settings page as the CV
output calibration (`calibration.md`).

---

## 3. Component values (per channel, ×2)

| Ref | Value | Package | MPN / LCSC | Notes |
|---|---|---|---|---|
| R24, R28 | 100 kΩ 1 % | 0805 | Vishay CRCW0805100KFKEA (LCSC TODO) | Doepfer A-100 standard CV input impedance; also the fault-current limiter into the op-amp |
| R31, R32 | 33.0 kΩ 1 % | 0805 | Vishay CRCW080533K0FKEA (LCSC TODO) | Feedback; together with R_in sets the −0.330 attenuation |
| R25, R27 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line (or C17513) | Series resistor into ADC pin; part of anti-alias RC and isolates op-amp from C_ao capacitive load |
| C41, C42 | 100 nF X7R | 0805 | Existing BOM 100 nF line | Anti-alias cap to GND; fc ≈ 1.6 kHz with R_ao — matched to CV modulation bandwidth |
| D4, D5 | BAT54SLT1G | SOT-23 | ON Semi BAT54SLT1G / LCSC TODO | Dual Schottky (common anode); clamps ADC pin to GND and +3V3 rails |

Bias-generation network (**shared** between both channels — one
divider drives both non-inverting pins):

| Ref | Value | Package | MPN / LCSC | Notes |
|---|---|---|---|---|
| R30 | 10.0 kΩ 1 % | 0805 | existing 10 k line | Divider top, tied to REF5025 2.5 V; matched value with R29 for tempco tracking |
| R29 | 10.0 kΩ 1 % | 0805 | existing 10 k line | Divider bottom, tied to GND |
| C38 | 2.2 nF X7R | 0805 | Existing BOM 2.2 nF line | Noise filter at the divider tap; one cap for both channels |

Both divider resistors are the same value (10.0 kΩ), so 1 % tolerance
is sufficient — ratio error from matched-value parts is dominated by
tempco tracking rather than initial tolerance, and any residual offset
calibrates out in firmware.

**Op-amp:** one dual OPA1642AIDR package covers both channels. Call
it **U23** in the schematic. Rails
are the existing `+12V` and `−12V` nets from `power-supply.md`.
Decoupling: 100 nF 0805 at V+ and 100 nF 0805 at V−, each as close
to the package pins as possible; one 10 µF bulk cap per rail is
already provided by the OPA1642 clusters at U7/U10 (no need to
duplicate per op-amp — see `pcb-design.md`).

---

## 4. Protection strategy

The design relies on **two cheap, passive mechanisms** and does not
add diode clamps at the jack tip.

### 4.1 Fault current at the op-amp input

The 100 kΩ R_in is the only path from the jack tip to the op-amp
inverting node. For any patch-cord fault:

| Fault scenario | Voltage at jack | Current into op-amp |
|---|---|---|
| Normal modulation | ±5 V | ±50 µA |
| Over-driven modulation | ±10 V | ±100 µA |
| Patched into +12 V hot rail | +12 V | ~107 µA |
| Patched into −12 V hot rail | −12 V | ~107 µA |
| Reverse-plugged pitch CV | +15 V (extreme) | ~133 µA |

The OPA1642 absolute-max input is `(V−) − 0.5 V` to `(V+) + 0.5 V`,
which with ±12 V rails is ±12.5 V. At any plausible Eurorack fault,
the current through R_in is under 135 µA — well inside the part's
internal ESD clamp tolerance. No external diodes are needed on the
jack side.

### 4.2 ADC pin clamp (BAT54SLT1G)

The op-amp can drive its output to within ~0.2 V of either ±12 V rail,
which is obviously outside the STM32's 0–3.3 V ADC input range. The
1 kΩ `R_ao` limits current into the ADC pin under fault, and a
**BAT54SLT1G** dual Schottky at the ADC node clamps any overshoot to the
digital 3V3 rail (top) and GND (bottom):

- Worst case: op-amp output pinned at +12 V → V across R_ao ≈ 8.7 V
  → 8.7 mA through R_ao into the upper BAT54SLT1G diode. BAT54SLT1G handles
  200 mA pulse, 600 mA non-repetitive surge; 8.7 mA is trivial.
- Same analysis for the −12 V rail through the lower diode.

The STM32 pin itself has internal ESD clamps rated to a few mA, so
even without BAT54SLT1G the pin would probably survive; BAT54SLT1G is cheap
insurance for users who do unusual things with their patch cables.

### 4.3 No TVS on the jack tip

A bidirectional TVS (e.g. 15 V) on the jack could be added but is
**not required** — the 100 kΩ input resistor already limits any DC
fault to safe levels, and the OPA1642 internal clamps absorb ESD.
Skip the TVS to keep the input impedance pure (no leakage, no
nonlinear capacitance) and the BOM short.

---

## 5. STM32F405 pin allocation

| Signal | STM32 pin | Peripheral / AF | Notes |
|---|---|---|---|
| `CV-IN-A` | **PA0** | ADC1_IN0 (also ADC2_IN0, ADC3_IN0) | Port-A corner pin; easy to route to front panel |
| `CV-IN-B` | **PA1** | ADC1_IN1 (also ADC2_IN1, ADC3_IN1) | Adjacent to PA0 |

Both pins are **free** per the current `hardware-design-plan.md` pin
allocation and the `.ioc` state (neither is claimed by USB, SWD,
I²S3, SPI2, the encoder, OLED CS, DAC CS, or LSE).

### Why PA0/PA1 specifically

- Both map to `ADCx_IN0` and `ADCx_IN1` on **all three** ADC
  peripherals. If a future firmware revision wants to sample both
  jacks truly simultaneously (e.g. for X/Y vector modulation), the
  F405's dual-ADC regular-simultaneous mode is available on these
  pins without any reshuffling.
- Higher-than-spec source impedance via the 1 kΩ R_ao + 100 nF C_ao
  RC is well within F405 ADC recommendations (< 10 kΩ source Z for
  56-cycle sampling); the 100 nF acts as a charge reservoir that
  fully replenishes the S&H cap on each conversion.
- Corner of Port A, no crossing of the SPI2 / I²S3 signal groups
  that sit on Ports B and C.
- Leaves PC0–PC3 and PB0 as a reserve ADC pool for future expansion
  (extra CV, pots, VCA CV, etc.) without touching PA0/PA1.

### ADC configuration

| Setting | Value | Rationale |
|---|---|---|
| ADC peripheral | ADC1 | Single peripheral for both channels, scan mode |
| Resolution | 12-bit | 0.8 mV LSB at VREF+ = 3.3 V → ~4.9 mV referred to CV input |
| Sample time | 56 cycles (typ. 84 cycles if drift observed) | Source Z ≈ 1 kΩ (R_ao) || 200 kΩ ≈ 995 Ω; 56 cycles @ 21 MHz ADC clock gives ~2.7 µs acquisition — comfortable |
| Mode | Scan, continuous or triggered by timer | Firmware choice — TIM-triggered at ~1 kHz is typical for CV modulation |
| Oversampling (firmware) | 16× decimation in software | Trades 4 bits of rate for ~2 bits of noise-floor improvement; brings effective resolution to ~13.5 bits |

VREF+ for ADC1 is tied to the WeAct board's digital 3V3 rail (this
is how the WeAct module wires it and we do not change it). The
resulting absolute ADC accuracy is dominated by that rail's
tolerance (~1 %), but since firmware calibrates zero-offset and
gain per channel against the DAC output (see `calibration.md`), the
absolute rail error calibrates out. **Do not** attempt to repurpose
`+3V3_PREC` as VREF+ — the WeAct core board has its 3V3 pin bonded
to VDDA/VREF+ internally and rewiring it is out of scope.

---

## 6. Jack wiring and normalization

Use **switched** mono 3.5 mm jacks (PJ398SM or equivalent):

| Jack pin | Net |
|---|---|
| Tip | `CV-IN-A-TIP` / `CV-IN-B-TIP` — goes to R_in |
| Tip switch (normalled) | **GND** — unpatched input = 0 V |
| Sleeve | GND |

When nothing is plugged in, the tip switch ties the tip to GND, so
the ADC reads ~1.65 V (mid-scale code 2048) and firmware interprets
this as "no modulation". Inserting a plug breaks the normal and
routes whatever the external source is providing.

If a channel ever needs to be normalled to a non-zero default (for
example, to bias a VCA halfway open when unpatched), drive the
switched contact from a dedicated bias tap — **do not** tap the
1.250 V V_BIAS node, because loading it will shift both channels'
zero point.

---

## 7. BOM additions

Items already in the BOM are noted; items to add are flagged.

| Component | Designator | MPN / LCSC | New? |
|---|---|---|---|
| OPA1642AIDR dual op-amp | U23 | TI OPA1642AIDR / **C67640** | Reuse BOM line (already on board as U7/U10) |
| 100 kΩ 1 % 0805 ×2 | R24, R28 | Vishay CRCW0805100KFKEA / LCSC TODO | **Add** |
| 33.0 kΩ 1 % 0805 ×2 | R31, R32 | Vishay CRCW080533K0FKEA / LCSC TODO | **Add** |
| 1.0 kΩ 1 % 0805 ×2 | R25, R27 | existing 1 k line | Reuse |
| 100 nF X7R 0805 ×2 | C41, C42 | existing 100 nF line | Reuse |
| BAT54SLT1G dual Schottky ×2 | D4, D5 | ON Semi BAT54SLT1G / LCSC TODO | **Add (×2)** |
| 10.0 kΩ 1 % 0805 ×2 | R29, R30 | existing 10 k line | Reuse |
| 2.2 nF 0805 X7R | C38 | existing 2.2 nF line | Reuse |
| 100 nF 0805 ×2 | C39, C40 | existing 100 nF line | Reuse |
| PJ398SM switched 3.5 mm jack ×2 | J_CV1, J_CV2 | PJ398SM / LCSC TODO | **Add (×2)** |

---
