#include "wt_osc.h"
#include "audio.h"
#include "main.h"
#include <math.h>
#include <stdlib.h>

/* int16 PCM -> int32 output scale. int16 full-scale maps to ~0.375 of the
 * int32 range, matching the headroom the old sine tone used (0x30000000). This
 * is the per-voice scale BEFORE the 1/sqrt(active) poly normalisation, so a
 * single voice is identical to the old mono oscillator. */
#define WT_OUT_GAIN   24576.0f

#define INT32_MAXF    2147483647.0f
#define INT32_MINF   -2147483648.0f

/* One sounding voice. zone/phase_inc/active are written from the main loop
 * under a short critical section and read every block by the DMA IRQ; phase is
 * IRQ-owned (the main loop only writes it inside that critical section, to reset
 * a clean attack). midi/age are main-loop-only bookkeeping for note_off and
 * voice stealing — the render never reads them. */
typedef struct {
    const ms_zone_t *volatile zone;
    volatile float            phase_inc;
    volatile int              active;
    int                       midi;
    uint32_t                  age;
    float                     phase;
} wt_voice_t;

static const multisample_t *s_ms;
static wt_voice_t           s_voice[WT_MAX_VOICES];
static volatile float       s_level = 1.0f;
static uint32_t             s_age;     /* monotonic note_on stamp, main-loop */

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

/* Phase increment for a note in a zone (0 if the zone is empty). */
static float zone_inc(const ms_zone_t *z, float freq_hz)
{
    return (z && z->frames) ? (freq_hz * (float)z->frames / AUDIO_FS_HZ) : 0.0f;
}

/* DMA-IRQ render: sum every active voice (each a single-cycle table walked with
 * linear interp), scaled so a chord stays near a single voice's loudness.
 *
 * Force-optimized even in -O0 Debug builds: this runs in the I2S DMA IRQ and
 * must fill a half-buffer within the audio deadline (HALF_FRAMES / AUDIO_FS_HZ ≈
 * 667 µs at ~192 kHz). At -O0 the 4-voice float loop overruns that deadline,
 * which makes the DMA lap the CPU — the IRQ then re-fires back-to-back and
 * starves the main loop (frozen UI + stuck audio buffer). */
__attribute__((optimize("O3")))
static void wt_render(int32_t *stereo, uint32_t n, void *ctx)
{
    (void)ctx;

    /* Snapshot the active voices once per block so the inner loop touches no
     * volatiles and skips silent voices. */
    struct { const int16_t *t; uint32_t frames; float inc; float ph; int idx; }
        v[WT_MAX_VOICES];
    int nv = 0;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        const ms_zone_t *z = s_voice[i].zone;
        if (!s_voice[i].active || z == NULL || z->frames < 2 ||
            s_voice[i].phase_inc <= 0.0f)
            continue;
        float ph = s_voice[i].phase;
        if (ph >= (float)z->frames) ph = 0.0f;
        v[nv].t = z->samples;
        v[nv].frames = z->frames;
        v[nv].inc = s_voice[i].phase_inc;
        v[nv].ph = ph;
        v[nv].idx = i;
        nv++;
    }

    if (nv == 0) {
        for (uint32_t i = 0; i < n * 2; i++) stereo[i] = 0;
        return;
    }

    float gain = s_level * WT_OUT_GAIN / sqrtf((float)nv);

    for (uint32_t i = 0; i < n; i++) {
        float acc = 0.0f;
        for (int k = 0; k < nv; k++) {
            const int16_t *t = v[k].t;
            uint32_t Ni = v[k].frames;
            float    N  = (float)Ni;
            float    ph = v[k].ph;
            uint32_t i0 = (uint32_t)ph;
            float    fr = ph - (float)i0;
            uint32_t i1 = i0 + 1;
            if (i1 >= Ni) i1 = 0;
            acc += (float)t[i0] + ((float)t[i1] - (float)t[i0]) * fr;
            ph += v[k].inc;
            if (ph >= N) ph -= N;
            v[k].ph = ph;
        }
        float f = acc * gain;
        if (f > INT32_MAXF) f = INT32_MAXF;       /* clamp summed overshoot */
        else if (f < INT32_MINF) f = INT32_MINF;
        int32_t out = (int32_t)f;
        stereo[i * 2 + 0] = out;
        stereo[i * 2 + 1] = out;
    }

    for (int k = 0; k < nv; k++) s_voice[v[k].idx].phase = v[k].ph;
}

