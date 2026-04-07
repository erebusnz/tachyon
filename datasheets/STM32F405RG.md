# STM32F405RG -- ARM Cortex-M4 Microcontroller with FPU, 1MB Flash, 168 MHz

## BOM Details
| Field | Value |
|-------|-------|
| Manufacturer Part | STM32F405RGT6 |
| Manufacturer | STMicroelectronics |
| Package | LQFP-64 (WeAct dev board) |
| Designator(s) | U1 |
| Quantity | 1 |
| Supplier Part | WeAct module |

## Description
The STM32F405RG is a high-performance ARM Cortex-M4 microcontroller with a single-precision floating-point unit (FPU), running at up to 168 MHz. It features 1 MB of Flash memory, 192 KB of SRAM (128 KB main + 64 KB CCM), and a rich set of peripherals including USB OTG, multiple SPI/I2S interfaces, and dual 12-bit ADCs. The device is well-suited for audio and real-time signal processing applications due to its DSP instruction set, I2S interfaces with dedicated PLL, and DMA capabilities.

## Key Electrical Characteristics
| Parameter | Min | Typ | Max | Unit | Condition |
|-----------|-----|-----|-----|------|-----------|
| Core | -- | ARM Cortex-M4F | -- | -- | With FPU and DSP instructions |
| Max Clock Frequency (SYSCLK) | -- | -- | 168 | MHz | VDD >= 2.7 V |
| Flash Memory | -- | 1024 | -- | KB | -- |
| SRAM (main) | -- | 128 | -- | KB | -- |
| SRAM (CCM) | -- | 64 | -- | KB | Core-coupled memory, no DMA access |
| Supply Voltage (VDD) | 1.8 | -- | 3.6 | V | -- |
| I/O Voltage (VIH, 5V-tolerant pins) | -- | -- | 5.5 | V | FT pins only |
| Operating Temperature | -40 | -- | +85 | degC | Industrial grade |
| GPIO (LQFP-64) | -- | 51 | -- | -- | 5V-tolerant I/Os |
| Advanced Timers (TIM1, TIM8) | -- | 2 | -- | -- | 16-bit, PWM, complementary outputs |
| General-Purpose Timers (TIM2-TIM5) | -- | 4 | -- | -- | TIM2/TIM5: 32-bit; TIM3/TIM4: 16-bit |
| General-Purpose Timers (TIM9-TIM14) | -- | 6 | -- | -- | 16-bit |
| Basic Timers (TIM6, TIM7) | -- | 2 | -- | -- | 16-bit, DAC trigger |
| SPI / I2S | -- | 3 | -- | -- | SPI1/2/3; SPI2/3 have full-duplex I2S |
| I2C | -- | 3 | -- | -- | I2C1/2/3; SMBus/PMBus compatible |
| USART / UART | -- | 4/2 | -- | -- | USART1/2/3/6 + UART4/5 |
| USB OTG FS | -- | 1 | -- | -- | Full-speed, device/host/OTG |
| USB OTG HS | -- | 1 | -- | -- | With ULPI or internal FS PHY |
| ADC (12-bit) | -- | 3 | -- | -- | ADC1/2/3, up to 16 external channels (LQFP-64) |
| ADC Sampling Rate | -- | -- | 2.4 | MSPS | Triple interleaved mode |
| DAC (12-bit) | -- | 2 | -- | ch | DAC1/2 |
| DMA Controllers | -- | 2 | -- | -- | 16 streams total (8 per controller) |
| RTC | -- | 1 | -- | -- | With calendar, alarm, tamper detection |
| SDIO | -- | 1 | -- | -- | SD/MMC interface |
| RNG | -- | 1 | -- | -- | True random number generator |
| CRC | -- | 1 | -- | -- | Hardware CRC calculation unit |

## Pinout
The STM32F405RGT6 on the WeAct dev board uses the LQFP-64 package. Rather than listing all 64 pins, below is the peripheral availability relevant to a sequencer/audio project:

**SPI Buses:**
- SPI1: PA5 (SCK), PA6 (MISO), PA7 (MOSI) -- or alternate pins PB3/PB4/PB5
- SPI2: PB13 (SCK), PB14 (MISO), PB15 (MOSI) -- also supports I2S2
- SPI3: PB3 (SCK), PB4 (MISO), PB5 (MOSI) -- also supports I2S3

**I2S Interfaces:**
- I2S2: PB12 (WS), PB13 (CK), PB15 (SD), PC6 or PB9 (MCK), PB14 (ext_SD for full-duplex)
- I2S3: PA4 or PA15 (WS), PB3 or PC10 (CK), PB5 or PC12 (SD), PC7 (MCK), PB4 or PC11 (ext_SD for full-duplex)

**I2C Buses:**
- I2C1: PB6 (SCL), PB7 (SDA) -- or PB8/PB9
- I2C2: PB10 (SCL), PB11 (SDA)
- I2C3: PA8 (SCL), PC9 (SDA)

