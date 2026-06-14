#include "audio.h"
#include "main.h"
#include <math.h>

extern I2S_HandleTypeDef hi2s3;

/* Continuous circular DMA buffer of DMA_FRAMES stereo frames, refilled one half
 * at a time from the I2S Tx half/complete callbacks. Streaming never stops, so
 * BCK/LRCK run continuously — the PCM5102A soft-mutes if its clocks halt. Each
 * frame is 4 halfwords: L_hi, L_lo, R_hi, R_lo (32-bit I2S sent as halfword
 * pairs). See audio-output-dac.md and firmware/README.md. */
#define DMA_FRAMES   256
#define HALF_FRAMES  (DMA_FRAMES / 2)
#define TONE_AMP     0x30000000   /* ~0.375 full-scale, headroom against clipping */
#define TWO_PI       6.283185307179586f

static uint16_t s_buf[DMA_FRAMES * 4];

/* Scratch for one half's interleaved L,R samples. Static (not stack) because it
 * is filled from the DMA IRQ; the half/complete callbacks never overlap. */
static int32_t s_scratch[HALF_FRAMES * 2];

static volatile audio_render_fn s_render;
static void *volatile          s_render_ctx;

/* Built-in tone source state. */
static struct {
  float phase;
  float inc;
} s_tone;

static void tone_render(int32_t *stereo, uint32_t n, void *ctx)
{
  (void)ctx;
  for (uint32_t i = 0; i < n; i++) {
    int32_t s = (int32_t)((float)TONE_AMP * sinf(s_tone.phase));
    s_tone.phase += s_tone.inc;
    if (s_tone.phase >= TWO_PI) s_tone.phase -= TWO_PI;
    stereo[i * 2 + 0] = s;   /* L */
    stereo[i * 2 + 1] = s;   /* R */
  }
}

/* Fill one half (HALF_FRAMES frames) of the circular buffer: pull samples from
 * the render source (or zeros), then pack each 32-bit L/R into hi/lo halfwords.
 * Runs in DMA IRQ context. Force-optimized even in -O0 builds — this is on the
 * audio deadline (see wt_render). */
__attribute__((optimize("O2")))
static void audio_fill_half(int half)
{
  audio_render_fn render = s_render;
  if (render) {
    render(s_scratch, HALF_FRAMES, s_render_ctx);
  } else {
    for (uint32_t i = 0; i < HALF_FRAMES * 2; i++) s_scratch[i] = 0;
  }

  int base = half ? HALF_FRAMES : 0;
  for (uint32_t i = 0; i < HALF_FRAMES; i++) {
    uint32_t l = (uint32_t)s_scratch[i * 2 + 0];
    uint32_t r = (uint32_t)s_scratch[i * 2 + 1];
    int idx = (base + (int)i) * 4;
    s_buf[idx + 0] = (uint16_t)(l >> 16);
    s_buf[idx + 1] = (uint16_t)(l & 0xFFFF);
    s_buf[idx + 2] = (uint16_t)(r >> 16);
    s_buf[idx + 3] = (uint16_t)(r & 0xFFFF);
  }
}

/* Circular Tx DMA callbacks: refill the half the DMA just finished playing
 * (HalfCplt = first half, Cplt = second half). Override the weak HAL symbols. */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI3) audio_fill_half(0);
}
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI3) audio_fill_half(1);
}

void audio_set_render(audio_render_fn fn, void *ctx)
{
  /* Publish ctx before fn so the IRQ never sees a new fn with a stale ctx. */
  s_render_ctx = ctx;
  __DMB();
  s_render = fn;
}

void audio_tone(float hz)
{
  s_tone.inc = TWO_PI * hz / AUDIO_FS_HZ;
  audio_set_render(tone_render, NULL);
}

void audio_tone_off(void)
{
  audio_set_render(NULL, NULL);
}

void audio_mute(bool muted)
{
  /* XSMT (~MUTE, PC6) is active-low: LOW = muted/standby, HIGH = unmuted. */
  HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin,
                    muted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void audio_init(void)
{
  s_render = NULL;
  s_render_ctx = NULL;
  s_tone.phase = 0.0f;
  s_tone.inc = 0.0f;

  audio_mute(true);   /* hold XSMT low until the clocks are stable */

  /* Prime both halves, then start the never-ending circular stream. Size = count
   * of 32-bit samples (L+R) = DMA_FRAMES*2; for the 32-bit data format the HAL
   * doubles it internally to DMA_FRAMES*4 halfwords = the whole buffer. */
  audio_fill_half(0);
  audio_fill_half(1);
  HAL_I2S_Transmit_DMA(&hi2s3, s_buf, DMA_FRAMES * 2);
}
