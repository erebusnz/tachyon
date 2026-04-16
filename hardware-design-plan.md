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
- **MicroSD slot** via SDIO — available for patch/preset storage if needed

#### MicroSD SDIO Pin Allocation

| MicroSD pin | Signal | STM32 pin | Peripheral / AF |
|---|---|---|---|
| 1 (DAT2) | `SDIO_D2` | PC10 | SDIO_D2 (AF12) |
| 2 (CD/DAT3) | `SDIO_D3` | PC11 | SDIO_D3 (AF12) |
| 3 (CMD) | `SDIO_CMD` | PD2 | SDIO_CMD (AF12) |
| 4 (VDD) | `+3V3` | — | Decoupled with 100 nF close to slot |
| 5 (CLK) | `SDIO_CK` | PC12 | SDIO_CK (AF12) |
| 6 (VSS) | GND | — | — |
| 7 (DAT0) | `SDIO_D0` | PC8 | SDIO_D0 (AF12) |
| 8 (DAT1) | `SDIO_D1` | PC9 | SDIO_D1 (AF12) |

Card detect: insert switch pulls PA8 low when card is present; R12 10 kΩ pull-up to VDD33 holds PA8 high when empty. Firmware reads PA8 as GPIO input (active-low = card inserted).

On-board R11 100 kΩ pull-up on CD/DAT3 (PC11) keeps DAT3 high during card insertion to prevent spurious SPI-mode entry
- **SWD debug header** on PA13/PA14 — leave accessible for firmware development

### Pin Conflicts and Reservations

The following pins are consumed by on-board hardware and must not be reassigned to sequencer I/O:

| Pin(s) | Net name | Peripheral / AF | Notes |
|---|---|---|---|
| PH0, PH1 | — | HSE crystal | WeAct board; must remain unloaded |
| PC14, PC15 | — | LSE crystal | WeAct board; must remain unloaded |
| PA11, PA12 | — | USB OTG FS D−/D+ | WeAct board; keep 27 Ω series resistors populated |
| PA13, PA14 | — | SWD SWDIO/SWCLK | WeAct board; leave accessible for debug |
| PA0 | `CV-IN-A` | ADC1_IN0 | CV modulation input A (see `cv-input.md`) |
| PA1 | `CV-IN-B` | ADC1_IN1 | CV modulation input B (see `cv-input.md`) |
| PA2 | `CLK-IN` | TIM2_CH3 | Clock input capture (see `clock-input.md`) |
| PA3 | `GATE-OUT-A` | TIM2_CH4 | Gate A output compare (see `gate-output.md`) |
| PA8 | `SD_CD` | GPIO input | MicroSD card detect, active-low with 10 kΩ pull-up (WeAct board) |
| PA5 | `OLED-SPI-SCLK` | SPI1_SCK (AF5) | OLED dedicated bus (see `user-interface.md` §1) |
| PA6 | `GATE-OUT-B` | TIM3_CH1 | Gate B output compare (see `gate-output.md`) |
| PA7 | `OLED-SPI-MOSI` | SPI1_MOSI (AF5) | OLED dedicated bus (see `user-interface.md` §1) |
| PA15 | `I2S3_WS` | I2S3_WS (AF6) | PCM5102A word select (see `audio-output-dac.md`) |
| PC1 | `OLED-RES` | GPIO output | OLED reset, active-low (see `user-interface.md` §1) |
| PC2 | `OLED-DC` | GPIO output | OLED data/command select (see `user-interface.md` §1) |
| PB1 | `DAC-SPI-CS` | GPIO output | DAC8552 SYNC, active-low (see `cv-output-dac.md`) |
| PB3 | `I2S3_BCK` | I2S3_CK (AF6) | PCM5102A bit clock (see `audio-output-dac.md`) |
| PB4 | `USR-ENC-SW` | GPIO EXTI | Encoder push switch (see `user-interface.md` §2) |
| PB5 | `I2S3_SD` | I2S3_SD (AF6) | PCM5102A serial data (see `audio-output-dac.md`) |
| PB6 | `USR-ENC-A` | TIM4_CH1 (AF2) | Encoder quadrature A (see `user-interface.md` §2) |
| PB7 | `USR-ENC-B` | TIM4_CH2 (AF2) | Encoder quadrature B (see `user-interface.md` §2) |
| PB12 | `OLED-SPI-CS` | GPIO output | OLED chip select, active-low (see `user-interface.md` §1) |
| PB13 | `DAC-SPI-SCLK` | SPI2_SCK (AF5) | DAC8552 clock (see `cv-output-dac.md`) |
| PB15 | `DAC-SPI-MOSI` | SPI2_MOSI (AF5) | DAC8552 data (see `cv-output-dac.md`) |
| PC0 | `USR-POT-1` | ADC1_IN10 | 10K pot wiper (see `user-interface.md` §3) |
| PC6 | `~MUTE` | GPIO output | PCM5102A ~XSMT (see `audio-output-dac.md`) |
| PC8 | `SDIO_D0` | SDIO_D0 (AF12) | MicroSD data 0 (WeAct board) |
| PC9 | `SDIO_D1` | SDIO_D1 (AF12) | MicroSD data 1 (WeAct board) |
| PC10 | `SDIO_D2` | SDIO_D2 (AF12) | MicroSD data 2 (WeAct board) |
| PC11 | `SDIO_D3` | SDIO_D3 (AF12) | MicroSD data 3 (WeAct board) |
| PC12 | `SDIO_CK` | SDIO_CK (AF12) | MicroSD clock (WeAct board) |
| PD2 | `SDIO_CMD` | SDIO_CMD (AF12) | MicroSD command (WeAct board) |

