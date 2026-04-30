# CV Output DAC -- DAC8552 (U6) + OPA1642 (U7) + REF5025 (U2) Wiring Spec

This document is the schematic-side wiring spec for the precision CV output
chain on the Tachyon board:

- **U6 — DAC8552IDGKR** — 16-bit dual-channel SPI DAC (raw 0–2.5 V output)
- **U2 — REF5025IDR** — 2.5 V precision voltage reference driving DAC8552 VREF
- **U7 — OPA1642AIDR** — dual JFET op-amp, ×4 non-inverting gain stage to 0–10 V

The symbols are already placed; this file enumerates every net, decoupling
component, feedback resistor, and MCU pin assignment needed to wire the
pitch-critical CV path in EasyEDA Pro.

Source references:
- `datasheets/DAC8552.md` — pinout, SPI protocol, decoupling rules
- `datasheets/REF5025.md` — pinout, noise-reduction pin, decoupling
- `datasheets/OPA1642.md` — pinout, rail-to-rail headroom, capacitive-load notes
- `hardware-design-plan.md` §"DAC 16-bit Precision DAC" and §"Op-Amp Output Stage — Mandatory"
- `power-supply.md` — rail definitions (`+3V3_PREC`, `+5V`, `±12V`), LDOs, current budget
- `ref-dac-amp.png` — reference schematic (feedback topology, resistor values)

---

## 1. Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| DAC resolution | 16-bit | ~0.013 cents per step at 1 V/oct — full calibration headroom |
| DAC channels | 2 (DAC A, DAC B) | One per CV output; no spare channels for accent/mod |
| DAC VREF | 2.500 V from REF5025 (pin 6) | Sets DAC full-scale to 2.500 V |
| DAC SPI mode | Mode 1 (CPOL = 0, CPHA = 1) | Clocks in on falling SCLK edge |
| SPI bus | SPI2 (dedicated — OLED is on separate SPI1) | No bus contention or SPI mode conflict |
| Reference | REF5025IDR, 2.5 V, 3 ppm/°C | Drives DAC8552 VREF at 2.500 V |
| Op-amp | OPA1642AIDR (dual JFET, rail-to-rail) | 5.1 nV/√Hz noise, 1 mV typ Vos, ±11.8 V swing on ±12 V |
| Topology | Non-inverting, G = ×4 | Scales 0–2.500 V DAC to 0–10.000 V CV |
| Full-scale CV output | 0 V to +10 V unipolar | 10 octaves of 1 V/oct |
| Output protection | 1 kΩ series resistor per jack | Short-circuit limit + stability + defined Zout |

---

## 2. STM32F405 pin allocation

SPI2 is dedicated to the DAC8552 — the SSD1327 OLED runs on its own SPI1 bus
(see `user-interface.md` §1). This eliminates any bus contention between
display refreshes and timing-critical DAC updates.

| DAC8552 pin | Net name | STM32 pin | Peripheral / AF |
|---|---|---|---|
| 5 SYNC (~CS) | `DAC-SPI-CS` | **PB1** | GPIO output, push-pull, idle HIGH |
| 6 SCLK | `DAC-SPI-SCLK` | **PB13** | SPI2_SCK (AF5) |
| 7 DIN | `DAC-SPI-MOSI` | **PB15** | SPI2_MOSI (AF5) |

Notes:
- **`DAC-SPI-CS`** must idle HIGH. The DAC8552 clocks DIN on the falling edge
  of SCLK only while SYNC is LOW, and latches the 24-bit frame on the 24th
  falling edge; a premature rising edge on SYNC aborts the transfer
  (`DAC8552.md:46,72`). Firmware drives CS LOW for exactly 24 SCLK cycles
  per write.
- **SPI mode 1 (CPOL = 0, CPHA = 1).** The device also accepts mode 2
  (`DAC8552.md:71`), but pick mode 1 in the HAL config and do not change it.
- **No bus sharing.** The OLED has been moved to SPI1 (PA5/PA7) so SPI2 is
  now dedicated to the DAC8552. SPI2 remains permanently in Mode 1 — no
  CPHA switching needed. See `user-interface.md` §1.2 for rationale.

