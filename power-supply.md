# Power Supply -- Module Power Tree

This document describes the full power supply for the Tachyon
Eurorack module: how the +12 V bus rail is converted down to the +5 V,
`+3V3_AUDIO`, and `+3V3_PREC` rails consumed by the rest of the board.

The audio DAC sub-spec lives in `audio-output-dac.md` and references the
rails defined here.

---

## 1. Power tree overview

```
Eurorack +12V (P1) ──> D1 (SS14) ──> L1 (BLM21AG601SN1D ferrite) ──> C1 100uF || C3 100nF ──> +12V
                                                          │
                                                          ▼
                                       TPS54202DDCR buck (U12)
                                                          │
                                                          ▼
                                                        +5V
                                                          │
                              ┌───────────────────────────┼───────────────────────────┐
                              ▼                           ▼                           ▼
                        WeAct VIN                  TPS7A2033PDBVR            TPS7A2033PDBVR
                              │                          (U14)                     (U15)
                              ▼                           │                           │
                       onboard 3V3 LDO                    ▼                           ▼
                              │                       +3V3_PREC                  +3V3_AUDIO
                              ▼                           │                           │
                      +3V3 (digital) ────┐                ▼                           ▼
                                         │            DAC8552 VDD          PCM5102A AVDD
                                         │            REF5025 VIN          PCM5102A CPVDD
                                         ▼
                                STM32F405, OLED,
                                PCM5102A DVDD,
                                logic, 74AHCT1G125

                                       +12V ────────────────────────────────> OPA1642 V+ (U7 CV buffers, U16 audio buffers)

Eurorack -12V (P1) ──> D2 (SS14) ──> L2 (BLM21AG601SN1D ferrite) ──> C2 100uF || C4 100nF ──> -12V ──> OPA1642 V- (U7, U16)
```

Notes:
- **P1** is a 10-pin (2×5, 2.54 mm pitch) "power-only" Doepfer header
  (per `eurorackpower.png`) carrying only +12 V, GND, and -12 V — no
  +5 V and no CV/gate rows. +5 V is generated locally by the TPS54202
  buck from the +12 V rail.
- The WeAct STM32F405 core board has its own onboard 3V3 LDO; we feed
  it +5 V and let it produce the digital +3V3 rail used by the MCU,
  OLED, PCM5102A DVDD, and any logic.
- Analog precision (`+3V3_PREC`) and audio (`+3V3_AUDIO`) come from
  **two dedicated low-noise LDOs**, both fed from the local +5 V rail.

---

## 2. Current budget

| Load | Rail | Typ | Peak | Notes |
|---|---|---|---|---|
| STM32F405 @ 168 MHz | +3V3 (WeAct LDO) | 80 mA | 150 mA | Peak with USB enumeration / I/O activity |
| SSD1327 OLED 128x128 | +3V3 (WeAct LDO) | 15 mA | 30 mA | 4-bit grayscale; panel VCC from on-board boost |
| 74AHCT1G125 buffer | +3V3 (WeAct LDO) | 1 mA | 2 mA | |
| PCM5102A DVDD | +3V3 (WeAct LDO) | 3 mA | 5 mA | Internal LDO drops to 1.8 V |
| **Subtotal +3V3 (digital)** | | **~100 mA** | **~190 mA** | Comes out of WeAct onboard LDO |
| PCM5102A AVDD | +3V3_AUDIO | 17 mA | 20 mA | |
| PCM5102A CPVDD | +3V3_AUDIO | 4.5 mA | 6 mA | Charge pump |
| **Subtotal +3V3_AUDIO** | | **~22 mA** | **~26 mA** | TPS7A2033 #2 |
| DAC8552 | +3V3_PREC | 0.5 mA | 1 mA | 2 × 155 µA quiescent + SPI switching |
| REF5025 | +3V3_PREC | 1 mA | 1.5 mA | Precision Vref; 0.6 V over VS_min = 2.7 V (datasheet §6.5). Moved onto `+3V3_PREC` so U2 can sit next to U6 and keep the `VREF_2V5` trace < 20 mm |
| **Subtotal +3V3_PREC** | | **~1.5 mA** | **~2.5 mA** | TPS7A2033 #1 — still very lightly loaded (< 1 % of 300 mA capability) |
| TPS7A2033 ×2 quiescent | +5V | 0.05 mA | -- | Negligible |
| **Total +5V load** | | **~134 mA** | **~224 mA** | Buck must supply this (STM32 + OLED + PCM5102A + U14/U15 in) |
| OPA1642 (U7, dual)  | +12V | 3.6 mA | 4.6 mA | CV output buffers; 2 × 1.8 mA typ / 2.3 mA max quiescent (`OPA1642.md:48`) |
| OPA1642 (U16, dual) | +12V | 3.6 mA | 4.6 mA | Audio output buffers (PCM5102A → jacks, gain ×1.68) |
| **Total +12V load** | | **~143 mA** | **~235 mA** | Buck input current + both OPA1642 V+ quiescent; dominated by the buck |
| OPA1642 (U7, dual)  | -12V | 3.6 mA | 4.6 mA | CV output buffers, V- side |
| OPA1642 (U16, dual) | -12V | 3.6 mA | 4.6 mA | Audio output buffers, V- side |
| **Total -12V load** | | **~7 mA** | **~10 mA** | Output stages only — no other -12 V loads on the board |

