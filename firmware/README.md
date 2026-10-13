# Firmware

## Voice transitions and control acquisition

Replacing a sounding wavetable voice (including MIDI retriggers and chord
changes) fades the old voice out over 5 ms, starts the requested note at the
next audio block, then fades in over 5 ms. This also applies in filter bypass.
Only four voices render at once; a replacement can wait up to about 5.7 ms
from the first render block after its request. Note-off, panic, filter-mode
changes and wavetable unloading cancel pending notes. Continuous pitch updates
retain legato behavior.

ADC1 scans CV-A, CV-B and the adjustment pot at 1 kHz, triggered by TIM8 TRGO.
DMA2 Stream0/channel 0 stores alternating three-sample scans in SRAM and
publishes them from short DMA callbacks. TIM8 needs no external pin and is
owned by `analog_in`; TIM2/TIM3 gate functions are unchanged. CV samples are
unfiltered; the pot has a 1/8 one-pole filter at 1 kHz (about 7.5 ms time
constant). There is no ADC polling in the main loop. Application/sequence
updates still consume the latest readings in the main loop; this change does
not move the application or clock handling into an interrupt.

The `diag:` line reports completed ADC scans (`adc n`, about 2000 per normal
two-second reporting interval), useful when checking sampling during OLED
activity. Hardware validation should check distinct DC voltages on the three
channels, CV steps during redraws, and the worst audio-render time during rapid
four-voice replacements against the ~667 us block deadline.

TIM8 and the ADC DMA stream are initialized by `analog_in_init`, rather than
CubeMX-generated timer/DMA setup. Preserve this ownership when regenerating.

## Flashing

### ST-LINK/V2 over SWD (preferred)

Fastest path — no BOOT0/DFU dance, ~2 s per flash. Wire the ST-LINK/V2 to the
WeAct board's SWD header (PA13/PA14, see `hardware-design-plan.md`):

| ST-LINK pin | Board pin |
|---|---|
| SWDIO | PA13 (`DIO`) |
| SWCLK | PA14 (`CLK`) |
| GND | GND |
| 3.3V (VTref) | 3V3 |

The `3.3V`/VTref wire is required even when the board is self-powered — the
ST-LINK senses target voltage on it before driving the SWD lines (a missing
VTref shows up as `Voltage : 0.00V` and `Unable to get core ID`). With the board
self-powered this ties the probe's 3.3 V to the live 3V3 rail (both ~3.3 V),
which is fine for programming.

Build and flash with the helper script (auto-discovers `STM32_Programmer_CLI`):
```powershell
./flash.ps1            # flash build/Debug/firmware.elf
./flash.ps1 -Build     # build Debug first, then flash
./flash.ps1 Release    # flash the Release build
```

Or invoke the programmer directly:
```
STM32_Programmer_CLI --connect port=SWD mode=UR --download build\Debug\firmware.elf --start
```

### DFU over USB (fallback, no ST-LINK)

Hold down the BOOT0 (B0) button and connect to the computer via USB, release after 1s. You should now be in DFU mode over USB.

You can test connection with:
```
STM32_Programmer_CLI --connect port=USB1
```

Flashing firmware:
```
STM32_Programmer_CLI --connect port=USB1 --download build\Debug\firmware.elf 0x08000000 --start
```

## Audio output (PCM5102A) — I2S considerations

The audio DAC (U3, PCM5102A) is driven from **I2S3**: `PB3` = BCK, `PB5` =
DIN/SD, `PA15` = LRCK/WS, with `PC6` = `~MUTE` to the XSMT pin. It is wired in
**3-wire mode**: `SCK` (pin 12) is tied to GND, so the part runs its **internal
PLL**, deriving the system clock from BCK — there is no MCLK. See
`audio-output-dac.md` and `datasheets/PCM5102A.md` for the full wiring.

This mode imposes firmware rules that are easy to get wrong and produce a DAC
that is powered, unmuted, and correctly clocked yet **completely silent**:

- **The I2S clocks must run continuously — never stop BCK/LRCK.** The PCM5102A
  treats any clock halt or invalid BCK/LRCK relationship (more than 4 LRCK
  periods) as a clock error: it drops to **standby and forces the analog
  output to bipolar-zero (silence)** until the clocks resynchronise. A blocking
  / one-shot `HAL_I2S_Transmit()` per buffer stops the clocks between bursts and
  will leave the output muted.
- **Drive the DAC from a circular DMA**, not polling/normal-mode transfers.
  `HAL_I2S_Transmit_DMA()` with the SPI3_TX stream (`DMA1_Stream5`, Channel 0)
  in `DMA_CIRCULAR` mode keeps the clocks free-running even when the CPU is busy
  (e.g. during a blocking OLED frame blit): if a refill callback is late the DMA
  simply replays the buffer rather than halting the clocks. Refill the buffer
  halves from `HAL_I2S_TxHalfCpltCallback` / `HAL_I2S_TxCpltCallback`. To emit
  silence, stream zeros — **do not stop the DMA.**
- **Let the PLL lock before expecting output.** With SCK grounded the internal
  PLL only starts after BCK + LRCK have run for **16 successive LRCK periods**.
  Start the (silent) stream early in init so the PLL is locked by the time audio
  is needed.
- **XSMT (`~MUTE`, PC6) sequencing:** hold it **LOW** through init until the I2S
  clocks are stable, then drive it **HIGH** to unmute (the net is active-low; PC6
  goes straight to XSMT with no inverter). XSMT LOW is also the part's
  power-down state.
- **Clock ratio:** the build runs **192 kHz, 32-bit** I2S (Philips standard),
  i.e. **64 BCK per stereo frame** (32 BCK/channel) → BCK = 12.288 MHz. That is
  within the PLL's supported BCK/LRCK range (32/48/64 per channel); changing the
  sample rate or word length must keep the ratio in that range.

> The `firmware-test/` bring-up console drives the DAC exactly this way
> (continuous circular DMA streaming silence from boot, sine on `tone <hz>`);
> mirror that pattern in the application audio path.
