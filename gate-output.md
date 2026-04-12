# Gate Output -- STM32F405 GPIO → 2N7002K Level Shift → +5 V Gate Jacks

This document is the schematic-side wiring spec for the two gate/trigger
outputs on the Tachyon board:

- **GATE A** — sequencer gate / trigger / accent output
- **GATE B** — second gate / trigger output (auxiliary pattern, clock out, etc.)

Each output is a 0/+5 V digital signal suitable for driving any Eurorack
gate/trigger input. The STM32F405 GPIO (3.3 V logic) is level-shifted to
+5 V using an N-channel MOSFET inverting driver with a pull-up to the `+5V`
rail.

Source references:
- `hardware-design-plan.md` — available GPIO, timer peripherals
- `power-supply.md` — `+5V` rail definition and current budget
- `clock-input.md` — jack wiring conventions (PJ398SM)
- `cv-input.md` — protection strategy reference

---

## 1. Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Number of outputs | 2 (Gate A, Gate B) | Two independent gate/trigger channels |
| Output levels | 0 V (low) / +5 V (high) | Standard Eurorack gate level |
| Drive method | N-channel MOSFET (2N7002K-T1-GE3) inverting driver | Clean 0/+5 V swing from 3.3 V GPIO; simple, low part count |
| Pull-up rail | `+5V` (Eurorack) | Already available; no extra regulator needed |
| Output impedance | 1 kΩ (series protection resistor) | Short-circuit protection; limits current to 5 mA at +5 V |
| Rise/fall time | < 1 µs into 10 nF load | 2N7002 Rds(on) ≈ 5 Ω; 10 kΩ pull-up gives ~100 µs rise, but downstream module input impedance (typically 100 kΩ) keeps edges fast; adequate for gate signals |
| Timer capability | PA3 = TIM2_CH4, PA6 = TIM3_CH1 | Hardware output-compare for jitter-free gate timing |

---

## 2. Topology

Each gate output channel is identical:

```
                          Q2 (2N7002K-T1-GE3)
                          ┌───┐
PA3 (GATE-OUT-A) ──R20 1kΩ──┤G  D├──┬── R18 10kΩ ──── +5V
                          │   S│  │
                          └───┘  └── R19 1kΩ ──── JACK A TIP (U20)
                            │
                           GND
```

Channel A: PA3 → R20 (1 kΩ) → Q2 gate; Q2 drain → R18 (10 kΩ) pull-up to +5V, R19 (1 kΩ) to jack U20
Channel B: PA6 → R23 (1 kΩ) → Q3 gate; Q3 drain → R21 (10 kΩ) pull-up to +5V, R22 (1 kΩ) to jack U21

### Signal path

1. **STM32 GPIO** drives 0 or 3.3 V
2. **R20/R23 (1 kΩ)** series gate resistor — limits inrush into MOSFET
   gate capacitance, damps ringing on the trace
3. **Q2/Q3 (2N7002K-T1-GE3)** N-channel MOSFET — switches drain to GND
   when gate is HIGH. Vgs(th) is 1.0–2.5 V; 3.3 V GPIO drives it fully
   enhanced (Rds(on) ≈ 2–5 Ω at Vgs = 3.3 V)
4. **R18/R21 (10 kΩ)** pull-up to `+5V` — pulls drain to +5 V when
   MOSFET is OFF (gate LOW)
5. **R19/R22 (1 kΩ)** output protection — limits short-circuit current
   to 5 mA, provides defined output impedance

### Logic inversion

The MOSFET stage inverts the signal:

| GPIO state | MOSFET | Output at jack |
|---|---|---|
| LOW (0 V) | OFF | +5 V (gate HIGH) |
| HIGH (3.3 V) | ON | 0 V (gate LOW) |

Firmware must invert: write GPIO **HIGH** to assert gate LOW at the jack,
write GPIO **LOW** to assert gate HIGH. Alternatively, use timer
output-compare in toggle mode with inverted polarity
(`TIM_OCPolarity_Low`).

---

## 3. STM32F405 pin allocation

| Signal | STM32 pin | Peripheral / AF | Notes |
|---|---|---|---|
| `GATE-OUT-A` | **PA3** | TIM2_CH4 (AF1) / TIM5_CH4 (AF2) / TIM9_CH2 (AF3) | 5V-tolerant (FT) pin; adjacent to PA2 (clock input) for clean routing |
| `GATE-OUT-B` | **PA6** | TIM3_CH1 (AF2) / TIM13_CH1 (AF9) | 5V-tolerant (FT) pin; available per pin budget |

Both pins are **free** per `hardware-design-plan.md`. PA3 is adjacent to
PA2 (clock input) on Port A, keeping clock/gate signals in the same PCB
zone. PA6 provides an independent timer (TIM3) so both gates can fire
simultaneously without contention.

### GPIO configuration (simple mode)

