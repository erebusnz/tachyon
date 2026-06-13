#include "wt_osc.h"
#include "audio.h"
#include "main.h"
#include <stdlib.h>

/* int16 PCM -> int32 output scale. int16 full-scale maps to ~0.375 of the
 * int32 range, matching the headroom the old sine tone used (0x30000000). */
#define WT_OUT_GAIN   24576.0f

/* Live parameters. Written from the main loop under a short critical section,
 * read every block from the DMA IRQ. s_phase is owned by the IRQ. */
static const multisample_t *s_ms;
static const ms_zone_t     *volatile s_zone;
static volatile float       s_phase_inc;
static volatile float       s_level = 1.0f;
static volatile int         s_active;
static float                s_phase;   /* IRQ-only */

static const ms_zone_t *select_zone(const multisample_t *ms, int midi)
{
    if (!ms || ms->zone_count == 0) return NULL;
    const ms_zone_t *best = NULL;
    int best_d = 1000;
    for (int i = 0; i < ms->zone_count; i++) {
        const ms_zone_t *z = &ms->zones[i];
        if (midi >= z->key_low && midi <= z->key_high) return z;  /* exact band */
        int d = abs((int)z->key_root - midi);
        if (d < best_d) { best_d = d; best = z; }
    }
    return best;
}

/* DMA-IRQ render: walk the selected single-cycle table with linear interp. */
static void wt_render(int32_t *stereo, uint32_t n, void *ctx)
{
    (void)ctx;
    const ms_zone_t *z   = s_zone;
    float            inc = s_phase_inc;
    float            gain = s_level * WT_OUT_GAIN;

    if (!s_active || z == NULL || z->frames < 2 || inc <= 0.0f) {
        for (uint32_t i = 0; i < n * 2; i++) stereo[i] = 0;
        return;
    }

    const int16_t *t = z->samples;
    float          N = (float)z->frames;
    uint32_t       Ni = z->frames;
    float          ph = s_phase;
    if (ph >= N) ph = 0.0f;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t i0 = (uint32_t)ph;
        float    fr = ph - (float)i0;
        uint32_t i1 = i0 + 1;
        if (i1 >= Ni) i1 = 0;
        float s = (float)t[i0] + ((float)t[i1] - (float)t[i0]) * fr;

        int32_t out = (int32_t)(s * gain);
        stereo[i * 2 + 0] = out;
        stereo[i * 2 + 1] = out;

        ph += inc;
        if (ph >= N) ph -= N;
    }
    s_phase = ph;
}

void wt_osc_init(void)
{
    s_ms = NULL;
    s_zone = NULL;
    s_phase_inc = 0.0f;
    s_level = 1.0f;
    s_active = 0;
    s_phase = 0.0f;
}

void wt_osc_set_multisample(const multisample_t *ms)
{
    __disable_irq();
    s_ms = ms;
    s_zone = NULL;
    s_active = 0;
    __enable_irq();
}

/* Select the zone for `midi_note`, tune to `freq_hz`, gate on. `reset_phase`
 * restarts the table (clean attack) on a new zone; continuous pitch updates
 * (CV) pass 0 to glide without clicks. */
static void wt_set(int midi_note, float freq_hz, int reset_phase)
{
    const ms_zone_t *z = select_zone(s_ms, midi_note);
    float inc = (z && z->frames) ? (freq_hz * (float)z->frames / AUDIO_FS_HZ) : 0.0f;

    __disable_irq();
    if (reset_phase && z != s_zone) s_phase = 0.0f;
    s_zone = z;
    s_phase_inc = inc;
    s_active = 1;
    __enable_irq();
}

void wt_osc_note(int midi_note, float freq_hz)
{
    wt_set(midi_note, freq_hz, 1);
}

void wt_osc_set_pitch(int midi_note, float freq_hz)
{
    wt_set(midi_note, freq_hz, 0);
}

void wt_osc_gate_off(void)
{
    s_active = 0;
}

void wt_osc_set_level(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    s_level = level;
}

void wt_osc_install(void)
{
    audio_set_render(wt_render, NULL);
}