The TPS54202's 2 A rating gives ~9× headroom over peak load -- the buck
runs cool, well below its thermal envelope. The ±12 V rails are very
lightly loaded (two OPA1642s only) so the Eurorack bus sees ~10 mA on
-12 V and ~143 mA on +12 V; well inside any Doepfer busboard budget.

---

## 3. Stage 1: +12V → +5V buck

### Part

**`TPS54202DDCR`** (Texas Instruments, genuine)
- LCSC: **C191884**
- Package: SOT-23-6
- Vin: 4.5 V to **28 V** (comfortable Eurorack hot-plug margin)
- Iout: 2 A
- fsw: 500 kHz internal
- Topology: **synchronous** buck — both high-side and low-side FETs integrated, no external Schottky catch diode required ("EMI friendly synchronous step-down converter" per the datasheet title)
- Reference design: TI's datasheet "12 V to 5 V" application schematic

If more current headroom is ever needed, **`TPS54302DDCR`** is a pin-/
footprint-compatible 3 A variant in the same family. The 2 A TPS54202
already gives us ~9× margin at peak load, so there is no reason to
switch unless supply availability forces it.

### Application schematic (TI WEBENCH validated, 12 V → 4.95 V)

The values below come from TI WEBENCH for this exact buck and load, with
the feedback divider re-tuned to hit **5.00 V** using standard E96
resistors. WEBENCH uses Vref = 0.600 V (nominal; the datasheet lists
0.596 V typ, min 0.584, max 0.608). Vout in sim calculates to
**0.600 V × (1 + 97.6 k / 13.3 k) = 5.003 V**.

Note: with R1 = 100 k the nearest E96 pair is 100 k / 13.7 k (4.96 V
in sim) or 100 k / 13.3 k (5.11 V); neither hits 5.00 V. Moving to
R1 = 97.6 k / R2 = 13.3 k lands within 3 mV of target -- the best
E96 pair available. Real-world Vout will shift by the Vref tolerance
(±2 %), which dominates over divider error regardless of the pair
chosen. The feed-forward zero shifts by ~2 % (R1 moves from 100 k to
97.6 k), which is negligible for loop response -- C7 stays at 68 pF.

**Validated in TI WEBENCH sim:** Vout avg 5.03 V (previous 90.9 k /
12.3 k divider -- being replaced by 97.6 k / 13.3 k), startup 4.61 ms.
WEBENCH reports **peak inductor current ~1.70 A during the startup
inrush transient** (19 % margin vs the DFE252012F-4R7M 2.1 A Isat).
Steady-state DCM peak is much lower -- see the L3 note below for
the ripple calculation.