The REF5025 and OPA1642 consume **no MCU pins** — both are purely analog.

---

## 3. Pin-by-pin connection table

### U6 — DAC8552IDGKR (VSSOP-8)

| Pin | Name | Connection |
|---|---|---|
| 1 | VDD | `+3V3_PREC` via C18 (1 µF) + C19 (100 nF) decoupling |
| 2 | VREF | `VREF_2V5` from U2 (REF5025) pin 6, decoupled with C20 (1 µF) + C21 (100 nF) |
| 3 | VOUTB | To U7.1 +IN A (pin 3) — drives **CV-OUT-A** jack (channels cross-wired — see note below) |
| 4 | VOUTA | To U7.2 +IN B (pin 5) — drives **CV-OUT-B** jack (channels cross-wired — see note below) |
| 5 | SYNC | `DAC-SPI-CS` → STM32 PB1 |
| 6 | SCLK | `DAC-SPI-SCLK` → STM32 PB13 (shared SPI2 SCK) |
| 7 | DIN | `DAC-SPI-MOSI` → STM32 PB15 (shared SPI2 MOSI) |
| 8 | GND | GND (Layer 2 plane) |

> **Channel swap (PCB-placement driven):** DAC8552 `VOUTA` (pin 4) feeds the
> CV-OUT-**B** jack via U7.2, and `VOUTB` (pin 3) feeds the CV-OUT-**A** jack
> via U7.1. Firmware must therefore set DB[21] = **1** (DAC channel B) to
> drive the physical CV-OUT-A jack, and DB[21] = **0** (DAC channel A) for
> CV-OUT-B. The bit flip lives in the DAC driver; the `cv_cal[]` table in
> `calibration.md` remains indexed by physical jack (0 = CV-OUT-A,
> 1 = CV-OUT-B) so calibration and user-facing menus are unaffected.

### U2 — REF5025IDR (SOIC-8)

| Pin | Name | Connection |
|---|---|---|
| 1 | DNC | Do not connect (leave floating) |
| 2 | VIN | `+3V3_PREC` via C15 decoupling |
| 3 | TEMP | No connect (leave floating) |
| 4 | GND | GND (Layer 2 plane) |
| 5 | TRIM/NR | Noise-reduction cap C17 (100 nF) to GND |
| 6 | VOUT | `VREF_2V5` → U6 DAC8552 pin 2 via C16 decoupling |
| 7 | NC | No connect |
| 8 | DNC | Do not connect (leave floating) |

### U7 — OPA1642AIDR (SOIC-8, dual)

U7.1 (OPA1642 unit A) — drives **CV-OUT-A** jack, fed from DAC8552 **VOUTB**:

| Pin | Name | Connection |
|---|---|---|
| 1 | OUT A | CV_A net → R13 1 kΩ → CV jack A tip; also → R3 (30 kΩ) feedback tap |
| 2 | -IN A | Junction of R3 (30 kΩ to OUT A) and R4 (10 kΩ to GND) |
| 3 | +IN A | U6 pin 3 (VOUTB) — DAC channel B raw output |
| 4 | V- | `-12V` (Eurorack) via C23 (100 nF) |

U7.2 (OPA1642 unit B) — drives **CV-OUT-B** jack, fed from DAC8552 **VOUTA**:

| Pin | Name | Connection |
|---|---|---|
| 5 | +IN B | U6 pin 4 (VOUTA) — DAC channel A raw output |
| 6 | -IN B | Junction of R5 (30 kΩ to OUT B) and R6 (10 kΩ to GND) |
| 7 | OUT B | CV_B net → R14 1 kΩ → CV jack B tip; also → R5 (30 kΩ) feedback tap |
| 8 | V+ | `+12V` (Eurorack) via C22 (100 nF) |

All "GND" connections above land on the single continuous Layer 2
ground plane defined in `pcb-design.md` §3. There is no separate AGND /
DGND net; the DAC8552 GND (pin 8) and REF5025 GND (pin 4) each get
their own via to Layer 2 placed directly adjacent to the pin.

