# CV Output Calibration

Procedure for calibrating Tachyon's precision CV outputs
(CV-OUT-A and CV-OUT-B) so that each jack produces accurate 1 V/oct
pitch voltages across the full 0–10 V range.

This is a bring-up-time procedure, run once per assembled board and
stored in flash. Re-run if the board is moved to a dramatically
different temperature environment or if a downstream VCO is replaced
with one of very different input impedance.

Scope: this document covers only the precision CV path:

```
DAC8552 (VREF = REF5025 2.500 V)  →  OPA1642 non-inverting ×4
                                   →  R_PROT 1 kΩ  →  CV-OUT-A / CV-OUT-B jack
```

The audio DAC path (PCM5102A) is not calibrated — see `audio-output-dac.md`.

---

## 1. Why calibration is necessary

The nominal transfer function is:

```
V_jack_ideal = (DAC_code / 65535) × V_REF × Gain
             = (DAC_code / 65535) × 2.500 × 4
             = (DAC_code / 65535) × 10.000  V
```

In practice the actual jack voltage deviates from this because of a
chain of static linear errors, each contributing to either the
**slope** (V per DAC code) or the **offset** (V at DAC code 0):

| Source | Typical spread | Contribution |
|---|---|---|
| REF5025 initial accuracy | ±0.05% (±1.25 mV at 2.5 V) | Slope |
| REF5025 tempco | 3 ppm/°C max = ±30 ppm over 10 °C | Slope drift |
| DAC8552 INL | ±12 LSB max over 65536 codes = ±0.018% FS | Mostly slope, tiny curvature |
| DAC8552 DNL | ±1 LSB (guaranteed monotonic) | Below calibration resolution |
| R_f / R_g ratio (0.1% resistors) | √2 × 0.1% = ±0.14% | Slope |
| Feedback-resistor tempco mismatch | ~25 ppm/°C × 10 °C = ±250 ppm | Slope drift |
| R_PROT / Eurorack Z_in divider (1 kΩ / 100 kΩ) | −0.99%, load-dependent | Slope |
| OPA1642 Vos × Gain | ±3.5 mV × 4 = ±14 mV max | Offset |
| OPA1642 Vos drift | 1.5 µV/°C × 4 × 10 °C = ±60 µV | Offset drift |

Sum of static slope errors: roughly ±1.3% worst case before
calibration. At 1 V/oct that is **~15 cents of pitch error per
octave**, accumulating to a full semitone over the 10-octave range.
Audibly out of tune.