The feedback divider designators are **R1 / R2** (was R7 / R8 in earlier
revs). R7 / R8 are now claimed by the PCM5102A audio EMI series
resistors -- see `audio-output-dac.md` §4a.

| Ref | Value | Package | MPN (WEBENCH) | Notes |
|---|---|---|---|---|
| C5 | 10 uF / 25 V ±10 % | 0805 X5R | Murata GRM21BR61E106KA73L (LCSC **C84416**) | Input cap per WEBENCH. Place ≤ 2 mm from VIN/GND |
| C6 | 100 nF / 25 V | 0805 | AVX 08053C104KAT2A | BOOT-SW cap; reuse BOM line 1 (0805 100 nF) |
| L3 | 4.7 uH ±20 % / Isat 2.1 A / Irms 1.5 A | 2520 metric (2.5×2.0 mm) shielded | Murata `DFE252012F-4R7M=P2` (LCSC **C668313**) | DCR 190 mΩ max (160 mΩ typ); metal-alloy shielded. Isat at ΔL/L = 30 %; Irms at ΔT = 40 °C |
| R1 | 97.6 k 1 % | 0805 | Vishay CRCW080597K6FKEA | Feedback divider top; sets Vout = 5.00 V with R2 |
| R2 | 13.3 k 1 % | 0805 | Vishay CRCW080513K3FKEA | Feedback divider bottom |
| C7 | 68 pF / 50 V ±1 % | 0805 NP0 | YAGEO CC0805FRNPO9BN680 (LCSC **C541517**) | Feed-forward across R1, improves loop response |
| C8 | 47 uF / 16 V | 1210 X5R | Murata GRM32ER61C476KE15L | Output bulk; ESR ~3 mΩ |

**Substitutions and assembly notes:**

- **C7 (feed-forward cap):** WEBENCH proposed an 0201 part. **Bumped to
  0805** for hand assembly / rework. Any C0G/NP0 68 pF 0805 25 V works --
  the exact MPN doesn't matter as long as the dielectric is C0G.
- **R1 / R2:** WEBENCH used 0402. Bumped to **0805** to match the
  rest of the board and to keep hand-soldering manageable. Keep the
  **exact resistance values** (97.6 k and 13.3 k, 1 %) -- do not round
  to standard E12 values; the divider math depends on these, and any
  substitution will move Vout off 5.00 V.
- **C5:** WEBENCH specifies a **10 µF** input cap at the buck
  VIN. The existing 10 µF 0805 in BOM line 2 is rated for the WeAct
  supply context only and its voltage rating is not guaranteed for
  +12 V plus hot-plug transients. Add a dedicated 10 µF / **25 V**
  X5R/X7R 0805 cap as a new BOM line -- do not reuse BOM line 2.
- **C8:** 47 uF 1210 is a chunky cap; this is what gives the
  buck its low output ripple. Don't substitute a smaller cap to save
  area -- if 1210 is too big, the alternative is 2× 22 uF 0805 in
  parallel (re-run WEBENCH to confirm stability).
- **L3:** chosen part is Murata `DFE252012F-4R7M=P2` (LCSC C668313).
  Higher DCR (190 mΩ max, 160 mΩ typ) than the WEBENCH reference NIC
  part (80 mΩ), but at our 225 mA peak load the I²R loss is only ~10 mW
  -- negligible. Ratings: **Isat 2.1 A** (ΔL/L = 30 %) and **Irms 1.5 A**
  (ΔT = 40 °C); both give ≥ 6× margin at our load. Inductor ripple is
  ~1.24 A pk-pk **typical** at nominal L; with the ±20 % tolerance band,
  worst-case-low L (3.76 µH) pushes ripple to ~1.55 A pk-pk, still
  comfortably under Isat. Because ripple (~1.2–1.5 A) greatly exceeds
  average output current (225 mA), peak inductor current at normal load
  sits around ~0.8–1.0 A -- still well under the 2.1 A Isat rating.
  At light load the TPS54202's Advanced Eco-mode transitions to
  pulse-skipping (PFM), which is the expected low-current behaviour for
  this synchronous part and keeps efficiency high. The metal-alloy
  shielding of the DFE series keeps radiated EMI low, which matters for
  the nearby audio DAC.

