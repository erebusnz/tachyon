# User Interface — OLED Display, Rotary Encoder, Potentiometer

This document covers the three front-panel user-interface components
on the Tachyon board:

- **SSD1327 OLED** — 1.5" 128x128 4-bit grayscale SPI display for menu and status
- **Alps Alpine EC11E18244AU** — incremental rotary encoder with
  push switch for menu navigation and parameter editing
- **Alps 100K potentiometer** — general-purpose analog input read
  by the STM32 ADC, firmware-assignable to any parameter

Source references:
- `hardware-design-plan.md` — pin budget, peripheral stack, display
  and encoder selection rationale
- `datasheets/SSD1327.md` — OLED controller pinout, electrical specs,
  SPI timing, grayscale LUT, init sequence
- `datasheets/STM32F405RG.md` — SPI, GPIO, ADC peripheral details
- `power-supply.md` — rail definitions (+3V3, +5V, +12V)
- `cv-output-dac.md` — DAC8552 on SPI2 (separate bus from OLED)

---

## 1. OLED Display — SSD1327 128x128 4-bit Grayscale SPI

### 1.1 Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Controller | SSD1327 (Solomon Systech) | 128x128, 4-bit grayscale (16 levels per pixel); see `datasheets/SSD1327.md` |
| Resolution | 128 x 128 pixels | 5-6 menu lines at 8 px font, 3-4 at 16 px, plus status bar |
| Grayscale | 4 bits per pixel, 16 levels | Smooth UI rendering for waveforms, bar graphs, menu highlights |
| Module | Waveshare 1.5" OLED Module (~44.5 mm x 37 mm) | On-board VCC boost converter; only 3.3 V logic supply needed externally |
| Interface | 4-wire SPI (SCK, MOSI, CS, DC) + RES | Write-only; no MISO needed; BS1/BS2 jumpers set to 0,0 for SPI |
| SPI bus | SPI1 (dedicated — not shared with DAC8552) | Eliminates SPI mode conflict and bus contention with precision DAC |
| SPI mode | Mode 0 (CPOL = 0, CPHA = 0) | Data clocked in on rising edge, MSB first; max 10 MHz SCLK |
| Supply | 3.3 V from digital LDO (VDD); panel VCC from on-board boost | 15-30 mA typical at normal contrast |
| GDDRAM | 8192 bytes (128 x 128 x 4 bits, two pixels per byte) | Upper nibble = left pixel, lower nibble = right pixel |
| Driver | U8g2 `U8G2_SSD1327_WS_128X128` or Adafruit SSD1327 (SPI mode) | Not register-compatible with SSD1306 or SH1107 — use SSD1327-specific init |

### 1.2 Dedicated SPI1 bus

The OLED runs on its own SPI1 bus, separate from the DAC8552 on
SPI2. This avoids two problems that would arise from sharing:

1. **SPI mode conflict** — SSD1327 uses Mode 0 (CPHA=0), DAC8552
   uses Mode 1 (CPHA=1). Sharing would require reconfiguring CPHA
   on every bus switch.
2. **Bus contention during DMA** — the SSD1327's 8192-byte
   framebuffer takes ~6.6 ms to transfer at 10 MHz. On a shared
   bus, timing-critical DAC updates would be blocked for the
   duration of each display refresh, adding jitter to CV output.

With separate buses, the OLED DMA runs on SPI1/DMA2 while DAC8552
transactions run on SPI2/DMA1 — fully concurrent, no arbitration
needed. SPI1 sits on APB2 (84 MHz base clock), so a prescaler of
/8 gives 10.5 MHz — just above the SSD1327's 10 MHz max; use /16
(5.25 MHz) for margin, or /8 if the module tolerates it.

Full framebuffer transfer time:
- At 10 MHz: ~6.6 ms
- At 5.25 MHz: ~12.5 ms

Both are acceptable at a 10-30 Hz UI refresh rate.

### 1.3 Pin allocation

| Net name | Pin | Peripheral | Notes |
|---|---|---|---|
| `OLED-SPI-SCLK` | PA5 | SPI1_SCK (AF5) | Dedicated OLED bus; PA5 also DAC1 output — DAC1 not used |
| `OLED-SPI-MOSI` | PA7 | SPI1_MOSI (AF5) | Dedicated OLED bus; PA6 (MISO) not needed — display is write-only |
| `OLED-SPI-CS` | PB12 | GPIO output | Active-low chip select |
| `OLED-DC` | PC2 | GPIO output | Data/command select |
| `OLED-RES` | PC1 | GPIO output | Active-low reset; hold high in normal operation |