After two-point calibration these all collapse into two per-channel
constants stored in flash. Residual errors are from tempco drift
(negligible over normal operating swings) and DAC nonlinearity (well
below 1 cent and below the quantizer's step size anyway).

---

## 2. What gets calibrated

Two numbers per channel, stored per unit in the last flash page:

```c
typedef struct {
    float slope;     // volts per DAC code, typically ≈ 10.0 / 65535 = 152.6 µV/LSB
    float offset;    // volts at DAC code 0, typically ≈ 0 ± 15 mV
} cv_cal_t;

cv_cal_t cv_cal[2];  // index 0 = channel A, index 1 = channel B
```

To produce a target voltage `V_target`, firmware computes:

```c
uint16_t code = (uint16_t)roundf((V_target - cv_cal[ch].offset) / cv_cal[ch].slope);
if (code > 65535) code = 65535;
dac8552_write(ch, code);
```

Slope and offset are fitted from two measured points via the standard
two-point linear fit:

```
slope  = (V_high_measured − V_low_measured) / (code_high − code_low)
offset = V_low_measured − slope × code_low
```

---

## 3. Equipment required

Pick one of the two measurement paths:

**Path A — digital multimeter (recommended, most accurate)**
- DMM with ≥ 4½ digit resolution on DC volts (a basic Fluke 115,
  Brymen BM235, or even a cheap UT61E is adequate; 5½ or 6½ digit is
  better if available).
- DC accuracy ≤ 0.1% of reading in the 0–20 V range (~10 mV worst case
  at 10 V; well under a semitone at 1 V/oct, leaving headroom for the
  Tachyon itself to be the dominant error).
- Test leads with banana-to-3.5mm-TS adapter, or a bare 3.5 mm TS cable
  cut short with the conductors stripped.

**Path B — reference VCO + chromatic tuner**
- A known-good 1 V/oct VCO with flat tracking (e.g. Mutable Plaits,
  Doepfer A-110-1, Intellijel Dixie II).
- A chromatic tuner accurate to ≥ 1 cent (Peterson StroboStomp, a
  Korg OT-120, or a software tuner like TE Tuner / Cleartune).
- Patch cable between the sequencer output and the VCO 1 V/oct input,
  audio cable from VCO out to the tuner's input.

Path A calibrates the sequencer **as a voltage source** and is
independent of any downstream module — the resulting slope/offset are
universal. Path B calibrates the sequencer **as-patched into one
specific VCO** — the slope absorbs that VCO's particular input
impedance into the R_PROT divider, giving slightly better accuracy
when used with that VCO but slightly worse accuracy with others.

Recommended: use Path A for the permanent flash-stored calibration,
then spot-verify with Path B against the VCO you'll actually patch
into most often.

---

## 4. Procedure — Path A (DMM)

### 4.1 Setup

1. Power the sequencer from the Eurorack bus (±12 V and +5 V present,
   +3V3_PREC rail up, REF5025 and DAC8552 powered).
2. Connect the DMM across the CV-OUT-A jack: red probe on the tip
   (plug only partially inserted, or use a TS breakout), black probe
   on the sleeve (board GND).
3. **Leave the jack unloaded** — do not patch CV-OUT-A into any
   downstream module during this measurement. An unloaded output
   eliminates the R_PROT divider so the calibration captures only the
   DAC + op-amp + REF5025 errors. (Why: see §6 below.)
4. Enter calibration mode via the OLED menu: **Menu → Calibration →
   CV Out A**. Firmware disables any sequence playback and holds the
   selected DAC code steady.

### 4.2 Low-point measurement

1. Firmware sets DAC code = **6554** (nominal 1.0000 V — 10% of full
   scale; avoids using code 0, which would make any output offset
   invisible).
2. Wait 2 seconds for the reading to settle (op-amp slews in
   microseconds, but the DMM filter averaging takes ~1 second for a
   stable reading).
3. Record the DMM reading as `V_low_measured` — to 4 decimal places
   if possible.
4. Press the EC11E encoder switch to confirm; firmware stores
   `code_low = 6554` and `V_low_measured`.

### 4.3 High-point measurement

1. Firmware sets DAC code = **58982** (nominal 9.0000 V — 90% of full
   scale; stays well inside OPA1642's rail-to-rail output capability
   even at ±12 V supplies, see `hardware-design-plan.md` § Op-Amp Output
   Stage for the headroom calculation).
2. Wait 2 seconds for settling.
3. Record `V_high_measured`.
4. Press to confirm; firmware stores `code_high = 58982` and
   `V_high_measured`.

### 4.4 Slope and offset computation

Firmware computes:

```c
float slope  = (V_high_measured - V_low_measured) / (58982.0f - 6554.0f);
float offset = V_low_measured - slope * 6554.0f;
cv_cal[0].slope  = slope;
cv_cal[0].offset = offset;
```

Example with typical real-world measurements:

```
V_low_measured  = 0.9972 V
V_high_measured = 8.9854 V

slope  = (8.9854 − 0.9972) / (58982 − 6554)
       = 7.9882 / 52428
       = 152.363 µV / LSB

offset = 0.9972 − 152.363e−6 × 6554
       = 0.9972 − 0.9986
       = −0.00137 V (−1.37 mV)
```

Compare against the ideal: 152.588 µV/LSB and 0 V offset. The
measured slope is 0.15% low (consistent with the R_PROT divider not
being active because the jack is unloaded, and the feedback-resistor
ratio being slightly under 4) and the offset is within spec.

### 4.5 Save to flash

Firmware writes the new `cv_cal[0]` into the calibration flash page
and displays "CV A calibrated — slope 152.36 µV/LSB, offset −1.4 mV"
for user confirmation.

### 4.6 Repeat for CV-OUT-B

Same procedure with the DMM moved to the CV-OUT-B jack and the menu
advanced to **Menu → Calibration → CV Out B**. The two channels have
independent `slope` and `offset` values because the feedback
resistors R1/R3 and ground-leg R2/R4 have independent tolerances.

---

## 5. Procedure — Path B (reference VCO + tuner)

Use only to spot-verify or when a DMM isn't available.

1. Patch CV-OUT-A → VCO 1 V/oct input. Patch VCO audio out → tuner.
2. In the calibration menu, firmware sets code 6554 (nominal 1.0 V).
3. Note the VCO's current pitch on the tuner (e.g. "A2 +3 cents").
4. Firmware sets code 58982 (nominal 9.0 V).
5. Confirm the pitch is exactly 8 semitones × 12 = 96 semitones higher
   (e.g. "A10 +3 cents" — same cents offset).
6. If the cents offset differs between the two points, the slope is
   wrong. Compute the slope correction from the cents delta:

```
cents_error = cents_at_high - cents_at_low
slope_correction_factor = 1 - (cents_error / (8 × 1200))
new_slope = old_slope × slope_correction_factor
```

7. If the cents offset is the same at both points but non-zero,
   that's the octave offset — adjust `offset` by `(cents_at_low / 1200) × 1 V`.

Path B has no way to measure absolute voltage, only pitch ratios, so
it cannot calibrate the offset independently of the VCO's own offset.
This is why Path A (DMM) is the canonical procedure.

---

## 6. Why the jack is unloaded during Path A calibration

R_PROT (1 kΩ) forms a voltage divider with the downstream module's
input impedance. A typical Eurorack 1 V/oct input is 100 kΩ, giving a
−0.99% attenuation at the jack. Different VCOs have different input
impedances (30 kΩ to 1 MΩ is the range in the wild), so calibrating
into one VCO gives a slope that is 0.1–2% wrong for any other VCO.

Calibrating with the jack **unloaded** sidesteps this — the DMM's
input impedance is typically 10 MΩ (or 1 GΩ for an electrometer
DMM), so the divider attenuation is < 0.01% and effectively zero.
The stored slope then represents the op-amp output impedance, not
the op-amp + a particular load.

At runtime, every downstream module gets the same unloaded-calibration
slope, and the ~1% divider error at the jack is treated as part of
each downstream module's own tuning tolerance (which it is, since
different VCOs have different input impedances anyway). In practice
this residual error is well under a semitone and is absorbed into
the VCO's own front-panel tuning knob.