---

## 4. Power rails

Rail definitions, LDO selection, current budget, and sequencing live in
**`power-supply.md`**. This document only specifies which rail each pin
lands on:

| Part | Pin | Rail |
|---|---|---|
| U6 DAC8552 | VDD (pin 1) | `+3V3_PREC` |
| U2 REF5025 | VIN (pin 2) | `+3V3_PREC` (0.6 V over the REF5025 VS_min = 2.7 V hard floor per datasheet §6.5; moved onto the LDO rail so U2 can sit next to U6 and keep the `VREF_2V5` trace < 20 mm per §6) |
| U7 OPA1642 | V+ (pin 8) / V− (pin 4) | `+12V` / `−12V` (Eurorack) |

---

## 4a. Decoupling and passives BOM

Place every cap **as close to its pin as possible**, on the same side as the
part where practical, with short fat traces to GND.

### U6 — DAC8552 decoupling (per `DAC8552.md:74`)

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C18 | 1 µF | 0805 X7R | VDD (pin 1) – GND | Bulk on `+3V3_PREC` |
| C19 | 100 nF | 0805 | VDD (pin 1) – GND | HF bypass, closest to pin |
| C20 | 1 µF | 0805 X7R | VREF (pin 2) – GND | Bulk on `VREF_2V5`; low-impedance source is mandatory (`DAC8552.md:75`) |
| C21 | 100 nF | 0805 | VREF (pin 2) – GND | HF bypass, closest to pin |

### U2 — REF5025 decoupling (per `datasheets/REF5025.md` Application Notes)

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C15 | 10 µF | 0805 X5R/X7R | VIN (pin 2) – GND  | Bulk + HF bypass on `+5V` feed, close to pin |
| C16 | 10 µF | 0805 X5R/X7R | VOUT (pin 6) – GND | Stability + transient response |
| C17 | 100 nF | 0805           | TRIM/NR (pin 5) – GND | Broadband noise reduction; larger = quieter but slower startup |

### U7 — OPA1642 decoupling + feedback network

Per-rail decoupling (OPA1642 sits on ±12 V; `OPA1642.md:102`):

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C22 | 100 nF | 0805 X7R | V+ (pin 8) – GND | HF bypass on `+12V`, closest to pin. Bulk is handled by C1 at the input filter. |
| C23 | 100 nF | 0805 X7R | V− (pin 4) – GND | HF bypass on `−12V`, closest to pin. Bulk is handled by C2 at the input filter. |

Feedback network (×4 non-inverting gain, per `hardware-design-plan.md` §"Op-Amp Output Stage"):

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| R3 | 30 kΩ 0.1% | 0805 thin film | OUT A (pin 1) ↔ −IN A (pin 2) | Feedback resistor A; tap *at OUT A pin, before R13* |
| R4 | 10 kΩ 0.1% | 0805 thin film | −IN A (pin 2) – GND | Ground leg A |
| R5 | 30 kΩ 0.1% | 0805 thin film | OUT B (pin 7) ↔ −IN B (pin 6) | Feedback resistor B; tap *at OUT B pin, before R14* |
| R6 | 10 kΩ 0.1% | 0805 thin film | −IN B (pin 6) – GND | Ground leg B |

Exact gain: 1 + 30 k / 10 k = ×4.0000 → 2.5000 V × 4 = 10.000 V full-scale.
Use 0.1 % thin-film parts on R3–R6 so the untrimmed channel-to-channel gain
mismatch stays under 0.2 % before firmware calibration.

Output protection (per `hardware-design-plan.md` §"Op-Amp Output Stage"):

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| R13 | 1 kΩ 1% | 0805 thick film | OUT A (pin 1) → CV jack A tip | **Outside feedback loop** — R3 taps at the op-amp pin, then R13 is in series to the jack. Limits short-circuit current to 10 mA at 10 V, provides stability into cable capacitance, defines output Zout. Lives on the IO board. |
| R14 | 1 kΩ 1% | 0805 thick film | OUT B (pin 7) → CV jack B tip | Same as R13 for channel B. Lives on the IO board. |