### 1.4 Wiring

```
                SPI1 bus (dedicated to OLED)
                ┌──────────────────────────┐
  PA5  ─────────┤ CLK                      │
  PA7  ─────────┤ DIN                      │
                │                          │
  PB12 ─────────┤ CS   SSD1327 OLED module  │
  PC2  ─────────┤ DC                       │
  PC1  ─────────┤ RES                      │
                │                          │
  +3V3 ─────────┤ VCC                      │
  GND  ─────────┤ GND                      │
                └──────────────────────────┘
```

### 1.5 Decoupling

The Waveshare 1.5" SSD1327 module has on-board VCC decoupling, so no
additional external bypass cap is fitted on the Tachyon PCB. If a future
board revision uses a bare SSD1327 panel without an integrated breakout,
add a 100 nF X7R 0805 between VCC and GND as close to the connector as
possible.

### 1.6 Firmware notes

- **Init sequence:** After hardware reset (RES low ≥3 µs, then high,
  wait ≥3 µs), send the SSD1327 configuration commands: display off
  (0xAE), remap (0xA0), start line, offset, multiplex ratio (0x7F),
  function select, phase length, clock divider, pre-charge, VCOMH,
  grayscale LUT, then display on (0xAF). See `datasheets/SSD1327.md`
  for the full command list
- **Framebuffer:** 8192 bytes — the F405's 192 KB SRAM easily
  accommodates a full framebuffer. Use DMA-driven SPI for the
  transfer to free the CPU during the ~6.6 ms write
- **Column addressing:** Two pixels per byte (upper/lower nibble).
  Column address range is 0x00-0x3F (64 addresses for 128 columns).
  Set column window (0x15, 0x00, 0x3F) and row window (0x75, 0x00,
  0x7F) before a full-screen data write
- **Grayscale LUT:** The default linear LUT (command 0xB9) is
  adequate for menus. A custom gamma curve (command 0xB8) can
  improve contrast for waveform rendering
- **DMA:** Use DMA2 for SPI1 OLED transfers (SPI1 is on DMA2).
  DAC8552 uses SPI2 on DMA1 — both run concurrently with no
  contention
- Refresh the display in the main loop or a low-priority timer;
  since the OLED is on its own bus, display updates cannot block
  or delay DAC8552 CV output

---

## 2. Rotary Encoder — Alps Alpine EC11E18244AU

### 2.1 Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Part | Alps Alpine EC11E18244AU | 11 mm EC11E series, panel mount, widely available |
| Detents | 24 per revolution | Tactile feedback; 1 detent = 1 menu step or 1 parameter increment |
| Pulses | 24 per revolution | 1:1 detent-to-pulse ratio; each detent produces one full quadrature cycle |
| Push switch | Integrated momentary, normally open | Short press = confirm/enter; long press = back/exit (firmware) |
| Quadrature outputs | A and B (common pin to GND) | Decode via pin-change EXTI interrupts or TIMx encoder mode |
| Debounce | 10 nF caps on A, B, and SW to GND; push switch additionally debounced in firmware (5-10 ms) | Quadrature state machine tolerates bounce on A/B; caps provide RF immunity in Eurorack environment |

### 2.2 Pin allocation

| Net name | Pin | Peripheral / AF | Notes |
|---|---|---|---|
| `USR-ENC-A` | PB6 | TIM4_CH1 (AF2) | Hardware encoder mode, internal pull-up enabled |
| `USR-ENC-B` | PB7 | TIM4_CH2 (AF2) | Hardware encoder mode, internal pull-up enabled |
| `USR-ENC-SW` | PB4 | GPIO EXTI, internal pull-up enabled | Active-low on press |

The encoder common pin connects to GND.

**Why PB6/PB7 (TIM4) instead of PC4/PC5 (EXTI):** PC4 and PC5 are
ADC-capable (ADC1_IN14/IN15) but do not map to any timer's CH1/CH2
encoder-mode inputs. PB6/PB7 map to TIM4_CH1/CH2 (AF2), enabling
zero-overhead hardware quadrature decoding. PB6/PB7 are the default
I2C1 pins, but I2C1 is not allocated and remaps to PB8/PB9 (both
free) if needed later.

### 2.3 Wiring

