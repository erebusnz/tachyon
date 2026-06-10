# Tachyon IO Bring-up Test Plan

Bench-test plan for exercising **every non-power-rail IO pin** on the
assembled Tachyon stack (WeAct STM32F405 core) using instrument MCP servers
driven from this session plus a purpose-built **test firmware** running on
the F405.

## Test boundary — BACKING BOARD ONLY

**Only the backing (MCU + audio) board is under test. The IO board and front
panel are NOT connected.** Every test therefore happens **at the H2 / H3 /
H4 / H9 header pins** on the backing board, and the expected value is the
**backing-board-side** signal — *not* the post-IO-board jack/control value.

This matters because several conditioning stages live on the **IO board**, so
their effect is absent at the header:

| Stage | Lives on | Consequence at the backing-board header |
|---|---|---|
| Gate 2N7002K invert + +5 V level-shift | IO board (`H9.11→R20→Q2`) | Gates leave the MCU as **raw 3.3 V GPIO, non-inverting** — 3.3 V at H9.11/H9.10 is CORRECT, not a fault |
| Pot (U25) | IO board | H9.4 goes ~straight to the MCU ADC — **inject** a voltage at the header to test |
| Encoder (SW1) | IO board | H4.2/4.3/4.5 go ~straight to the MCU — **inject** edges at the header |
| CV-in scaling op-amp (inverting map) | IO board | H9.5/H9.6 reach the MCU ADC through protection only — **inject** 0–3.3 V; ADC code is linear, the inverted transfer function is absent |

Stages that **are** on the backing board and so *are* live at the header:
- **CV-out**: DAC8552 → **OPA1642 (U7)** → H3.6 / H3.7 (full 0–10 V swing).
- **Audio**: PCM5102A → buffer **U16** → H2.2 / H2.3 (~10 Vpp).
- **VREF**: REF5025 (U2/U6) → **H9.2 ≈ 2.500 V DC**.
- **Gates** (raw GPIO), **CLK-IN** (protection → TIM2 capture).

Pin/stage assignments are authoritative in the per-board `.tel` netlists
(`gerber/Netlist_backing-board-schematic_*.tel` and the `io-board` one), not
in the design markdown.

The test firmware in this folder (`firmware-test/`) is a fork of the main
`firmware/` tree. It replaces the application with a **USB-CDC test
console**: a command interpreter that lets the host drive each output and
read back each input, so the instruments and the MCU meet in the middle on
every net.

## Instruments