### Available GPIO for Sequencer Use

- **Port A:** PA4, PA9, PA10 (PA0/PA1 = CV in, PA2 = clock in, PA3 = gate A, PA5/PA7 = SPI1 OLED, PA6 = gate B, PA8 = SD card detect)
- **Port B:** PB0, PB2, PB8–PB11, PB14 (PB1 = `DAC-SPI-CS`, PB2 = LED shared, PB3 = `I2S3_BCK`, PB4 = `USR-ENC-SW`, PB5 = `I2S3_SD`, PB6/PB7 = `USR-ENC-A`/`USR-ENC-B`, PB12 = `OLED-SPI-CS`, PB13/PB15 = SPI2)
- **Port C:** PC3–PC5, PC7, PC13 (button, shared) (PC0 = `USR-POT-1`, PC1 = `OLED-RES`, PC2 = `OLED-DC`, PC8–PC12/PD2 = SDIO)
- **ADC-capable pins (12-bit):** PA0–PA7 (ADC1/2/3 inputs), PC0–PC5 (ADC1/2 inputs), PB0–PB1 (ADC1/2 inputs)
- **Reserve ADC pool:** PC3–PC5, PB0 — four ADC-capable pins for future pots, CV inputs, or sensors (PC1/PC2 consumed by OLED)
- **DAC outputs:** PA4 (DAC1) — onboard 12-bit; PA5 (DAC2) consumed by SPI1 OLED SCK. Supplemented by external 16-bit DAC for CV precision (see DAC section)
- **SPI buses:** SPI1 (PA5 SCK / PA7 MOSI) — OLED; SPI2 (PB13/PB15 + CS) — DAC8552
- **I2C buses:** I2C1 (PB8/PB9 — default PB6/PB7 consumed by TIM4 encoder), I2C2 (PB10/PB11)
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
| DAC8552IDGKR (16-bit dual DAC) | CV outputs | SPI2 (dedicated) | **Yes** |
| OPA1642AIDR (dual JFET, RRO, LCSC C67640) | DAC output buffering + ×4 gain to 0–10 V | Analog | **Yes** |
| REF5025IDR (precision 2.5 V ref) | DAC VREF, powered from +5V | — | **Yes** (for pitch accuracy) |
| Low-noise analog LDO | Separate analog 3.3V rail for ADC/DAC | — | **Yes** |
| PCM5102APWR | Audio I2S output (see `audio-output-dac.md`) | I2S3 | **Yes** |
| SSD1327 OLED 1.5″ 128×128 4-bit grayscale | Menu / status display | SPI1 (dedicated) | **Yes** |
| Alps Alpine EC11E18244AU | Menu navigation, parameter adjust, confirm (rotary encoder w/ push) | TIM4 encoder mode (PB6/PB7) + GPIO EXTI (PB4 switch) | **Yes** |
| Alps Alpine 10K linear pot (RK09K or equiv.) | General-purpose parameter knob, firmware-assignable | ADC1_IN10 (PC0) | **Yes** |

---

### Input Hardware

**Alps Alpine EC11E18244AU — incremental rotary encoder with push switch**
- 11 mm EC11E series, 24 detents/revolution, 24 pulses/revolution, integrated momentary push switch on the shaft
- Quadrature outputs A/B → PB6/PB7 (TIM4_CH1/CH2, AF2); decoded in hardware via TIM4 encoder mode — zero CPU overhead, counter auto-increments in hardware
- Common pin (between A and B) → GND
- Push switch → PB4, GPIO with internal pull-up (active-low on press), EXTI interrupt
- Use: rotate to navigate menu items / adjust the selected parameter value, press to confirm/enter and back out of sub-menus (short vs long press in firmware)
- Single-control UX: menu state machine distinguishes "navigation mode" (rotate moves highlight) from "edit mode" (rotate changes value); press toggles between them

**Debouncing:** The EC11E encoder contacts bounce (~5 ms typical). Handle the push switch in firmware (5–10 ms software debounce). TIM4 encoder mode's input filter (ICFilter) provides hardware-level bounce rejection on A/B; the external 10 nF caps to GND on A, B, and the switch pin add RF immunity in a Eurorack environment.