void wt_osc_init(void)
{
    s_ms = NULL;
    s_level = 1.0f;
    s_age = 0;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        s_voice[i].zone = NULL;
        s_voice[i].phase_inc = 0.0f;
        s_voice[i].active = 0;
        s_voice[i].midi = -1;
        s_voice[i].age = 0;
        s_voice[i].phase = 0.0f;
    }
}

void wt_osc_set_multisample(const multisample_t *ms)
{
    __disable_irq();
    s_ms = ms;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        s_voice[i].zone = NULL;
        s_voice[i].active = 0;
    }
    __enable_irq();
}

/* --- Monophonic: drive voice 0, silence the rest. --- */

static void mono_set(int midi_note, float freq_hz, int reset_phase)
{
    const ms_zone_t *z = select_zone(s_ms, midi_note);
    float inc = zone_inc(z, freq_hz);

    __disable_irq();
    if (reset_phase && z != s_voice[0].zone) s_voice[0].phase = 0.0f;
    s_voice[0].zone = z;
    s_voice[0].phase_inc = inc;
    s_voice[0].midi = midi_note;
    s_voice[0].active = 1;
    for (int i = 1; i < WT_MAX_VOICES; i++) s_voice[i].active = 0;
    __enable_irq();
}

void wt_osc_note(int midi_note, float freq_hz)
{
    mono_set(midi_note, freq_hz, 1);
}

void wt_osc_set_pitch(int midi_note, float freq_hz)
{
    mono_set(midi_note, freq_hz, 0);
}

/* --- Polyphonic. --- */

void wt_osc_chord(const int *midi_notes, const float *freq_hz, int count)
{
    if (count < 0) count = 0;
    if (count > WT_MAX_VOICES) count = WT_MAX_VOICES;

    /* Resolve zones/increments outside the critical section. */
    const ms_zone_t *z[WT_MAX_VOICES];
    float            inc[WT_MAX_VOICES];
    for (int i = 0; i < count; i++) {
        z[i] = select_zone(s_ms, midi_notes[i]);
        inc[i] = zone_inc(z[i], freq_hz[i]);
    }

    __disable_irq();
    for (int i = 0; i < count; i++) {
        s_voice[i].zone = z[i];
        s_voice[i].phase_inc = inc[i];
        s_voice[i].phase = 0.0f;          /* clean attack */
        s_voice[i].midi = midi_notes[i];
        s_voice[i].age = ++s_age;
        s_voice[i].active = 1;
    }
    for (int i = count; i < WT_MAX_VOICES; i++) s_voice[i].active = 0;
    __enable_irq();
}

/* Pick a free voice, else steal the oldest. */
static int alloc_voice(void)
{
    int oldest = 0;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        if (!s_voice[i].active) return i;
        if (s_voice[i].age < s_voice[oldest].age) oldest = i;
    }
    return oldest;
}

void wt_osc_note_on(int midi_note, float freq_hz)
{
    const ms_zone_t *z = select_zone(s_ms, midi_note);
    float inc = zone_inc(z, freq_hz);
    int i = alloc_voice();
    uint32_t age = ++s_age;

    __disable_irq();
    s_voice[i].zone = z;
    s_voice[i].phase_inc = inc;
    s_voice[i].phase = 0.0f;
    s_voice[i].midi = midi_note;
    s_voice[i].age = age;
    s_voice[i].active = 1;
    __enable_irq();
}

void wt_osc_note_off(int midi_note)
{
    for (int i = 0; i < WT_MAX_VOICES; i++)
        if (s_voice[i].active && s_voice[i].midi == midi_note)
            s_voice[i].active = 0;
}

void wt_osc_all_off(void)
{
    for (int i = 0; i < WT_MAX_VOICES; i++) s_voice[i].active = 0;
}

void wt_osc_gate_off(void)
{
    wt_osc_all_off();
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