| Role | Hardware | MCP server | Purpose |
|---|---|---|---|
| Stimulus / digital capture | **Bus Pirate 6** | [`mplogas/buspirate-mcp`](https://github.com/mplogas/buspirate-mcp) | PSU stimulus into analog inputs, logic-analyzer capture of digital buses, SPI/I2C sniff, GPIO toggling |
| Measurement | **Rigol DS1000Z scope** (DS1054Z / DS1104Z etc.) | [`erebusnz/rigol-mcp`](https://github.com/erebusnz/rigol-mcp) | DC + AC measurement at output jacks, waveform capture, edge/pulse timing, frequency |

Key capability split, because it shapes the whole plan:

- **rigol-mcp is scope-only** — it measures, it cannot source a signal.
  No function generator, no DMM, no PSU. Tools we lean on: `measure`
  (VMAX/VMIN/VPP/VRMS/FREQ/PERIOD/PWIDTH/RTIME), `get_waveform`,
  `set_channel`, `set_timebase`, `set_trigger`, `screenshot`, `send_raw`.
- **buspirate-mcp is the only programmable source** — its bench PSU
  (`set_voltage` / `set_power`, **0–5 V positive only**, confirmation-gated)
  is our analog stimulus, and its 75 MHz 8-ch **logic analyzer**
  (`la_prepare`/`la_command`/`la_analyze`/`la_identify`/`la_cleanup`) plus
  `open_spi`/`spi_*` capture and decode the digital buses. GPIO toggling is
  available for injecting edges.

Consequences baked into the procedures below:

- The Bus Pirate PSU **cannot make negative voltage**, and on the backing
  board the CV-in/pot pins go straight to a 3.3 V MCU ADC, so inputs are
  injected over **0 V → 3.3 V** at the header. The bipolar/inverted range is
  an IO-board op-amp and is out of scope here.
- Neither instrument is a clean clock generator, so **CLK-IN is verified by
  loopback** from a Tachyon gate output (see §6), with the BP logic analyzer
  as cross-check.

### Instrument strategy — Bus-Pirate-first (Strategy)

For the backing-board pass the **Bus Pirate is the primary instrument** and
the **Rigol is deferred**. Rationale: everything testable at the headers is
either digital, ≤3.3 V, or a ≤5 V DC level the BP can source/read directly.

- **BP reads a static 0–5 V DC level** via `verify_connection`'s
  `voltage_range_mv` (min≈max for a flat level; it samples ~20 Hz, enough for
  slow gate clocks too). The MCP exposes no generic ADC/GPIO read, so for
  finer pin-ADC reads use the SDK (`bench/bp_probe.py` →
  `status_request()['adc_mv']`).
- **BP sources** analog stimulus with `set_voltage`/`set_power` (0–5 V,
  approval-gated) and **injects edges** by GPIO toggling.
- **Never feed the BP >5 V or anything negative**: the 0–10 V CV-out top
  half, the ~10 Vpp audio, and the ±12 V rails exceed BP limits. Verify CV-out
  only over `dac _ mv 0..5000` (≤5 V at the header), verify audio via the
  **I2S3 logic-analyzer decode** (BCK/LRCK/DIN), and read ±12 V / full audio
  only with the **Rigol + 10× probe** if/when it is reattached.
- The console runs over the F405's own USB-CDC (Windows **COM13**), driven by
  `bench/serial_cli.py`. The BP terminal is **COM14**, its BPIO2 binary
  interface **COM9** (see [[instrument-mcp-servers]]).

### MCP setup checklist

1. Bus Pirate 6 on USB in **BPIO2 binary mode** (`binmode`→`2`→save `y` on
   the COM14 terminal). `list_devices` → `verify_connection`. (Windows
   detection fix + orphan-process gotcha: see [[instrument-mcp-servers]].)
2. Tachyon console enumerated on **COM13**; `serial_cli.py --port COM13
   --send id` returns the build string.
3. **Common ground first, always.** Tie BP **GND** to a Tachyon **GND** (a
   header GND pin — e.g. H9.1/3/7/9/12 — or a board GND test point) before
   any other connection. With the IO board absent there are no jack sleeves.
4. Rigol is **deferred** (BP-first). Only attach it for the >5 V analog
   amplitude checks; then set probes to **10×** and declare the ratio to
   rigol-mcp via `set_channel`.

## Test firmware (USB-CDC console)

The fork keeps all the HAL/peripheral init from `firmware/` (same `.ioc`,
same pin map — see `Core/Inc/main.h`) but swaps the application loop for a
line-based command parser over the USB CDC ACM port. Flash it the working
way: **USB DFU** (BOOT0 high, `dfu-util`), not SWD.

The console exposes one verb per IO function. Sketch of the command set:

| Command | Action | Pins exercised |
|---|---|---|
| `id` | print firmware id + build | — (USB PA11/PA12) |
| `led on/off/blink` | toggle user LED | **PB2** (heartbeat / proof of life) |
| `dac <ch> <code>` | write 16-bit DAC8552 code (raw) | SPI2 PB1/PB13/PB15 → CV-OUT |
| `dac <ch> mv <millivolts>` | write calibrated CV output | CV-OUT-A/B |
| `gate <a\|b> <0\|1>` | static gate level | PA3 / PA6 |
| `gate <a\|b> pulse <ms>` | one-shot timer pulse | PA3 / PA6 |
| `gate <a\|b> clk <bpm> <ppqn>` | free-run gate as clock source | PA3 / PA6 (drives loopback) |
| `adc <a\|b>` | read CV-IN ADC code + volts | PA0 / PA1 |
| `pot` | read USR_POT_1 ADC code | PC0 |
| `clk` | report captured CLK-IN period / BPM | PA2 (TIM2_CH3 capture) |
| `enc` | report count, turn direction, live A/B/SW level + last SW edge | PB6 / PB7 / PB4 |
| `enc reset` | zero the encoder count | PB6 / PB7 |
| `tone <hz>` | play sine over I2S3 | PB3/PB5/PA15 → audio L/R |
| `mute <0\|1>` | drive XSMT | PC6 |
| `oled test` | draw test pattern (also drives SPI1) | PA5/PA7/PB12/PC1/PC2 |
| `sd` | mount card, read/write/verify a test file | PC8–12, PD2; PA8 = CD |
| `stream adc on/off` | continuous ADC dump (for live stimulus sweeps) | PA0/PA1/PC0 |

The console is what makes the instruments useful: for **outputs** the host
commands a known state and the scope measures it; for **inputs** the host
(BP PSU / loopback / a human) applies a stimulus and the console reports
what the MCU saw.

## Test scope — backing-board headers H2 / H3 / H4 / H9

**The OLED (SPI1) is connected and working — it is out of scope.** With the
IO board absent, the goal is to confirm the backing board **correctly drives
or reads each signal at its own header pin**: outputs (gates, CV-out, audio,
VREF) appear at the header at their backing-side level; inputs (CV-in, pot,
encoder, clock) are exercised by **injecting a stimulus at the header pin**
and reading what the MCU saw via the console. The DAC SPI2, I2S3, MUTE,
SDIO, and USB nets are backing-board-internal and validated incidentally
(e.g. a tone proves I2S3 on the way to checking H2).

Each output test is a **drive-and-measure**: the console commands a known
state, the instrument measures the header pin. Each input test is an
**inject-and-read**: the instrument applies a stimulus at the header pin, the
console reports the MCU reading. "Backing pin dead" shows up as the MCU
driving/reading its GPIO but nothing appearing at (or no response to) the
header.

Pinouts below are from `gerber/Netlist_backing-board-schematic_2026-05-04.tel`
(GND pins omitted — tie them to the common ground). **Expect = backing-side
value at the header pin**, with the BP as primary instrument (see Strategy).

### H2 — audio out (4-pin, left edge)
| Pin | Net | Console | Measure at | Expect (backing side) |
|---|---|---|---|---|
| 1, 4 | GND | — | — | common ground |
| 2 | A-OUT-R | `mute 0` + `tone 1000` | H2.2 | ~1 kHz, ~10 Vpp (buffer U16 on backing) — **>BP range; use I2S LA decode or scope** |
| 3 | A-OUT-L | `mute 0` + `tone 1000` | H2.3 | ~1 kHz, ~10 Vpp (via R9/U16) — same |

### H3 — CV out + analog power (8-pin, right edge)
| Pin | Net | Console | Measure at | Expect (backing side) |
|---|---|---|---|---|
| 1 | +12V | — | H3.1 | rail present (power-supply check) — **>BP; scope/DMM** |
| 2 | −12V | — | H3.2 | rail present — **negative; never into BP** |
| 4 | +5V | — | H3.4 | +5 V rail (BP-safe to read) |
| 3,5,8 | GND | — | — | common ground |
| 6 | CV-OUT-A | `dac a mv 0..5000` | H3.6 | 0–5 V, tracks command (DAC8552→OPA1642 U7; full range 0–10 V, **swept to ≤5 V for BP**) |
| 7 | CV-OUT-B | `dac b mv 0..5000` | H3.7 | 0–5 V, tracks command (via R5/U7) |

### H4 — encoder (8-pin, bottom)
| Pin | Net | Console | Stimulus (IO board absent) | Expect |
|---|---|---|---|---|
| 2 | USR-ENC-A | `enc` | **inject** quadrature edges at H4.2 (BP) | count changes |
| 3 | USR-ENC-B | `enc` | **inject** at H4.3, lead/lag vs A | direction sign flips with phase |
| 5 | USR-ENC-SW | `enc` | **pull H4.5 to GND** (BP) | "SHORT/LONG press" prints |
| 7 | +3V3 | — | H4.7 | +3V3 rail present (BP-safe) |
| 1,4,6,8 | GND | — | — | common ground |

### H9 — pot / CV in / clock / gates / Vref (12-pin, bottom)
| Pin | Net | Console | Stimulus / measure | Expect (backing side) |
|---|---|---|---|---|
| 2 | VREF | — | read H9.2 (BP) | **2.500 V DC** (REF5025 U2/U6 on backing) |
| 4 | USR-POT-1 | `pot` | **inject** 0–3.3 V at H9.4 (BP PSU) | code ~0 → ~4095, linear (U25 pot is IO board) |
| 5 | CV-IN-A | `adc a` | **inject** 0–3.3 V at H9.5 (BP PSU) | code linear ≈ V/3.3×4095 (inverted map is IO-board op-amp, absent) |
| 6 | CV-IN-B | `adc b` | **inject** 0–3.3 V at H9.6 | same |
| 8 | CLK-IN | `clk` | gate→clk header loopback, or BP edges at H9.8 | measured period/BPM |
| 10 | GATE-OUT-B | `gate b 1/0/pulse` | read H9.10 (BP) | **0 / 3.3 V, non-inverting** (raw GPIO) |
| 11 | GATE-OUT-A | `gate a 1/0/pulse` | read H9.11 (BP) | **0 / 3.3 V, non-inverting** (raw GPIO) |
| 1,3,7,9,12 | GND | — | — | common ground |

**Header-to-header loopbacks (one patch cable, no external source):**
GATE-OUT-A (H9.11) → CLK-IN (H9.8) proves the gate-out drive and the clock
capture together (3.3 V edges are valid logic). CV-OUT-A (H3.6) → CV-IN-A
(H9.5) proves the DAC→buffer→ADC chain — **sweep `dac a mv 0..3300` only**,
so CV-IN stays ≤3.3 V (the input protection clamps above that).

## Pin inventory — non-power-rail IO

The table below is the full MCU-pin reference. The pins that **cross the
headers above** (CV in/out, gates, clock, encoder, pot, audio) are the test
focus; the rest (SPI2/I2S3/SDIO/USB/OLED/MUTE) are backing-board-internal
and already working or incidentally covered.

Power pins (VDD/VDDA/VBAT/VSS and the board power nets `+3V3`, `+3V3_PREC`,
`+3V3_AUDIO`, `+5V`, `±12V`) are **out of scope** — they are not GPIO and
are validated separately in `power-supply.md` bring-up. Everything below is
a signal pin and gets tested. The **Test method** column reflects the
backing-board pass (measure/inject at the header, BP-first).

| STM32 pin | Net / label | Function | Dir | Test method | Instrument |
|---|---|---|---|---|---|
| PA0 | CV_IN_A | ADC1_IN0 | in | inject 0–3.3 V at H9.5 → `adc a` | BP PSU + console |
| PA1 | CV_IN_B | ADC1_IN1 | in | inject 0–3.3 V at H9.6 → `adc b` | BP PSU + console |
| PC0 | USR_POT_1 | ADC1_IN10 | in | inject 0–3.3 V at H9.4 → `pot` | BP PSU + console |
| PA2 | CLK_IN | TIM2_CH3 capture | in | gate→clk header loopback → `clk` | loopback + BP LA |
| PA3 | GATE_OUT_A | TIM2_CH4 | out | `gate a …`, read **H9.11** (3.3 V GPIO) | BP |
| PA6 | GATE_OUT_B | TIM3_CH1 | out | `gate b …`, read **H9.10** (3.3 V GPIO) | BP |
| PB1 | DAC_SPI_CS | GPIO out (SYNC) | out | `dac …`; frame timing | BP LA |
| PB13 | DAC_SPI_SCLK | SPI2_SCK | out | `dac …`; decode | BP `spi_*` / LA |
| PB15 | DAC_SPI_MOSI | SPI2_MOSI | out | `dac …`; decode | BP `spi_*` / LA |
| H3.6 | CV-OUT-A | DAC8552 B→OPA1642 U7 | out | `dac a mv 0..5000`, read **H3.6** | BP (≤5 V); Rigol for full 0–10 V |
| H3.7 | CV-OUT-B | DAC8552 A→OPA1642 U7 | out | `dac b mv 0..5000`, read **H3.7** | BP (≤5 V); Rigol for full 0–10 V |
| PA5 | OLED_SPI_SCLK | SPI1_SCK | out | `oled test`; clock activity | BP LA / Rigol |
| PA7 | OLED_SPI_MOSI | SPI1_MOSI | out | `oled test`; decode | BP `spi_*` / LA |
| PB12 | OLED_SPI_CS | GPIO out | out | `oled test`; framing | BP LA |
| PC1 | OLED_RES | GPIO out | out | reset pulse on `oled test` | BP LA / Rigol |
| PC2 | OLED_DC | GPIO out | out | toggles cmd/data | BP LA |
| PB3 | I2S3_CK (BCK) | I2S3 | out | `tone`; clock present | BP LA / Rigol |
| PB5 | I2S3_SD (DIN) | I2S3 | out | `tone`; data toggling | BP LA |
| PA15 | I2S3_WS (LRCK) | I2S3 | out | `tone`; ~Fs/64 of BCK | BP LA / Rigol |
| H2.3/H2.2 | A-OUT-L/-R | PCM5102A→buffer U16 | out | `tone 1000`; I2S decode now, F+VPP later | BP LA; Rigol for amplitude |
| PC6 | MUTE_N (XSMT) | GPIO out | out | `mute`; PC6 level (audio collapse on Rigol) | BP LA / SDK + console |
| PB6 | USR_ENC_A | TIM4_CH1 | in | inject edges at H4.2 → `enc` | BP + console |
| PB7 | USR_ENC_B | TIM4_CH2 | in | inject edges at H4.3 → `enc` | BP + console |
| PB4 | USR_ENC_SW | EXTI4 | in | pull H4.5 to GND → `enc` SW edge | BP + console |
| PA8 | SD_CD | GPIO in | in | insert/remove → `sd` | console (manual) |
| PC8–PC12 | SDIO D0–D3/CK | SDIO | i/o | `sd` mount + r/w/verify | console; BP LA optional |
| PD2 | SDIO_CMD | SDIO | i/o | covered by `sd` | console |
| PA11 / PA12 | USB DM / DP | USB FS | i/o | console link itself = pass | host enumeration |

## Per-subsystem procedures

All measurements/injections are **at the backing-board header pin** (IO board
absent). BP is primary; "read level" = `verify_connection` on the BP IO pin
clipped to that header pin, taking `voltage_range_mv`.

### 1. CV outputs (DAC8552 → OPA1642 U7, on backing) — read at H3.6/H3.7
1. BP IO clipped to **H3.6** (CV-OUT-A). Common ground.
2. Console `dac a mv 0 / 1000 / 2500 / 5000`; at each step read the level.
   **Stop at 5000 mV** — the chain swings 0–10 V full-scale, so >5000 mV
   exceeds BP input. (For the full 0–10 V span + calibration, use the Rigol.)
3. Expect a monotonic line, ~1:1 mV→mV over 0–5 V. Repeat on **H3.7**
   (CV-OUT-B). Watch for the documented A/B channel swap (firmware already
   maps jack A→DAC ch B).

### 2. Gate outputs (raw 3.3 V GPIO at the header) — read at H9.11/H9.10
1. BP IO clipped to **H9.11** (GATE-OUT-A). Common ground.
2. `gate a 0` → read ~3.3 V; `gate a 1` → read ~0 V. This is **non-inverting
   3.3 V GPIO** — correct for the backing board. (The MOSFET invert/+5 V
   level-shift is on the IO board; the firmware's inversion only takes effect
   once the IO board is attached.)
3. `gate a clk 120 1` → read over ~2 s: level toggles ~2 Hz (BP ~20 Hz
   sampling resolves it). For precise pulse width/edges use `la_*` or Rigol.
   Repeat for B at **H9.10**.

### 3. SPI bus — DAC (SPI2), backing-internal
1. BP logic analyzer on SPI2 SCK/MOSI/CS (PB13/PB15/PB1);
   `la_prepare` → `dac a 0x8000` (console) → `la_analyze` / `la_identify`,
   or `open_spi`+`spi_*` to decode.
2. Verify clock toggling, CS framing (DAC8552 = 24-bit frame, SYNC low for
   exactly 24 SCLK, **mode 1** per `cv-output-dac.md`), and MOSI data.
   (SPI1/OLED is already validated and out of scope.)

### 4. Audio (I2S3 → PCM5102A → buffer U16, on backing)
1. **Digital decode (BP):** logic analyzer on PB3/PB5/PA15 — confirm BCK
   present, LRCK ≈ BCK/64, data on DIN while `mute 0` + `tone 1000`.
2. **Mute (BP LA / SDK):** `mute 1` → DIN/I2S activity continues but the
   PCM5102A XSMT (PC6) asserts; confirm the PC6 level changes. Analog
   silence/return is only visible on the **Rigol** (audio is ~10 Vpp at
   H2.2/H2.3 — **never into the BP**).
3. **Amplitude (Rigol, deferred):** scope H2.3 AC-coupled, `tone 1000`,
   `measure FREQ`≈1 kHz and `VPP`≈10 Vpp.

### 5. ADC inputs (CV-IN, pot) — inject at the header, read console
1. `stream on`.
2. **CV-IN-A:** BP `set_voltage` 0 → 3.3 V in steps, output clipped to
   **H9.5**; `adc a` code should rise ~linearly (`code ≈ V/3.3×4095`). The
   inverted 0 V⇒2063 map of `cv-input.md` is an **IO-board op-amp** and is
   absent here. Repeat at **H9.6** for CV-IN-B. **Never** feed >5 V or
   negative into the BP. Keep injection ≤3.3 V (input protection clamps).
3. **Pot:** inject 0 → 3.3 V at **H9.4**; `pot` should sweep ~0 → ~4095.

### 6. Loopback self-tests (header-to-header patch, no external source)
- **Gate → Clock:** patch **H9.11 (GATE-OUT-A) → H9.8 (CLK-IN)**.
  `gate a clk 120 1`, then `clk` should report ≈120 BPM via TIM2 capture —
  exercises PA3 (out) and PA2 (capture) together (3.3 V edges are valid
  logic). BP LA on the net confirms the edges.
- **CV-OUT → CV-IN:** patch **H3.6 (CV-OUT-A) → H9.5 (CV-IN-A)**. Sweep
  `dac a mv 0…3300` only (keeps CV-IN ≤3.3 V); `adc a` code should track the
  commanded voltage. Exercises DAC→OPA1642→ADC with just a patch cable.

### 7. Encoder, USB (and SD)
- **Encoder (inject at header):** the encoder (SW1) is on the IO board, so
  pulse **H4.2/H4.3** with quadrature edges from the BP and confirm `enc`
  count moves; lead/lag of A vs B sets the sign (`dir=CW/CCW`). `enc` also
  prints the live A/B/SW pin levels — handy for confirming the BP is actually
  toggling the right header pin before chasing a count that won't move.
  `enc reset` zeroes the count for a clean baseline between sweeps. Pull
  **H4.5** to GND for the SW press; `enc` reports it as `last_sw=SHORT/LONG`.
- **USB:** if the console enumerates on COM13 and round-trips commands,
  PA11/PA12 are proven by definition.
- **SD:** backing-board-internal (card slot on the backing board) — `sd`
  card-detect + block-0 read still applies if a slot/card is present;
  otherwise defer.

## Coverage / limitations
- **Backing-board scope:** every signal pin is checked **at its header**;
  IO-board-side behaviour (MOSFET 0/+5 V gates, pot/encoder hardware,
  inverted CV-in map, jack wiring) is explicitly **out of scope** until the
  IO board is attached.
- **BP input ceiling (≤5 V, no negative):** CV-out is verified only over
  0–5 V, audio only via I2S decode, ±12 V not at all. Full 0–10 V CV-out,
  audio amplitude, and the ±12 V rails need the **Rigol + 10× probe**.
- Inputs with no on-board source (CV-in, pot, encoder) are tested by
  **injecting** a stimulus at the header; the console reports the reading.

## Safety
- Common ground before anything else — to a header GND pin (e.g. H9.1/3/7/
  9/12) or a board GND test point (no jack sleeves with the IO board off).
- Bus Pirate sees only Tachyon's 0–5 V / 3.3 V logic and its own PSU
  output — **never the ±12 V rails, the 0–10 V CV-out top half, or the
  ~10 Vpp audio**. Keep CV-out reads ≤ `dac _ mv 5000` and injections ≤3.3 V;
  use the scope with a 10× probe for anything higher.
- Scope probes at 10×; declare the ratio to rigol-mcp (only when attached).
- BP PSU and `set_power` are confirmation-gated — approve each deliberately,
  and double-check the target pin before enabling output.
