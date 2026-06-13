#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

/* PCM5102A stereo audio engine (I2S3 + circular DMA).
 *
 * The DAC is driven from an uninterrupted circular DMA stream so BCK/LRCK never
 * stop — the PCM5102A soft-mutes if its clocks halt (see firmware/README.md).
 * audio_init() starts the stream emitting silence; it never stops. To emit
 * audio, register a render callback (or use the built-in tone). XSMT is held
 * asserted (muted) by audio_init(); unmute once the clocks are stable.
 *
 * 192 kHz, 32-bit Philips I2S, stereo.
 */

/* Fills `n_frames` interleaved stereo samples (L,R,L,R,...) — i.e. 2*n_frames
 * int32_t — into `stereo`. Called from the DMA Tx IRQ (block-rate, low NVIC
 * priority); keep it bounded. Full-scale is the int32_t range. */
typedef void (*audio_render_fn)(int32_t *stereo, uint32_t n_frames, void *ctx);

/* Audio sample rate (Hz) — the ACTUAL I2S LRCK rate, used for pitch.
 *
 * Exact 192.000 kHz is unreachable from this board's 8 MHz HSE with the integer
 * PLLI2S. With PLLI2SN=86 (I2SCLK=86 MHz) the 32-bit/MCLK-off divider of 7
 * yields 86e6/(64*7) = 191964.29 Hz, only -0.32 cents off 192 kHz. The
 * oscillator computes phase increments against this real rate so pitch is
 * accurate. See stm32f4xx_hal_msp.c (PLLI2S config). */
#define AUDIO_FS_HZ   191964.29f

/* Start the continuous (initially silent) I2S DMA stream and assert mute.
 * Call once after MX_I2S3_Init / MX_DMA_Init. */
void audio_init(void);

/* Register the sample source. NULL = digital silence (clocks keep running).
 * Safe to call from the main loop; the swap is IRQ-safe. */
void audio_set_render(audio_render_fn fn, void *ctx);

/* Built-in sine source: play a tone at `hz`, or stop (revert to silence). */
void audio_tone(float hz);
void audio_tone_off(void);

/* Drive XSMT (PC6, active-low): true = muted/standby, false = unmuted. */
void audio_mute(bool muted);

#endif