### Layout rules

Physical stackup, ground plane topology, and component placement zones
are defined in **`pcb-design.md`** -- read that document first. The
rules below are the power-supply-specific additions that sit on top of
the general PCB layout strategy.

- Keep the input loop (C5 → VIN → GND → C5) as small as
  possible -- this is the noisiest loop on the board. Target ≤ 5 mm²
  loop area on Layer 1, with the GND return via dropping straight to
  the Layer 2 plane next to the cap pad.
- Place the buck in **Zone A** (see `pcb-design.md` §5), in the corner
  furthest from U3 (PCM5102A) and U2 (REF5025). ≥ 30 mm separation
  target.
- Both TPS7A2033 LDOs sit on the Zone A / Zone C border so the +5 V
  feed stays in Zone A and only the regulated 3V3 rails enter Zone C.
- Route +5 V to the LDO inputs and the WeAct VIN as a Layer 3 pour
  region where possible; where a trace is needed, use ≥ 0.6 mm to
  minimize IR drop.
- The output ripple should be < 50 mV after C8; the downstream
  TPS7A2033 LDOs then contribute rail-noise rejection in the audio
  band (PSRR ~60 dB @ 1 kHz, dropping with frequency). The LDO does
  **not** rely on its PSRR at the buck's 500 kHz fundamental -- that
  ripple is handled by C8 and by the LDO's own output cap
  acting as an HF shunt.
- There is **no separate AGND net** and **no star-point tie** in this
  design. Layer 2 is one continuous ground plane; partitioning between
  analog and digital return currents is handled by component placement
  on Layer 1 (see `pcb-design.md` §3 and §5). Do not cut Layer 2.

---

## 4. Stage 2: +5V → +3V3_PREC and +5V → +3V3_AUDIO LDOs

Two **identical** TPS7A2033PDBVR low-noise LDOs hang off the +5 V rail.

| Designator | MPN | LCSC | Package | Vin | Vout | Iout | Noise |
|---|---|---|---|---|---|---|---|
| U14 | TPS7A2033PDBVR | C2862740 | SOT-23-5 | 4.5–6.5 V | 3.3 V fixed | 300 mA | 4 uVRMS |
| U15 | TPS7A2033PDBVR | C2862740 | SOT-23-5 | 4.5–6.5 V | 3.3 V fixed | 300 mA | 4 uVRMS |

**U14 → `+3V3_PREC`** feeds: **DAC8552 VDD and REF5025 VIN.** The
REF5025 was moved from `+5V` onto this LDO rail so U2 could be placed
directly adjacent to U6, keeping the `VREF_2V5` trace under the 20 mm
spec limit in `cv-output-dac.md` §6. `+3V3_PREC` at 3.3 V sits 0.6 V
above the REF5025's VS_min = 2.7 V hard floor (datasheet §6.5) — within
spec, and the TPS7A2033 is still < 1 % loaded (~2.5 mA peak against its
300 mA capability) so headroom for LDO transient response is ample.
The OPA1642 output buffers run from ±12 V, not `+3V3_PREC`. U14's
4 µVRMS noise floor directly sets the DAC8552's rail noise and the
REF5025's VIN noise; both parts then contribute their own line regulation
(≤ 3 ppm/V on the REF5025) so the shared rail is a non-issue for
precision.

**U15 → `+3V3_AUDIO`** feeds: PCM5102A AVDD (pin 18) and CPVDD (pin 12).

Per-LDO support components:

| Ref | Value | Package | LDO | Net | Notes |
|---|---|---|---|---|---|
| C9  | 1 uF  | 0805 X7R | U14 | VIN -- GND  | Close to pin 1 |
| C10 | 10 nF | 0805 X7R | U14 | VOUT -- GND | HF bypass on OUT |
| C11 | 1 uF  | 0805 X7R | U14 | VOUT -- GND | Close to pin 5; ceramic-stable |
| C12 | 1 uF  | 0805 X7R | U15 | VIN -- GND  | Close to pin 1 |
| C13 | 10 nF | 0805 X7R | U15 | VOUT -- GND | HF bypass on OUT |
| C14 | 1 uF  | 0805 X7R | U15 | VOUT -- GND | Close to pin 5; ceramic-stable |