```
                  EC11E18244AU (USR-ENC)
                ┌──────────────┐
  PB6 ──┬───────┤ A            │
        │       │              │
       C44      │   Common ────┤── GND
       10nF     │              │
        │       │              │
       GND      │              │
                │              │
  PB7 ──┬───────┤ B            │
        │       │              │
       C45      │              │
       10nF     │              │
        │       │              │
       GND      │              │
                │              │
  PB4 ──┬───────┤ SW (NO, SW1) │
        │       │              │
       C43      │   SW Common ─┤── GND
       10nF     │              │
        │       │              │
       GND      └──────────────┘
```

Internal pull-ups on PB6, PB7, PB4 pull idle state high. Encoder
contacts pull to GND through the common pin. The 10 nF caps filter
contact bounce and RF noise; they form an RC with the internal
pull-up (~40 kOhm on F405) giving tau ~0.4 ms — fast enough for
the quadrature edge rate at hand-turning speeds.

### 2.4 Quadrature decoding — TIM4 hardware encoder mode

The STM32F405 timers TIM1-TIM5 and TIM8 support a dedicated
hardware encoder interface mode. With PB6/PB7 on TIM4_CH1/CH2,
the timer's 16-bit counter auto-increments or auto-decrements on
each quadrature edge with no CPU intervention.

**Configuration:**
- TIM4 in encoder mode `TIM_EncoderMode_TI12` (count on both
  edges of both channels = x4 resolution: 96 counts/revolution
  from 24 PPR)
- Input capture polarity: rising on both channels
- Input filter: 4-8 timer clock cycles (`ICFilter = 0x04`) —
  hardware-level bounce rejection, complementing the external
  10 nF caps
- Auto-reload: 0xFFFF (full 16-bit range); wrap is handled in
  firmware by reading TIM4->CNT as a signed delta
- No interrupt needed for counting — firmware reads TIM4->CNT
  on each UI update pass (typically 1-10 ms)

**Push switch** is on PB4 as a GPIO EXTI interrupt (falling
edge, 5-10 ms software debounce). The switch is a low-frequency
event that does not benefit from timer hardware.

### 2.5 UX state machine

Single-control navigation using rotate + press:

| State | Rotate action | Press action |
|---|---|---|
| Navigation mode | Move highlight up/down through menu items | Enter selected item (switch to edit mode or open sub-menu) |
| Edit mode | Increment/decrement the selected parameter value | Confirm and return to navigation mode |
| Any | — | Long press (>500 ms): back / exit sub-menu |

Firmware distinguishes short press (<500 ms) from long press
(>=500 ms) using a timer started on the falling edge of PB4.

---

## 3. Potentiometer — Alps 100K Linear

### 3.1 Configuration summary

| Decision | Value | Rationale |
|---|---|---|
| Type | 100 kOhm linear taper (B100K) | Linear taper for predictable firmware mapping. 100K (vs 10K) reduces idle current draw across the divider by 10×; the wiper bypass cap C46 (§3.3) makes the higher source impedance invisible to the ADC. |
| Part | Alps Alpine RK09K1130C0L (or equivalent 9 mm vertical 100 kΩ linear PCB-mount) | 9 mm body fits Eurorack panel spacing; vertical mount for front-panel shaft |
| Designator | U25 | |
| Purpose | General-purpose analog parameter knob | Firmware maps the ADC reading to whichever parameter is currently selected or assigned |
| ADC pin | PC0 | ADC1_IN10 — from the reserve ADC pool (PC0-PC3, PB0) |
| Wiring | Wiper to ADC, CW to +3V3, CCW to GND | Full rotation maps 0-3.3 V across the 12-bit ADC range |

### 3.2 Pin allocation

| Net name | Pin | Peripheral | Notes |
|---|---|---|---|
| `USR-POT-1` | PC0 | ADC1_IN10 | Reserve ADC pool; leaves PC1-PC3 and PB0 for future expansion |

### 3.3 Wiring

```
            Alps 100K linear pot (U25)
           ┌───────────────────┐
  +3V3 ────┤ CW (pin 3)       │
           │                   │
           │    Wiper (pin 2) ─┤──┬──── PC0 (ADC1_IN10)
           │                   │  │
  GND  ────┤ CCW (pin 1)      │  C46 100nF
           └───────────────────┘  │
                                  GND
```

