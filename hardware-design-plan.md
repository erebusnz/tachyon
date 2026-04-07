# Tachyon — Hardware Design Plan

### MCU Specifications (STM32F405RGT6)

[WeAct Studio STM32F405RGT6 64-Pin Core Board](https://github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard)

- **CPU:** Cortex-M4F @ 168 MHz with hardware single-precision FPU + DSP SIMD
- **RAM:** 192 KB SRAM
- **Flash:** 1 MB onboard
- **ADC:** 12-bit, 3 × ADC peripherals (up to 16 channels each), ±2 LSB INL — adequate for all pots and CV modulation inputs
- **DAC:** 2 × 12-bit onboard DAC
- **I2S:** 2–3 × hardware I2S peripherals
- **USB:** USB OTG Full Speed — USB MIDI class device, no driver needed on Mac/Windows/Linux
- **Used in:** Mutable Instruments Clouds, Rings, Braids, Elements; Rebel Technology OWL — the proven Eurorack standard

### What the WeAct Board Provides
- **8 MHz HSE crystal** on PH0/PH1 — satisfies the USB clock requirement; feeds PLL to 168 MHz SYSCLK
- **32.768 kHz LSE crystal** on PC14/PC15 — RTC timekeeping
- **USB-C connector** on PA11 (D−) / PA12 (D+) — USB MIDI ready out of the box
- **3.3V LDO regulator** — digital supply for MCU; reduce digital power noise by adding a separate analog LDO (see Power Supply section below)
- **1× Blue LED** on PB2 — available for debug/status use
- **1× User button (KEY)** on PC13 (pull-down, active-high) — available for mode switching
- **MicroSD slot** via SDIO on PC8–PC12, PD2 (card detect PA8) — available for patch/preset storage if needed
- **SWD debug header** on PA13/PA14 — leave accessible for firmware development

### Pin Conflicts and Reservations

The following pins are consumed by on-board hardware and must not be reassigned to sequencer I/O:

| Pin(s) | Reserved by | Notes |
|---|---|---|
| PH0, PH1 | 8 MHz HSE crystal | Must remain unloaded |
| PC14, PC15 | 32.768 kHz LSE crystal | Must remain unloaded |
| PA11, PA12 | USB D−/D+ | Keep 27 Ω series resistors populated |
| PA13, PA14 | SWD SWDIO/SWCLK | Leave accessible; can be reassigned post-debug if needed |
| PC10, PC12, PA15 | I2S3 → PCM5102A | `I2S3_CK` / `I2S3_SD` / `I2S3_WS` (see `audio-output-dac.md` §2) |
| PC6 | PCM5102A XSMT | Board-wide `~MUTE` (see `audio-output-dac.md` §2) |
| PB13, PB15 | SPI2 SCK / MOSI | Shared bus: SH1107 OLED + DAC8552 |
| PB2 | On-board blue LED | Reusable; share with status indication |
| PC13 | User KEY button | Reusable; suitable for mode toggle |

### Available GPIO for Sequencer Use

- **Port A:** PA0–PA7, PA9, PA10
- **Port B:** PB0–PB1, PB3–PB12, PB14 (PB2 = LED, shared; PB13/PB15 = SPI2)
- **Port C:** PC0–PC5, PC7, PC8, PC9, PC11, PC13 (button, shared)
- **ADC-capable pins (12-bit):** PA0–PA7 (ADC1/2/3 inputs), PC0–PC5 (ADC1/2 inputs), PB0–PB1 (ADC1/2 inputs)
- **DAC outputs:** PA4 (DAC1), PA5 (DAC2) — onboard 12-bit; supplemented by external 16-bit DAC for CV precision (see DAC section)
- **SPI buses:** SPI1 (PA5/PA6/PA7 + CS), SPI2 (PB13/PB14/PB15 + CS) — for external DAC
- **I2C buses:** I2C1 (PB6/PB7 or PB8/PB9), I2C2 (PB10/PB11)
- **Timers with PWM:** TIM1–TIM14; PWM available on most GPIO pins for RGB LED or gate timing

## RGB LED for Operating Mode Indication

**Problem:** The module has no visual feedback beyond the gate signals reaching downstream modules. With planned features (quantization on/off, scale selection, latch state, channel inversion mode), the operator has no way to read the current operating mode at a glance.

**Addition:** One common-cathode (or common-anode) RGB LED on the front panel, driven from 3 PWM-capable GPIO pins.

**Suggested colour mapping:**

| Colour | Mode |
|---|---|
| White / dim white | Default / unquantized LFSR output |
| Green | Quantization active, chromatic |
| Blue | Quantization active, diatonic scale |
| Cyan | Quantization active, pentatonic scale |
| Yellow | Lock latched (button pressed, pot frozen) |
| Red | Sequence fully locked (lock value at maximum) |
| Magenta | Channel 2 inversion active (switch) |

Colours can be mixed and brightness PWM-dimmed to indicate combined states (e.g. blue + yellow = quantized scale with latch active).

**Hardware requirements:**
- 3 PWM GPIO pins (one per channel R/G/B) with 3 current-limiting resistors (~100–330 Ω depending on LED spec).
- If GPIO PWM count is constrained, a single IS31FL3193 or PCA9685 I2C LED driver handles all three channels from one I2C bus.
- Panel footprint: standard 3mm or 5mm LED bezel, or a Lumex/ROHM SMD RGB in a daughterboard.
- Requires PWM support on the MCU — available on RP2040 (all GPIO support PWM) or SAMD21.

---

---

## STM32F405RGT6 Peripheral Stack

This section answers: given the WeAct STM32F405 core board, what external chips does the sequencer module still need?

### ADC — Built-in is sufficient

The STM32F405 has three independent 12-bit ADC peripherals with low INL (±2 LSB typical), running up to 1 MSPS each. F4 ADC is well-characterised with no known non-linearity pathologies near the rails. It is adequate for:

- All potentiometer readings (lock, steps, etc.)
- CV modulation inputs (lock CV per channel)
- Any non-pitch CV (steps CV, mod CV)

**No external ADC is needed** for this module's use case. The only scenario requiring an external ADC would be if a future revision adds a V/oct CV *input* for pitch tracking across 5+ octaves with sub-cent accuracy — at that point, a 16-bit ADC (e.g. ADS1115 via I2C) would be appropriate. For now, skip it.

The F405 can comfortably handle all existing CV inputs (CV_1, CV_2, LOCK, STEPS, SWITCH) and the new additions (reset trigger, extra lock CV) using built-in ADC channels.

### Crystal Oscillator — Already on WeAct board

The WeAct core board ships with an 8 MHz HSE crystal on PH0/PH1, which feeds the PLL to produce 168 MHz SYSCLK and the required 48 MHz USB clock (via PLLQ = 7). No crystal needs to be sourced or placed on the sequencer PCB.

---

### Power Supply — Partial (board provides digital 3.3V)

The WeAct board includes a 3.3V LDO that covers the MCU, USB, and DAC digital supply. The sequencer PCB still needs:

- **Analog 3.3V (separate):** Low-noise LDO with extra decoupling (e.g. MCP1700 or LT3042) for ADC VREF and DAC analog supply. Do not share the board's digital LDO — switching noise from the MCU couples into ADC readings.
- **Precision voltage reference:** For the DAC8552, an external 2.5 V precision reference (REF5025IDR, 3 ppm/°C) drives VREF at 2.500 V to set DAC full-scale. The board's onboard LDO is not stable enough to use as a DAC reference directly. See `cv-output-dac.md` for wiring.
- **Eurorack power input:** 10-pin (2×5, 2.54 mm pitch) "power-only" Doepfer header carrying ±12 V + GND only (no +5 V row, no CV/gate rows) — see `eurorackpower.png`. The WeAct board takes 5 V input; +5 V is generated locally from +12 V via the TPS54202 buck documented in `power-supply.md`.

---

### USB Connector — Already on WeAct board

The WeAct board has a USB-C connector on PA11/PA12 with the required series resistors. USB MIDI class device works without any additional USB hardware on the sequencer PCB. Route the USB-C port to the Eurorack front panel via a short flex cable or panel-mount USB-C extension.

---

### Non-Volatile Storage — Flash Pages Sufficient

The STM32F405 has 1 MB onboard flash. User settings (scale presets, calibration offsets, lock latch state) can be stored in the last few flash pages (each page = 16 KB on F405) without an external EEPROM. No external storage chip needed.

---

### Summary: External Chip BOM for WeAct STM32F405 Build

Items already provided by the WeAct core board are marked accordingly.

| Chip / Component | Purpose | Interface | Needed on sequencer PCB? |
|---|---|---|---|
| STM32F405RGT6 MCU | Main processor | — | No — on WeAct board |
| 8 MHz HSE crystal | USB + PLL clock | — | No — on WeAct board |
| 32.768 kHz LSE crystal | RTC | — | No — on WeAct board |
| USB-C connector | USB MIDI | — | No — on WeAct board (route to panel via extension) |
| Digital 3.3V LDO | MCU/USB power | — | No — on WeAct board |
| External ADC | Precision CV input | SPI/I2C | No — F405 built-in ADC is sufficient |
| DAC8552IDGKR (16-bit dual DAC) | CV outputs | SPI2 (shared with OLED) | **Yes** |
| OPA1642AIDR (dual JFET, RRO, LCSC C67640) | DAC output buffering + ×4 gain to 0–10 V | Analog | **Yes** |
| REF5025IDR (precision 2.5 V ref) | DAC VREF, powered from +3V3_PREC | — | **Yes** (for pitch accuracy) |
| Low-noise analog LDO | Separate analog 3.3V rail for ADC/DAC | — | **Yes** |
| PCM5102APWR | Audio I2S output (see `audio-output-dac.md`) | I2S3 | **Yes** |
| SH1107 OLED 1.5″ 128×128 | Menu / status display | SPI2 (shared) | **Yes** |
| Alps Alpine EC11E18244AU | Menu navigation, parameter adjust, confirm (rotary encoder w/ push) | 2× GPIO EXTI (A/B) + GPIO EXTI (switch) | **Yes** |

---

### Input Hardware

**Sole control — Alps Alpine EC11E18244AU (incremental rotary encoder with push switch)**
- 11 mm EC11E series, 24 detents/revolution, 24 pulses/revolution, integrated momentary push switch on the shaft
- Quadrature outputs A/B → two GPIOs with internal pull-ups; decode in firmware on pin-change interrupts (or TIMx encoder mode if an encoder-capable timer channel pair is free)
- Common pin (between A and B) → GND
- Push switch → GPIO with internal pull-up (active-low on press), interrupt-capable
- Use: rotate to navigate menu items / adjust the selected parameter value, press to confirm/enter and back out of sub-menus (short vs long press in firmware)
- Single-control UX: menu state machine distinguishes "navigation mode" (rotate moves highlight) from "edit mode" (rotate changes value); press toggles between them

**Debouncing:** The EC11E encoder contacts bounce (~5 ms typical). Handle the push switch in firmware (5–10 ms software debounce); the quadrature state-machine decode tolerates bounce on A/B. Add 10 nF caps to GND on A, B, and the switch pin for noise immunity in a Eurorack environment.

### Display Hardware

**Selected: 1.5″ SH1107 OLED, 128×128, SPI**
- Controller: SH1107 — column/page addressing; use an SH1107-specific driver (e.g. U8g2 `U8G2_SH1107_128X128` or Adafruit SH110X in SPI mode)
- SPI mode: 4-wire (SCK, MOSI, CS, DC) + optional RES; no MISO needed — display is write-only
- 3.3V supply, ~5–10 mA typical at normal contrast
- SPI throughput vs I2C: SPI at 8–20 MHz refreshes the full 128×128 framebuffer in ~0.8 ms; I2C at 400 kHz would take ~33 ms — eliminates any visible flicker on menu transitions
- 128×128 px fits 5–6 menu lines at 8px font or 3–4 lines at 16px, plus a status bar
- Module dimensions: ~34mm × 34mm PCB; visible area ~27mm × 27mm — suits a 12–14 HP panel
- **Bus sharing:** OLED shares SPI2 (PB13 SCK / PB15 MOSI) with the DAC8552 precision DAC using separate CS pins — both are write-only so there is no bus contention risk (DAC8552 has no MISO anyway)
- **Note:** SH1107 is not register-compatible with SSD1306; confirm the module's init sequence from the breakout's datasheet (Adafruit/Waveshare variants differ slightly)

### Pin Budget

| Signal | Pin | Peripheral |
|---|---|---|
| OLED SCK | PB13 | SPI2 SCK (shared with DAC8552) |
| OLED MOSI | PB15 | SPI2 MOSI (shared with DAC8552) |
| OLED CS | e.g. PB12 | GPIO (SPI2 NSS or soft CS) |
| OLED DC | e.g. PB4 | GPIO output |
| OLED RES | e.g. PB5 | GPIO output (optional, tie high if unused) |
| DAC8552 SCLK | PB13 | SPI2 SCK (shared with OLED) — schematic net `DAC-SPI-SCLK` |
| DAC8552 DIN | PB15 | SPI2 MOSI (shared with OLED) — schematic net `DAC-SPI-MOSI` |
| DAC8552 SYNC (CS) | e.g. PB1 | GPIO output, active-low — schematic net `DAC-SPI-CS` |
| PCM5102A BCK | PC10 | SPI3/I2S3_CK (AF6) — schematic net `I2S3_BCK` |
| PCM5102A DIN | PC12 | SPI3/I2S3_SD (AF6) — schematic net `I2S3_SD` |
| PCM5102A LRCK | PA15 | SPI3/I2S3_WS (AF6) — schematic net `I2S3_WS` |
| PCM5102A XSMT | PC6 | GPIO output, board-wide active-low `~MUTE` |
| EC11E encoder A | e.g. PC4 | GPIO EXTI, pull-up |
| EC11E encoder B | e.g. PC5 | GPIO EXTI, pull-up |
| EC11E push switch | e.g. PB3 | GPIO EXTI, pull-up |

All pins are available in the free GPIO pool. I2C1 is not yet allocated to any other peripheral. The DAC8552 and OLED share SPI2's clock/MOSI; each has its own CS GPIO so firmware serialises transactions.