| Setting | Value | Rationale |
|---|---|---|
| Mode | GPIO output push-pull | Direct drive of MOSFET gate |
| Pull | None | R20/R23 + MOSFET gate capacitance define the DC state; no pull needed |
| Speed | Low (2 MHz) | Gate signals are < 1 kHz; low speed minimises EMI |
| Initial state | HIGH | MOSFET ON → output LOW at jack (gate de-asserted at power-up) |

### Timer mode (hardware-timed gates)

For jitter-free gate pulses, configure the timer channel in output-compare
mode:

| Setting | Value |
|---|---|
| Timer | TIM2_CH4 (Gate A) or TIM3_CH1 (Gate B) |
| Mode | Output compare, toggle or PWM mode 1 |
| Polarity | Active LOW (inverted, to compensate MOSFET inversion) |
| Auto-reload | Set from BPM/PPQN period |
| Pulse width | Configurable gate length (e.g. 50 %, 25 ms fixed, or trigger pulse) |

Using TIM2_CH4 for Gate A shares the same timer as the clock input
(TIM2_CH3) — this allows the firmware to synchronise gate output edges
directly to captured clock edges with zero software latency.

---

## 4. Power

Gate outputs draw from the `+5V` Eurorack rail only (via the pull-up
resistors). Per-channel quiescent current:

| State | Current from +5V |
|---|---|
| Gate HIGH (MOSFET off) | ~0.05 mA (pull-up into downstream 100 kΩ input) |
| Gate LOW (MOSFET on) | ~0.5 mA (5 V / 10 kΩ pull-up, through MOSFET to GND) |

Total worst-case for both channels: ~1 mA from `+5V` — negligible in the
power budget.

---

## 5. Jack wiring

Use **non-switched** mono 3.5 mm jacks (PJ398SM or equivalent). Gate
outputs do not require normalling — when unpatched, the output simply
drives into an open circuit (pull-up holds the net at +5 V or MOSFET pulls
to GND, no issue either way).

| Jack pin | Net (Gate A) | Net (Gate B) |
|---|---|---|
| Tip | `GATE-OUT-A` (from R19) | `GATE-OUT-B` (from R22) |
| Sleeve | GND | GND |

---

## 6. Component values

| Ref | Value | Package | MPN / LCSC | Notes |
|---|---|---|---|---|
| Q2 | 2N7002K-T1-GE3 | SOT-23 | 2N7002K-T1-GE3 / LCSC TODO | N-ch MOSFET, gate output A level shifter |
| Q3 | 2N7002K-T1-GE3 | SOT-23 | 2N7002K-T1-GE3 / LCSC TODO | N-ch MOSFET, gate output B level shifter |
| R18 | 10 kΩ 1 % | 0805 | Existing BOM 10 k line | Pull-up to +5V, gate A |
| R19 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line | Output protection A |
| R20 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line | Series gate resistor A |
| R21 | 10 kΩ 1 % | 0805 | Existing BOM 10 k line | Pull-up to +5V, gate B |
| R22 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line | Output protection B |
| R23 | 1.0 kΩ 1 % | 0805 | Existing BOM 1 k line | Series gate resistor B |
| U20 | PJ398SM 3.5 mm jack (switched) | — | PJ398SM / LCSC TODO | Gate A output jack |
| U21 | PJ398SM 3.5 mm jack (switched) | — | PJ398SM / LCSC TODO | Gate B output jack |

All passives are already in the BOM. Only the 2N7002K-T1-GE3 MOSFETs are new.

---

## 7. Firmware notes

### Gate API sketch

```
typedef enum {
    GATE_A = 0,
    GATE_B = 1,
} gate_channel_t;

typedef enum {
    GATE_MODE_GATE,     // held high for note duration
    GATE_MODE_TRIGGER,  // fixed-width pulse (e.g. 10 ms)
} gate_mode_t;
```

- **Gate mode:** Output stays HIGH for the duration of the sequencer
  step (or note-on to note-off). Firmware clears the GPIO (sets HIGH
  due to inversion) at step start, and sets it (LOW output) at step
  end or on next rest.
- **Trigger mode:** Fixed-width pulse (e.g. 10 ms) at the start of
  each active step. Use timer output-compare to generate the pulse
  width in hardware — set a one-shot compare event that auto-clears
  after the pulse period.
- **Accent:** Gate A could double as an accent output in some modes,
  outputting a second trigger pulse or a longer gate for accented
  steps.

### Inversion helper

```c
// MOSFET inverts: GPIO HIGH = output LOW, GPIO LOW = output HIGH
static inline void gate_set(gate_channel_t ch, bool active) {
    HAL_GPIO_WritePin(gate_port[ch], gate_pin[ch],
                      active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
```

### Power-up state

GPIO initialises HIGH → MOSFET ON → output LOW. This ensures gates are
de-asserted at power-up with no spurious trigger pulses.

---