BOM part for R13 / R14: **YAGEO AC0805FR-7W1KL** (1 kΩ, 0805, 1 %, 250 mW,
150 V, ±100 ppm/°C, LCSC **C727989**). 250 mW is 2.5× the 100 mW
worst-case short-circuit dissipation — sustained jack shorts at full scale
are handled indefinitely.

The board already stocks 100 nF 0805 and 10 µF 0805 from the audio-DAC
BOM line; reuse those values. The 0.1 % feedback resistors (R3–R6) must be
added to the BOM if not already present.

---

## 5. Output stage and jacks

The OPA1642 output stage drives the front-panel CV jacks through the
protection resistors:

- OUT A (U7 pin 1) → **R13** 1 kΩ → CV jack A (tip)
- OUT B (U7 pin 7) → **R14** 1 kΩ → CV jack B (tip)
- CV jack sleeves → GND (Layer 2 plane)
- Full-scale output: 0 V to +10 V unipolar (10 octaves at 1 V/oct)

**Feedback tap placement is critical:** R3 (and R5) must tap at the
op-amp output pin itself, *before* R13 (R14). Putting the protection
resistor outside the feedback loop decouples short-circuit survival from
output-voltage accuracy — a jack short can no longer drive the op-amp into
runaway correction. If the feedback is instead taken from the jack side of
R13 / R14, a short pulls the feedback node down and the op-amp rails trying
to compensate.

**No DC blocking capacitors** on the output — this is a DC-coupled CV
path, not audio. Eurorack 1 V/oct depends on the absolute DC level.

The ~1 % divider error between R13 / R14 (1 kΩ) and a typical Eurorack
1 V/oct input impedance (100 kΩ) is absorbed into firmware two-point slope
calibration at bring-up, alongside REF5025 tolerance, feedback-resistor
tolerance, and OPA1642 Vos. No static gain error remains after
calibration.

Place U7 adjacent to U6 and keep the OUT→R13/R14→jack runs short and on
Layer 1 with Layer 2 as the reference plane. R13 and R14 sit on the IO
board; CV jacks belong in the front-panel I/O zone per `pcb-design.md` §5.

---

## 6. Grounding

Ground topology is defined globally in **`pcb-design.md`** — Layer 2
is a single continuous GND plane, never split or cut, and partitioning
between analog and digital return currents is handled by component
placement on Layer 1, not by copper topology.

CV-path-specific notes on top of that:

- **U6 DAC8552 pin 8 (GND)** drops directly to Layer 2 via a via placed
  within < 1 mm of the pin. Do not daisy-chain.
- **U2 REF5025 pin 4 (GND)** gets its own via to Layer 2 placed within
  < 1 mm of the pin. Pins 1, 7, and 8 (DNC/NC) are left floating.
- **U7 OPA1642** has no dedicated GND pin; ground return for R4 and R6
  (the 10 kΩ feedback ground legs) must land on Layer 2 via a short
  trace close to the op-amp. Route these two ground-leg vias adjacent
  to the op-amp body, not back at the DAC or reference, so the feedback
  loop sees a local low-impedance return.
- **VREF routing:** `VREF_2V5` from U2 pin 6 to U6 pin 2 is a
  code-dependent load (`DAC8552.md:75`). Keep this trace short (< 20 mm),
  reasonably wide (0.3 mm+), and run it directly over Layer 2 — do not
  let it cross any split or any noisy corridor. C16 (at U2) and
  C20 / C21 (at U6) act as a distributed bypass along the trace.
- **+3V3_PREC routing:** feed U6 from the same LDO output node that feeds
  U2's decoupling area; do not star from the LDO with long separate runs.
- **Feedback node shielding:** R3/R5 (−IN nodes) are high-impedance
  summing junctions. Keep them short, away from the SPI corridor
  (PB13/PB15), and under U7's body if possible.

---

