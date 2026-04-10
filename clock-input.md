# Clock Input -- External Clock Jack → STM32F405 Timer Input Capture

This document is the schematic-side wiring spec for the external clock
input on the Tachyon board:

- **J_CLK** — 3.5 mm switched mono jack (PJ398SM or equivalent)
- **PA2** — STM32F405 GPIO / TIM2_CH3 input capture
- **Internal fallback clock** — firmware-generated BPM clock when
  nothing is patched

The input accepts standard Eurorack +5 V clock/trigger signals at any
PPQN from 1 to 24 (configurable in firmware) and a BPM range of
40–240 BPM.

Source references:
- `datasheets/STM32F405RG.md` — timer input capture, 5V-tolerant pins
- `hardware-design-plan.md` — available GPIO, timer peripherals
- `power-supply.md` — `+3V3` rail definitions
- `cv-input.md` — BAT54S clamping strategy (reused here)

---

## 1. Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Number of inputs | 1 | Single clock/trigger input |
| Nominal input | +5 V trigger/gate, 0 V low | Standard Eurorack clock level |
| Absolute max input | ±12 V (clamped) | BAT54S clamp + series R protects the GPIO; stage is not damaged |
| BPM range | 40–240 BPM | Covers typical musical tempo range |
| PPQN range | 1–24 (firmware-configurable) | At 240 BPM / 24 PPQN the max edge rate is 96 Hz — trivial for input capture |
| Detection method | Timer input capture (TIM2_CH3) | Hardware timestamping of rising edges; jitter-free period measurement at 32-bit resolution |
| Internal fallback | TIM-based software clock | When no external edges are detected within a timeout, firmware generates its own BPM clock |
| Jack normalization | Switched jack tip → GND when unplugged | Unpatched input is held LOW; no spurious edges, firmware sees silence and uses internal clock |
| Input threshold | STM32 Schmitt-trigger GPIO input | VIH ≈ 2.0 V on 5V-tolerant (FT) pin; clean switching on +5 V triggers |

---

## 2. Topology

The input stage is deliberately minimal — a series resistor, a small
filter cap, and a Schottky clamp — because the STM32F405's 5V-tolerant
GPIO pins already have built-in Schmitt-trigger inputs suitable for
digital clock signals.

```
                        R17 1 kΩ
JACK_TIP (U19) ──/\/\/\──┬──────────────────────► PA2 (TIM2_CH3)
                          │
                          ├── C37 2.2 nF ── GND
                          │
                          └── D3 BAT54SLT1G ── GND / +3V3
```

### Signal path

1. **Jack tip** receives the external clock signal (0/+5 V typical)
2. **R17 (1 kΩ)** limits fault current into the clamp diodes and
   GPIO under over-voltage conditions
3. **C37 (2.2 nF)** with R17 forms a low-pass filter at
   fc ≈ 72 kHz — passes clock edges cleanly (fastest edge rate at
   240 BPM / 24 PPQN is 96 Hz) while rejecting RF and switcher noise
4. **D3 (BAT54SLT1G)** clamps the filtered node to the 0–3.3 V
   range (same strategy as `cv-input.md` §4.2)
5. **PA2** reads the digital level via the internal Schmitt trigger;
   TIM2_CH3 captures the rising-edge timestamp in hardware

### Jack switch behaviour

The PJ398SM switched jack ties the tip to GND when no plug is
inserted. This holds PA2 LOW continuously — no edges, no interrupts.
Firmware interprets the absence of edges (timeout > 1.5 s, i.e.
below 40 BPM at 1 PPQN) as "no external clock" and runs the internal
BPM generator instead.

When a plug is inserted, the switch opens, and the tip receives
whatever the upstream module is sending. The first valid rising edge
causes firmware to lock onto the external clock and disable the
internal generator. If edges stop arriving (cable pulled, upstream
module stopped), the timeout fires and firmware reverts to internal
clock seamlessly.

---

## 3. Internal clock

When no external clock is detected, firmware runs a timer-based
internal clock:

| Parameter | Value | Notes |
|---|---|---|
| BPM range | 40–240 | Set via encoder / menu |
| Resolution | Sub-ms period accuracy | TIM2 at 84 MHz gives ~12 ns tick; period jitter is negligible |
| PPQN | 1–24, user-configurable | Internal clock generates edges at `60 / (BPM × PPQN)` second intervals |
| Output | Software event (same ISR path as external capture) | Downstream firmware sees identical tick events regardless of source |

The internal clock uses a separate TIM channel (or a second timer) in
output-compare mode to generate periodic interrupts at the configured
BPM and PPQN. The firmware clock API abstracts the source: consumers
receive a tick callback and a `clock_source` flag (internal/external)
but do not need to handle the two cases differently.

### External-to-internal handoff

When the external clock is active, firmware measures the inter-edge
period via TIM2_CH3 input capture. If edges stop (timeout), the
internal clock picks up at the **last measured tempo** so the sequence
continues without an abrupt tempo jump. The user can then adjust BPM
manually via the encoder.

### PPQN detection / configuration

PPQN is not auto-detected by default — the user sets it in the menu
(default: 1 PPQN). Firmware divides or multiplies incoming edges as
needed:

| Setting | Meaning |
|---|---|
| 1 PPQN | One clock edge = one step (quarter note) |
| 2 PPQN | Two edges per step — firmware counts every 2nd edge |
| 4 PPQN | Four edges per step (common for DIN sync / some sequencers) |
| 24 PPQN | Twenty-four edges per step (MIDI clock rate) |

If auto-detection is desired in a future revision, firmware can
measure the edge rate against a known BPM range and infer PPQN — but
this is a firmware feature, not a hardware concern.

---

## 4. Protection