The 5 V → 3.3 V drop dissipates only ~40 mW per LDO -- thermals are a
non-issue.

---

## 5. Stage 3: digital +3V3 (WeAct onboard LDO)

The WeAct STM32F405 core board ships with its own AMS1117-3.3 (or similar)
LDO that takes 5 V VIN and produces the digital 3.3 V used by the MCU,
USB peripherals, and on-board indicators. We feed this LDO from our local
+5 V rail and tap **+3V3** off the WeAct board's 3V3 pin to power:

- OLED (SSD1327 1.5")
- PCM5102A DVDD (pin 19)
- 74AHCT1G125 buffer (U4)
- Any other logic on the digital plane

We do **not** add a third external LDO for digital +3V3 -- the WeAct
onboard LDO is adequate for the digital domain (where switching noise from
the buck and AMS1117 ripple does not matter), and reusing it saves a part.

---

## 6. Input protection and EMI filter

Both ±12 V rails from the Doepfer header run through an identical
three-stage input network before reaching anything else on the board:

```
P1 ──> Dn (SS14) ──> Fn (PTC) ──> Ln (BLM21AG601SN1D) ──> Cbulk 100uF || Chf 100nF ──> rail
```

### 6.1 Reverse polarity / hot-plug protection (D1, D2)

The 10-pin Doepfer header is keyed but it is still standard practice to
protect against accidental reverse insertion.

**D1 (SS14) on +12 V** and **D2 (SS14) on -12 V** are series Schottkys
that drop ~0.4 V on each rail (acceptable: TPS54202 still has plenty of
headroom from 11.6 V; OPA1642 V- sees -11.6 V which is fine — OPA1642 is specified down to ±2.25 V).

No additional input TVS is required because the TPS54202's 28 V Vin max
is well above any plausible Eurorack transient.

### 6.2 Resettable fuses (F1, F2)

Downstream of each protection diode, each rail passes through a
**TECHFUSE SMD0805** polymeric PTC before reaching the EMI ferrite.
These catch downstream shorts (buck FET failure, tantalum short,
assembly errors) without relying on the Eurorack PSU's current limit,
and they self-reset once the fault is cleared.

| Ref | MPN | LCSC | Package | Vmax | I_hold | Rail | Notes |
|---|---|---|---|---|---|---|---|
| F1 | SMD0805-050-30V | C42924282 | 0805 | 30 V | 500 mA | +12 V | ~2× margin over 235 mA peak buck draw |
| F2 | SMD0805-020-30V | C6851465  | 0805 | 30 V | 200 mA | -12 V | 20× margin over 10 mA OPA1642 V- load; smallest in the family |

Placement: in series **after** D1/D2 and **before** L1/L2, so the PTC
protects the ferrite, bulk caps, and everything downstream. Series
resistance is negligible at our load (a few tens of mΩ cold).

PTCs are resettable but not instant — expect a few seconds to cool
down after a fault clears before the rail comes back. This is fine for
a module that's simply unplugged and re-plugged; during bring-up if
you're repeatedly shorting the rail you may want to power-cycle the
rack rather than waiting.

### 6.3 Input EMI ferrites (L1, L2)

Each rail passes through a **Murata BLM21AG601SN1D** chip ferrite bead
after the protection diode:

| Ref | MPN | Package | Impedance | DC R | Rated I | Notes |
|---|---|---|---|---|---|---|
| L1 | BLM21AG601SN1D | 0805 | 600 Ω @ 100 MHz | ~0.15 Ω | 2 A | +12 V feed, in series after D1 |
| L2 | BLM21AG601SN1D | 0805 | 600 Ω @ 100 MHz | ~0.15 Ω | 2 A | -12 V feed, in series after D2 |

Purpose: these form the series element of an LC low-pass against the
downstream bulk caps. They block HF conducted noise in **both
directions** -- keeping the Eurorack bus clean of the buck's 500 kHz
switching fundamental and its harmonics, and keeping external HF noise
from reaching the OPA1642 supply pins and the precision analog section.
Chosen over a wound inductor because ferrite beads lose their
impedance only resistively, so they cannot ring with the bulk caps.

DC loss: at peak +12 V draw (~135 mA through the buck plus ~5 mA for
the OPA1642 V+ quiescent, well under 250 mA total) the 0.15 Ω DCR drops
< 40 mV -- negligible.

### 6.4 Input bulk and HF bypass (C1–C4)

Downstream of each ferrite, each rail is decoupled by a parallel pair:

| Ref | Value | Package | Net | Notes |
|---|---|---|---|---|
| C1 | 100 µF | electrolytic or bulk ceramic | +12 V | Bulk energy reservoir, absorbs hot-plug inrush and supplies the buck input loop |
| C3 | 100 nF | 0805 X7R | +12 V | HF bypass, parallel to C1 |
| C2 | 100 µF | electrolytic or bulk ceramic | -12 V | Bulk reservoir for the OPA1642 V- stage |
| C4 | 100 nF | 0805 X7R | -12 V | HF bypass, parallel to C2 |

C1 and C2 are the true bulk reservoirs for the ±12 V rails and
complete the LC filter with L1/L2. C3 and C4 handle the high-frequency
bypass that the bulk caps' ESL cannot. Note that **C5 (10 µF /
25 V) is still required locally at the TPS54202 VIN pin** (§3) --
C1 is the bulk reservoir for the +12 V rail as a whole, not a
substitute for the buck's dedicated input cap.