C46 (100 nF) on the wiper acts as a charge reservoir for the ADC's
sample-and-hold so the pot's high source impedance (~25 kΩ at midpoint
for a 100 kΩ pot) does not degrade ADC accuracy. RC corner is
1/(2π × 25 kΩ × 100 nF) ≈ 64 Hz, well below the ADC scan rate but with
12.5 ms (5τ) settling, which is imperceptible on a hand-turned knob.
The cap also rejects RF and switching noise from the Eurorack power bus.

### 3.4 ADC configuration

| Setting | Value | Rationale |
|---|---|---|
| ADC peripheral | ADC1 | Same peripheral as CV inputs; add PC0 to the scan group |
| Resolution | 12-bit | 0.8 mV LSB — more than adequate for a panel knob |
| Sample time | 56 cycles | C46 (100 nF wiper-to-GND) acts as the charge source for the ADC S&H, so the effective source impedance during sampling is C46's series resistance (a few mΩ), not the pot's 25 kΩ; 56 cycles is comfortable |
| Scan order | After CV inputs (PA0, PA1) | Lower priority; pots change slowly relative to CV |
| Firmware filtering | 8x or 16x moving average | Eliminates wiper noise and provides smooth parameter response; ~1 ms latency at 1 kHz scan rate is imperceptible |

### 3.5 Firmware notes

- The pot reading is a raw 0-4095 ADC value. Firmware maps this to
  the active parameter's range (e.g., 0-127 for MIDI-style, 0-11
  for scale selection, etc.)
- Implement a dead-zone or hysteresis of ~2-4 LSB to prevent the
  displayed value from jittering when the knob is stationary
- The pot is firmware-assignable: which parameter it controls depends
  on the current menu state or a dedicated pot-assignment mode

---

## 4. Pin budget update

New pins consumed by user-interface hardware:

| Net name | Pin | Peripheral |
|---|---|---|
| `OLED-SPI-SCLK` | PA5 | SPI1_SCK (AF5) |
| `OLED-SPI-MOSI` | PA7 | SPI1_MOSI (AF5) |
| `OLED-SPI-CS` | PB12 | GPIO output |
| `OLED-DC` | PC2 | GPIO output |
| `OLED-RES` | PC1 | GPIO output |
| `USR-ENC-A` | PB6 | TIM4_CH1 (AF2) |
| `USR-ENC-B` | PB7 | TIM4_CH2 (AF2) |
| `USR-ENC-SW` | PB4 | GPIO EXTI |
| `USR-POT-1` | PC0 | ADC1_IN10 |

The OLED uses its own SPI1 bus on PA5/PA7 — no pins are shared
with the DAC8552 (SPI2, PB13/PB15). OLED-DC and OLED-RES moved
to PC1/PC2 to free PB4/PB5 for the encoder switch and I2S3_SD.
PA6 (SPI1_MISO) is not needed since the display is write-only;
PA6 remains allocated to Gate B (TIM3_CH1). PB6/PB7 provide TIM4 hardware encoder mode (CH1/CH2), same as
the original assignment. I2C1 remaps to PB8/PB9 if needed.

**Remaining reserve ADC pool** after pot allocation: PC1, PC2, PC3,
PC4, PC5, PB0 — six ADC-capable pins for future expansion
(additional pots, CV inputs, or sensor reads). PC4/PC5 were freed
by moving the encoder from GPIO EXTI to TIM4 hardware encoder mode
on PB6/PB7.

---

## 5. BOM additions

| Component | Designator | MPN / LCSC | New? |
|---|---|---|---|
| SSD1327 1.5" 128x128 OLED module (SPI, 4-bit grayscale) | OLED1 | Waveshare 1.5inch OLED Module (SSD1327) | **Add** |
| Alps Alpine EC11E18244AU rotary encoder | USR-ENC (+ SW1) | EC11E18244AU / LCSC TODO | **Add** |
| Alps Alpine 100K linear pot (or equiv. 9 mm vertical) | U25 | RK09K1130C0L / LCSC TODO | **Add** |
| 10 nF X7R 0805 x3 | C44 (A), C45 (B), C43 (SW) | Existing BOM 10 nF line | Reuse |
| 100 nF X7R 0805 x1 | C46 (pot wiper) | Existing BOM 100 nF line | Reuse |
| Knob for 6 mm D-shaft (encoder) | — | Davies 1900H or similar | **Add** |
| Knob for pot shaft | — | Match encoder knob style | **Add** |