**Alps Alpine 10K linear potentiometer (RK09K1130A5R or equivalent 9 mm vertical PCB-mount)**
- Wiper → PC0 (ADC1_IN10), CW terminal → +3V3, CCW terminal → GND
- 100 nF cap on wiper to GND for noise filtering
- Firmware-assignable: the ADC reading maps to whichever parameter is currently selected or assigned via the menu
- Added to the ADC1 scan group after PA0/PA1 (CV inputs); 8–16× moving average in firmware for stable readings

See `user-interface.md` for full wiring, decoupling, and firmware details.

### Display Hardware

**Selected: Waveshare 1.5″ SSD1327 OLED, 128×128, 4-bit grayscale, SPI**
- Controller: SSD1327 (Solomon Systech) — 4-bit grayscale (16 levels per pixel); see `datasheets/SSD1327.md`
- SPI mode: 4-wire (SCK, MOSI, CS, DC) + RES; no MISO needed — display is write-only
- SPI Mode 0 (CPOL=0, CPHA=0), max 10 MHz SCLK; dedicated SPI1 bus (PA5 SCK, PA7 MOSI) — no mode conflict with DAC8552 (Mode 1 on SPI2)
- 3.3V logic supply (VDD), ~15–30 mA typical; panel VCC from on-board boost converter
- GDDRAM: 8192 bytes (two pixels per byte, upper/lower nibble). Full framebuffer transfer at 10 MHz: ~6.6 ms
- 128×128 px fits 5–6 menu lines at 8px font or 3–4 lines at 16px, plus a status bar; grayscale enables smooth UI for waveforms, bar graphs, and highlighted selections
- Module dimensions: ~44.5 mm × 37 mm PCB; visible area ~27mm × 27mm — suits a 12–14 HP panel
- **Dedicated bus:** OLED runs on SPI1 (PA5 SCK / PA7 MOSI), separate from the DAC8552 on SPI2. This eliminates SPI mode switching and allows concurrent DMA transfers (SPI1 on DMA2, SPI2 on DMA1) — the 6.6 ms OLED framebuffer write never blocks timing-critical DAC updates
- **Driver:** U8g2 `U8G2_SSD1327_WS_128X128` or Adafruit SSD1327 (SPI mode); not register-compatible with SSD1306 or SH1107

### Pin Budget

| Net name | Pin | Peripheral / AF |
|---|---|---|
| `CV-IN-A` | PA0 | ADC1_IN0 |
| `CV-IN-B` | PA1 | ADC1_IN1 |
| `CLK-IN` | PA2 | TIM2_CH3 input capture |
| `GATE-OUT-A` | PA3 | TIM2_CH4 output compare |
| `OLED-SPI-SCLK` | PA5 | SPI1_SCK (AF5) |
| `GATE-OUT-B` | PA6 | TIM3_CH1 output compare |
| `OLED-SPI-MOSI` | PA7 | SPI1_MOSI (AF5) |
| `SD_CD` | PA8 | GPIO input, 10 kΩ pull-up |
| `I2S3_WS` | PA15 | I2S3_WS (AF6) |
| `DAC-SPI-CS` | PB1 | GPIO output, active-low |
| `I2S3_BCK` | PB3 | I2S3_CK (AF6) |
| `USR-ENC-SW` | PB4 | GPIO EXTI, pull-up |
| `I2S3_SD` | PB5 | I2S3_SD (AF6) |
| `USR-ENC-A` | PB6 | TIM4_CH1 (AF2) |
| `USR-ENC-B` | PB7 | TIM4_CH2 (AF2) |
| `OLED-SPI-CS` | PB12 | GPIO output, active-low |
| `DAC-SPI-SCLK` | PB13 | SPI2_SCK (AF5) |
| `DAC-SPI-MOSI` | PB15 | SPI2_MOSI (AF5) |
| `USR-POT-1` | PC0 | ADC1_IN10 |
| `OLED-RES` | PC1 | GPIO output |
| `OLED-DC` | PC2 | GPIO output |
| `~MUTE` | PC6 | GPIO output, active-low |
| `SDIO_D0` | PC8 | SDIO_D0 (AF12) |
| `SDIO_D1` | PC9 | SDIO_D1 (AF12) |
| `SDIO_D2` | PC10 | SDIO_D2 (AF12) |
| `SDIO_D3` | PC11 | SDIO_D3 (AF12) |
| `SDIO_CK` | PC12 | SDIO_CK (AF12) |
| `SDIO_CMD` | PD2 | SDIO_CMD (AF12) |

All pins are drawn from the free GPIO pool. The OLED uses a dedicated SPI1 bus (PA5/PA7) — separate from DAC8552 on SPI2 (PB13/PB15) — so there is no bus contention or SPI mode conflict. I2S3 is on PB3/PB5 (AF6) to avoid conflict with the onboard SDIO (PC8–PC12). PB6/PB7 provide TIM4 hardware encoder mode (CH1/CH2). OLED-DC and OLED-RES moved to PC2/PC1 respectively.