---

## 7. BOM additions

Items already in the existing BOM are noted; items to add are flagged.

| Component | Designator (proposed) | MPN / LCSC | New? |
|---|---|---|---|
| TPS54202DDCR buck | U12 | TI / **C191884** | **Add** |
| TPS7A2033PDBVR LDO ×2 | U14, U15 | TI / **C2862740** | **Add (×2)** |
| 4.7 uH shielded inductor (2520 metric) | L3 | Murata DFE252012F-4R7M=P2 / **C668313** | **Add** |
| 47 uF 16 V 1210 X5R | C8 | Murata GRM32ER61C476KE15L -- LCSC TODO | **Add** |
| 10 uF 25 V 0805 X5R | C5 | Murata GRM21BR61E106KA73L / **C84416** | **Add** |
| 100 nF 0805 | C6, LDO in/out HF caps (if used) | C1711 (existing line 1) | Reuse |
| 1 uF 0805 X7R | C9, C11, C12, C14 | YAGEO CC0805KKX7R9BB105 / **C91185** | **Add** |
| 10 nF 0805 X7R | C10, C13 | Existing BOM 10 nF line | Reuse |
| 97.6 k 1 % 0805 | R1 | Vishay CRCW080597K6FKEA / **C2077571** | **Add** |
| 13.3 k 1 % 0805 | R2 | Vishay CRCW080513K3FKEA / **C4359485** (or **C4309412**) | **Add** |
| 68 pF NP0 50 V ±1 % 0805 | C7 | YAGEO CC0805FRNPO9BN680 / **C541517** | **Add** |
| Ferrite bead 600 Ω @ 100 MHz ×2 | L1, L2 | Murata BLM21AG601SN1D -- LCSC TODO | **Add (×2)** |
| PTC resettable fuse 500 mA 30 V 0805 | F1 | TECHFUSE SMD0805-050-30V / **C42924282** | **Add** |
| PTC resettable fuse 200 mA 30 V 0805 | F2 | TECHFUSE SMD0805-020-30V / **C6851465** | **Add** |
| 100 µF bulk cap ×2 | C1, C2 | ±12 V bulk -- MPN/LCSC TODO | **Add (×2)** |
| 100 nF 0805 ×2 | C3, C4 | C1711 (existing line 1) | Reuse |
| SS14 Schottky ×2 | D1, D2 | SS14 (existing BOM line) | Reuse |

---