### 4.1 Over-voltage (BAT54S clamp)

Same strategy as `cv-input.md` §4.2. R17 (1 kΩ) limits current
into the BAT54SLT1G clamp diodes at the GPIO node:

| Fault scenario | Voltage at jack | Current through R17 |
|---|---|---|
| Normal +5 V clock | +5 V | ~1.7 mA (5 V − 3.3 V across 1 kΩ) |
| Patched to +12 V | +12 V | ~8.7 mA into upper diode |
| Patched to −12 V | −12 V | ~12 mA into lower diode |

All currents are well within BAT54SLT1G ratings (200 mA pulse). The GPIO
pin never sees more than ~3.6 V or less than ~−0.3 V (diode forward
drop).

### 4.2 No TVS needed

The 1 kΩ series resistor limits DC fault current to safe levels and
the BAT54SLT1G diodes absorb transients. No TVS is required — same
reasoning as `cv-input.md` §4.3.

---

## 5. STM32F405 pin allocation

| Signal | STM32 pin | Peripheral / AF | Notes |
|---|---|---|---|
| `CLK_IN` | **PA2** | TIM2_CH3 (AF1) / TIM5_CH3 (AF2) / TIM9_CH1 (AF3) | 5V-tolerant (FT) pin; input capture for period measurement |

PA2 is **free** per the current `hardware-design-plan.md` pin
allocation. It is not claimed by USB, SWD, I2S3, SPI2, the encoder,
OLED, DAC CS, or the CV inputs (PA0/PA1).

### Why PA2 specifically

- Maps to **TIM2_CH3** — TIM2 is a 32-bit timer clocked at 84 MHz
  (APB1 × 2), giving ~12 ns resolution and a 51-second overflow
  period. At 40 BPM / 1 PPQN the inter-edge period is 1.5 s; at
  240 BPM / 24 PPQN it is ~10.4 ms. Both fit comfortably in 32 bits
  with no overflow handling needed for single-period measurement.
- Also available on TIM5_CH3 (another 32-bit timer) and TIM9_CH1
  (16-bit), giving flexibility if TIM2 is needed elsewhere.
- Adjacent to PA0/PA1 (CV inputs) — clean routing in Zone A of the
  PCB.
- Leaves PA3 and higher Port A pins free for future expansion (reset
  input, gate outputs, etc.).

### GPIO configuration

| Setting | Value | Rationale |
|---|---|---|
| Mode | Alternate function (TIM2_CH3, AF1) | Hardware input capture |
| Pull | Pull-down | Ensures pin reads LOW when jack is unplugged (belt-and-suspenders with the jack switch to GND) |
| Speed | Low (2 MHz) | Input pin; speed setting is irrelevant but low minimises EMI |
| Input capture edge | Rising edge | Captures the leading edge of each clock pulse |
| Input capture prescaler | /1 (every edge) | Firmware handles PPQN division in software |
| Input capture filter | 4 samples at fDTS/2 | Minimal hardware debounce; rejects sub-microsecond glitches without adding latency to real edges |

---

## 6. Jack wiring

Use a **switched** mono 3.5 mm jack (PJ398SM or equivalent):

| Jack pin | Net |
|---|---|
| Tip | `CLK_IN_TIP` — goes to R_clk |
| Tip switch (normalled) | **GND** — unplugged = tip held LOW |
| Sleeve | GND |

When nothing is plugged in, the tip switch ties the tip to GND. PA2
reads a steady LOW, no input capture interrupts fire, and firmware
runs the internal clock.

Inserting a plug breaks the normal and routes the external clock
signal to the conditioning network. The first rising edge triggers
input capture and firmware locks to the external source.

---

## 7. Component values

| Ref | Value | Package | MPN / LCSC | Notes |
|---|---|---|---|---|
| R17 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line | Series current limiter; part of RC filter |
| C37 | 2.2 nF C0G/NP0 | 0805 | LCSC TODO | Filter cap; C0G for stable capacitance. fc ≈ 72 kHz with R17 |
| D3 | BAT54SLT1G | SOT-23 | BAT54SLT1G / LCSC TODO | Dual Schottky (common anode); clamps to +3V3 and GND |
| U19 | PJ398SM switched 3.5 mm jack | — | PJ398SM / LCSC TODO | Same jack type as J_CV1/J_CV2 |

All passives except C37 are already in the BOM.

---

## 8. Firmware notes

### Clock API sketch

```
typedef struct {
    uint32_t period_ticks;     // TIM2 ticks between last two edges
    uint32_t bpm_x100;        // derived BPM × 100 for 0.01 BPM resolution
    uint8_t  ppqn;            // user-configured PPQN (1–24)
    bool     external;        // true = external clock active
} clock_state_t;
```

- **Input capture ISR:** On each TIM2_CH3 capture event, compute
  `period = CCR_now − CCR_prev`. Convert to BPM:
  `bpm = 60 × f_timer / (period × ppqn)`. Apply a simple moving
  average (4–8 samples) to smooth jitter from upstream analog clocks.
- **Timeout:** If no capture event within `max_period` ticks
  (corresponding to 40 BPM at the configured PPQN), set
  `external = false` and start the internal clock at the last known
  BPM.
- **Internal clock:** Output-compare interrupt on a TIM channel,
  period set from the BPM/PPQN menu setting. Calls the same tick
  handler as the external capture path.
- **Swing / shuffle:** Can be applied in firmware by delaying
  alternate ticks — this is independent of the clock source and does
  not affect the hardware design.

### Tempo display

The measured or set BPM should be shown on the OLED status bar. When
external clock is active, show the measured BPM and a plug icon (or
"EXT" label). When internal, show the set BPM and "INT".

---