**USART/UART:**
- USART1: PA9 (TX), PA10 (RX) -- or PB6/PB7
- USART2: PA2 (TX), PA3 (RX)
- USART3: PB10 (TX), PB11 (RX) -- or PC10/PC11
- UART4: PA0 (TX), PA1 (RX)
- UART5: PC12 (TX), PD2 (RX)
- USART6: PC6 (TX), PC7 (RX)

**USB:**
- USB OTG FS: PA11 (DM), PA12 (DP) -- used on WeAct board for USB-C connector
- USB OTG HS: PB14 (DM), PB15 (DP) -- internal FS PHY mode

**ADC Channels (LQFP-64):**
- PA0-PA7: ADC_IN0 through ADC_IN7
- PB0-PB1: ADC_IN8, ADC_IN9
- PC0-PC5: ADC_IN10 through ADC_IN15
- Up to 16 external channels available

**Other notable pins:**
- BOOT0: dedicated pin (active high to enter bootloader)
- NRST: reset pin
- OSC_IN/OSC_OUT: PA0 area -- HSE crystal (8 MHz on WeAct board)
- PC13: on-board LED on WeAct board (active low)
- PA0: on-board user button on WeAct board

## Absolute Maximum Ratings
| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| VDD supply voltage | -0.3 | 4.0 | V |
| VIN on 5V-tolerant I/O pins | -0.3 | VDD + 4.0 (max 5.5) | V |
| VIN on non-5V-tolerant pins | -0.3 | 4.0 | V |
| Junction temperature (Tj) | -40 | +125 | degC |
| Storage temperature | -65 | +150 | degC |
| ESD (HBM, all pins) | -- | 2000 | V |
| ESD (CDM, all pins) | -- | 500 | V |

## Application Notes
**Clock Configuration:**
- HSE (High-Speed External): 4 to 26 MHz crystal or external clock. The WeAct board uses an 8 MHz crystal.
- HSI (High-Speed Internal): 16 MHz RC oscillator (factory trimmed to 1% accuracy).
- Main PLL fed from HSE or HSI, generating up to 168 MHz SYSCLK. Typical PLL config with 8 MHz HSE: PLLM=8, PLLN=336, PLLP=2 yields 168 MHz.
- APB1 peripheral bus: max 42 MHz (APB1 timer clocks run at 2x = 84 MHz).
- APB2 peripheral bus: max 84 MHz (APB2 timer clocks run at 2x = 168 MHz).
- PLLI2S: dedicated PLL for I2S clock generation, allowing precise audio sample rates (e.g., 44.1 kHz, 48 kHz, 96 kHz).

**Boot Mode Pins:**
- BOOT0 = 0: Boot from main Flash (normal operation).
- BOOT0 = 1, BOOT1 = 0: Boot from System Memory (built-in UART/USB DFU bootloader).
- BOOT0 = 1, BOOT1 = 1: Boot from embedded SRAM.
- The WeAct board provides a BOOT0 button for entering DFU mode.

**Power Supply Decoupling:**
- Each VDD pin requires a 100 nF ceramic capacitor to ground, placed as close as possible to the pin.
- A 4.7 uF bulk capacitor on VDD is recommended.
- VDDA (analog supply) requires a 1 uF + 100 nF capacitor pair and should be filtered from the digital VDD.
- VCAP1 and VCAP2 pins each require a 2.2 uF ceramic capacitor to ground (internal voltage regulator output at 1.2 V).
- VBAT requires a 100 nF decoupling capacitor.

**HSE Crystal Requirements:**
- Load capacitance: typically 10-20 pF (depends on crystal specification).
- The WeAct board includes an 8 MHz crystal with appropriate load capacitors.
- For low-power or high-accuracy applications, use a low-ESR crystal.

**Key Peripherals for Audio (I2S, SPI, DMA):**
- I2S2 and I2S3 support Philips I2S, MSB-justified, LSB-justified, and PCM standards at 16/24/32-bit data widths.
- I2S master clock output (MCK) can be enabled for codecs/DACs that require an external master clock (typically 256x fs).
- The PLLI2S provides a dedicated, tunable clock source for accurate audio sample rates without affecting the system clock.
- Full-duplex I2S is achieved using an I2S peripheral plus its extension (e.g., I2S2 + I2S2_ext) for simultaneous transmit and receive.
- DMA is essential for efficient audio streaming: SPI/I2S peripherals are connected to DMA1 (SPI2/I2S2, SPI3/I2S3) and DMA2 (SPI1). Use circular DMA mode with double-buffering for glitch-free audio.
- DMA streams support FIFO with configurable threshold (1/4, 1/2, 3/4, full) for burst transfers.
- For DAC output (e.g., DAC8552 via SPI), DMA-driven SPI transfers triggered by a timer ensure precise sample timing.