---

## 7. Verification

After calibration, firmware offers a **Menu → Calibration → Verify**
page that cycles through 5 test points and waits for the user to
record the DMM reading at each:

| Point | DAC code | Expected voltage | Measured | Error |
|---|---|---|---|---|
| 1 | 0 | 0.000 V | ___ | ___ |
| 2 | 16384 | 2.500 V | ___ | ___ |
| 3 | 32768 | 5.000 V | ___ | ___ |
| 4 | 49152 | 7.500 V | ___ | ___ |
| 5 | 65535 | 10.000 V | ___ | ___ |

Pass criteria:
- **Absolute error** at each point ≤ ±5 mV (half a cent at 1 V/oct).
- **Linearity error** (residual after subtracting the linear fit from
  point 1 to point 5) ≤ ±2 mV at any intermediate point.

If linearity is out of spec, the likely causes in order of probability:
1. DAC8552 has a bad INL segment (swap chips to confirm).
2. Op-amp is clipping on the high point (lower the high-point code
   from 65535 to 58982 and re-verify the top of range separately).
3. Feedback resistors have drifted or the PCB has a cold joint at R1
   or R3 — reflow U7's feedback network.
4. REF5025 is noisy or oscillating (add/check the NR cap on pin 5 and
   verify the 10 µF bulk on `+3V3_PREC`).

---

## 8. Flash storage format

Calibration constants live in the last 16 KB flash sector (sector 11
on the STM32F405RG) along with user presets. The calibration record
is a 32-byte struct at a fixed offset from the sector base:

```c
#define CAL_MAGIC 0x43564143  // 'CVCA'

typedef struct __attribute__((packed)) {
    uint32_t magic;        // CAL_MAGIC for valid record
    uint32_t version;      // 1
    float    slope_a;      // V per LSB, channel A
    float    offset_a;     // V, channel A
    float    slope_b;      // V per LSB, channel B
    float    offset_b;     // V, channel B
    uint32_t crc32;        // CRC-32 over bytes [0..27]
} cv_cal_flash_t;          // 32 bytes total
```

On boot, firmware reads the record, verifies magic and CRC, and
either loads the stored constants or falls back to the nominal
defaults (`slope = 10.0 / 65535`, `offset = 0`) if the record is
absent or corrupted. The user is notified via the OLED status bar if
default constants are in use.

Re-calibration rewrites only this 32-byte struct using the standard
erase-sector / program-word pattern. Since this is a 16 KB sector,
the other user data in the sector must be preserved via a
read-modify-write cycle with an RAM buffer.

---

## 9. When to re-calibrate

Calibration is stable and does not drift meaningfully under normal
conditions. Re-run only if:

- **First power-on after assembly** (no valid record in flash).
- **Ambient temperature has changed by > 15 °C** from the calibration
  temperature and pitch accuracy is audibly off. Typical Eurorack
  shows open to house temperature and a short soak; if you
  calibrated at 20 °C in winter and play at 35 °C under stage
  lights, recalibrate. Realistically this is ~5 cents of drift and
  most users won't notice.
- **A passive part in the precision chain has been replaced**
  (REF5025, DAC8552, OPA1642, R1–R4). This retunes the slope.
- **The module has sat unpowered for > 1 year** — op-amp Vos can age
  slightly. Verify first, re-cal only if out of spec.

Do not re-calibrate in response to a downstream VCO sounding flat —
that is almost always the VCO's own tuning knob, not the sequencer.

---

