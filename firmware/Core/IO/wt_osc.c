#include "wt_osc.h"
#include "audio.h"
#include "adsr.h"
#include "svf.h"
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

/* Envelope tick rate: once per render block. */
#define WT_TICK_HZ    (AUDIO_FS_HZ / (float)AUDIO_BLOCK_FRAMES)

/* Default velocity for the mono/chord helpers (filters.md §3.4). */
#define WT_DEF_VEL    100

/* One sounding voice. zone/phase_inc/active are written from the main loop
 * under a short critical section and read every block by the DMA IRQ; phase
 * and the SVF state are IRQ-owned. The envelope is stepped by the IRQ; the
 * main loop only flips its gate (stage byte) inside a critical section. midi/
 * age are main-loop bookkeeping for note_off and voice stealing; the render
 * never reads them (it does read env.level-adjacent state it owns).
 *
 * `active` means "occupies a slot and renders". With the filter on, note_off
 * leaves it set — the voice sounds through its release and the RENDER clears
 * it once the envelope idles (a plain int store; the main loop only sets it
 * inside irq-off sections, so the two writers never interleave). */
typedef struct {
    const ms_zone_t *volatile zone;
    volatile float            phase_inc;
    volatile int              active;
    int                       midi;
    uint32_t                  age;
    float                     phase;
    adsr_t                    env;       /* per-voice contour */
    svf_t                     filt;      /* per-voice lowpass state */
    float                     vel;       /* 0..1 normalised note velocity */
    float                     vel_gain;  /* amp multiplier (VOLUME mode) */
    float                     a_coef;    /* attack coef (ATK_TIME mode) */
} wt_voice_t;

static const multisample_t *s_ms;
static wt_voice_t           s_voice[WT_MAX_VOICES];
static volatile float       s_level = 1.0f;
static uint32_t             s_age;     /* monotonic note_on stamp, main-loop */

/* Filter/envelope parameters (filters.md §4 defaults). Single floats are
 * atomic stores on the M4 — written from the main loop, read per block. */
static volatile int   s_flt_en  = 1;
static volatile float s_cutoff  = 400.0f;   /* base cutoff, Hz */
static volatile float s_res     = 0.15f;    /* 0..1 */
static volatile float s_env_oct = 4.0f;     /* sweep depth, octaves */
static volatile int   s_vel_mode = WT_VEL_VOLUME;
static adsr_params_t  s_adsr;               /* swapped whole under irq-off */
static float          s_atk_ms = 5.0f;      /* configured attack, main loop */

static float s_def_vel;        /* WT_DEF_VEL normalised, from init */
static float s_def_vel_gain;   /* mode-baked values for the default velocity */
static float s_def_a_coef;

static volatile uint32_t s_max_cycles;      /* worst render block (DWT) */

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

/* Perceptual velocity-to-level curve (filters.md §3.5), on the normalised
 * 0..1 velocity. Main loop only. */
static float vel_curve(float vel01)
{
    return powf(vel01, 1.5f);
}

/* Velocity routing (filters.md §3.5): the mode picks the ONE thing velocity
 * drives. The per-note values are baked here on the main loop (powf/expf) so
 * the render only reads plain floats; the ATK_AMT/ATK_DEC/FLT_ENV modes shape
 * the contour at block rate in the render instead. */
static float vel_gain_for(float vel01)
{
    return (s_vel_mode == WT_VEL_VOLUME) ? vel_curve(vel01) : 1.0f;
}

static float atk_coef_for(float vel01)
{
    float ms = s_atk_ms;
    if (s_vel_mode == WT_VEL_ATK_TIME) {
        ms *= 1.0f - vel01;               /* harder = snappier */
        if (ms < 1.0f) ms = 1.0f;
    }
    return adsr_attack_coef(ms, WT_TICK_HZ);
}

static void refresh_def_vel(void)
{
    s_def_vel_gain = vel_gain_for(s_def_vel);
    s_def_a_coef   = atk_coef_for(s_def_vel);
}

/* Stop voice i sounding — anti-click fade with the filter on, hard cut with
 * it off. Call with IRQs disabled. */
static void hush_voice(int i)
{
    if (s_flt_en) {
        if (s_voice[i].active) adsr_kill(&s_voice[i].env);
    } else {
        s_voice[i].active = 0;
    }
}

/* DMA-IRQ render: sum every active voice (each a single-cycle table walked
 * with linear interp, then — filter path — through its SVF lowpass scaled by
 * its envelope), so a chord stays near a single voice's loudness.
 *
 * Block-rate work happens in the snapshot pass: one adsr_step, one exp2f and
 * one svf_coef per active voice; the inner loop touches no volatiles. A voice
 * whose envelope has fully released reclaims itself here (active = 0).
 *
 * Force-optimized even in -O0 Debug builds: this runs in the I2S DMA IRQ and
 * must fill a half-buffer within the audio deadline (AUDIO_BLOCK_FRAMES /
 * AUDIO_FS_HZ ≈ 667 µs at ~192 kHz). At -O0 the 4-voice float loop overruns
 * that deadline, which makes the DMA lap the CPU — the IRQ then re-fires
 * back-to-back and starves the main loop (frozen UI + stuck audio buffer). */
__attribute__((optimize("O3")))
static void wt_render(int32_t *stereo, uint32_t n, void *ctx)
{
    (void)ctx;
    uint32_t t0 = DWT->CYCCNT;
    int flt = s_flt_en;
    int vmode = s_vel_mode;

    /* Snapshot the active voices once per block so the inner loop touches no
     * volatiles and skips silent voices. Block-rate modulation happens here:
     * the render is always called with n == AUDIO_BLOCK_FRAMES (audio.c),
     * so one adsr_step per voice per call IS the ~1.5 kHz envelope tick. */
    struct {
        const int16_t *t; uint32_t frames; float nf; float inc; float ph;
        int idx;
        float eg;               /* per-voice level: env * velocity curve */
        svf_coef_t c; svf_t f;  /* this block's filter coefs + state */
    } v[WT_MAX_VOICES];
    int nv = 0;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        if (!s_voice[i].active) continue;

        float e = 0.0f;
        if (flt) {
            adsr_params_t pp = s_adsr;
            pp.a_coef = s_voice[i].a_coef;  /* per-note attack (ATK_TIME) */
            e = adsr_step(&s_voice[i].env, &pp);
            if (adsr_idle(&s_voice[i].env)) {   /* release finished: reclaim */
                s_voice[i].active = 0;
                continue;
            }
            /* Velocity-shaped contour (filters.md §3.5). */
            if (vmode == WT_VEL_ATK_AMT) {      /* transient above sustain */
                float su = s_adsr.sustain;
                if (e > su) e = su + (e - su) * s_voice[i].vel;
            } else if (vmode == WT_VEL_ATK_DEC) { /* whole contour */
                e *= s_voice[i].vel;
            }
        }

        const ms_zone_t *z = s_voice[i].zone;
        if (z == NULL || z->frames < 2 || s_voice[i].phase_inc <= 0.0f)
            continue;
        float ph = s_voice[i].phase;
        if (ph >= (float)z->frames) ph = 0.0f;
        v[nv].t = z->samples;
        v[nv].frames = z->frames;
        v[nv].nf = (float)z->frames;
        v[nv].inc = s_voice[i].phase_inc;
        v[nv].ph = ph;
        v[nv].idx = i;
        v[nv].eg = 1.0f;
        if (flt) {
            v[nv].eg = e * s_voice[i].vel_gain;
            /* Envelope sweep above the base cutoff; in FLT_ENV mode velocity
             * scales the sweep depth. */
            float sw = (vmode == WT_VEL_FLT_ENV) ? e * s_voice[i].vel : e;
            float fc = s_cutoff * exp2f(s_env_oct * sw);
            svf_coef(&v[nv].c, fc, s_res, AUDIO_FS_HZ);
            v[nv].f = s_voice[i].filt;
        }
        nv++;
    }

    if (nv == 0) {
        for (uint32_t i = 0; i < n * 2; i++) stereo[i] = 0;
        uint32_t dt = DWT->CYCCNT - t0;
        if (dt > s_max_cycles) s_max_cycles = dt;
        return;
    }

    float gain = s_level * WT_OUT_GAIN / sqrtf((float)nv);

    if (flt) {
        for (uint32_t i = 0; i < n; i++) {
            float acc = 0.0f;
            for (int k = 0; k < nv; k++) {
                const int16_t *t = v[k].t;
                uint32_t Ni = v[k].frames;
                float    N  = v[k].nf;
                float    ph = v[k].ph;
                uint32_t i0 = (uint32_t)ph;
                float    fr = ph - (float)i0;
                uint32_t i1 = i0 + 1;
                if (i1 >= Ni) i1 = 0;
                float s = (float)t[i0] + ((float)t[i1] - (float)t[i0]) * fr;
                acc += svf_lp(&v[k].f, &v[k].c, s) * v[k].eg;
                ph += v[k].inc;
                if (ph >= N) ph -= N;
                v[k].ph = ph;
            }
            float f = acc * gain;
            if (f > INT32_MAXF) f = INT32_MAXF;   /* clamp summed overshoot */
            else if (f < INT32_MINF) f = INT32_MINF;
            int32_t out = (int32_t)f;
            stereo[i * 2 + 0] = out;
            stereo[i * 2 + 1] = out;
        }
        for (int k = 0; k < nv; k++) {
            s_voice[v[k].idx].phase = v[k].ph;
            s_voice[v[k].idx].filt  = v[k].f;
        }
    } else {
        /* Bypass: the original raw render path. */
        for (uint32_t i = 0; i < n; i++) {
            float acc = 0.0f;
            for (int k = 0; k < nv; k++) {
                const int16_t *t = v[k].t;
                uint32_t Ni = v[k].frames;
                float    N  = v[k].nf;
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
            if (f > INT32_MAXF) f = INT32_MAXF;
            else if (f < INT32_MINF) f = INT32_MINF;
            int32_t out = (int32_t)f;
            stereo[i * 2 + 0] = out;
            stereo[i * 2 + 1] = out;
        }
        for (int k = 0; k < nv; k++) s_voice[v[k].idx].phase = v[k].ph;
    }

    uint32_t dt = DWT->CYCCNT - t0;
    if (dt > s_max_cycles) s_max_cycles = dt;
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
        adsr_init(&s_voice[i].env);
        svf_reset(&s_voice[i].filt);
        s_voice[i].vel = 0.0f;
        s_voice[i].vel_gain = 0.0f;
    }
    s_def_vel = (float)WT_DEF_VEL / 127.0f;
    /* filters.md §4.1 defaults; app.c pushes the UI's values over these. */
    s_atk_ms = 5.0f;
    adsr_config(&s_adsr, 5.0f, 300.0f, 0.6f, 200.0f, WT_TICK_HZ);
    refresh_def_vel();
    for (int i = 0; i < WT_MAX_VOICES; i++) s_voice[i].a_coef = s_adsr.a_coef;
    s_max_cycles = 0;
}

void wt_osc_set_multisample(const multisample_t *ms)
{
    __disable_irq();
    s_ms = ms;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        s_voice[i].zone = NULL;
        s_voice[i].active = 0;              /* hard cut: table is going away */
        adsr_init(&s_voice[i].env);
        svf_reset(&s_voice[i].filt);
    }
    __enable_irq();
}

/* --- Monophonic: drive voice 0, hush the rest. --- */

static void mono_set(int midi_note, float freq_hz, int reset_phase, int retrig)
{
    const ms_zone_t *z = select_zone(s_ms, midi_note);
    float inc = zone_inc(z, freq_hz);

    __disable_irq();
    if (reset_phase && z != s_voice[0].zone) s_voice[0].phase = 0.0f;
    s_voice[0].zone = z;
    s_voice[0].phase_inc = inc;
    s_voice[0].midi = midi_note;
    if (!s_voice[0].active) {               /* fresh start on a silent voice */
        svf_reset(&s_voice[0].filt);
        retrig = 1;
    }
    if (retrig) adsr_gate_on(&s_voice[0].env);
    s_voice[0].vel = s_def_vel;
    s_voice[0].vel_gain = s_def_vel_gain;
    s_voice[0].a_coef = s_def_a_coef;
    s_voice[0].active = 1;
    for (int i = 1; i < WT_MAX_VOICES; i++) hush_voice(i);
    __enable_irq();
}

void wt_osc_note(int midi_note, float freq_hz)
{
    mono_set(midi_note, freq_hz, 1, 1);
}

void wt_osc_set_pitch(int midi_note, float freq_hz)
{
    mono_set(midi_note, freq_hz, 0, 0);   /* no retrigger: click-free glide */
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
        if (!s_voice[i].active) svf_reset(&s_voice[i].filt);
        s_voice[i].zone = z[i];
        s_voice[i].phase_inc = inc[i];
        s_voice[i].phase = 0.0f;          /* clean attack */
        s_voice[i].midi = midi_notes[i];
        s_voice[i].age = ++s_age;
        s_voice[i].vel = s_def_vel;
        s_voice[i].vel_gain = s_def_vel_gain;
        s_voice[i].a_coef = s_def_a_coef;
        adsr_gate_on(&s_voice[i].env);
        s_voice[i].active = 1;
    }
    for (int i = count; i < WT_MAX_VOICES; i++) hush_voice(i);
    __enable_irq();
}

/* Pick the voice for a new note: the same note's own voice (re-strike), else
 * a free one, else the quietest releasing one, else steal the oldest. */
static int alloc_voice(int midi_note)
{
    for (int i = 0; i < WT_MAX_VOICES; i++)
        if (s_voice[i].active && s_voice[i].midi == midi_note) return i;
    for (int i = 0; i < WT_MAX_VOICES; i++)
        if (!s_voice[i].active) return i;
    int rel = -1;
    float rel_lvl = 2.0f;
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        uint8_t st = s_voice[i].env.stage;
        if ((st == ADSR_RELEASE || st == ADSR_KILL) && s_voice[i].env.level < rel_lvl) {
            rel_lvl = s_voice[i].env.level;
            rel = i;
        }
    }
    if (rel >= 0) return rel;
    int oldest = 0;
    for (int i = 1; i < WT_MAX_VOICES; i++)
        if (s_voice[i].age < s_voice[oldest].age) oldest = i;
    return oldest;
}

void wt_osc_note_on(int midi_note, float freq_hz, uint8_t vel)
{
    const ms_zone_t *z = select_zone(s_ms, midi_note);
    float inc = zone_inc(z, freq_hz);
    float v01 = (float)vel / 127.0f;
    float vg  = vel_gain_for(v01);
    float ac  = atk_coef_for(v01);
    int i = alloc_voice(midi_note);
    uint32_t age = ++s_age;

    __disable_irq();
    if (!s_voice[i].active) svf_reset(&s_voice[i].filt);
    s_voice[i].zone = z;
    s_voice[i].phase_inc = inc;
    s_voice[i].phase = 0.0f;
    s_voice[i].midi = midi_note;
    s_voice[i].age = age;
    s_voice[i].vel = v01;
    s_voice[i].vel_gain = vg;
    s_voice[i].a_coef = ac;
    adsr_gate_on(&s_voice[i].env);        /* attacks from the current level */
    s_voice[i].active = 1;
    __enable_irq();
}

void wt_osc_note_off(int midi_note)
{
    __disable_irq();
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        if (!s_voice[i].active || s_voice[i].midi != midi_note) continue;
        if (s_flt_en) adsr_gate_off(&s_voice[i].env);  /* sounds out its release */
        else          s_voice[i].active = 0;
    }
    __enable_irq();
}

void wt_osc_all_off(void)
{
    __disable_irq();
    for (int i = 0; i < WT_MAX_VOICES; i++) hush_voice(i);
    __enable_irq();
}

void wt_osc_gate_off(void)
{
    __disable_irq();
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        if (!s_voice[i].active) continue;
        if (s_flt_en) adsr_gate_off(&s_voice[i].env);
        else          s_voice[i].active = 0;
    }
    __enable_irq();
}

void wt_osc_set_level(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    s_level = level;
}

/* --- Filter/envelope parameters. --- */

void wt_osc_set_filter_enabled(bool en)
{
    __disable_irq();
    if ((int)en != s_flt_en) {
        s_flt_en = en;
        /* Deterministic switch: cut everything and reset per-voice state so
         * neither path inherits the other's — a held note carried across
         * would otherwise jump to full raw level (into bypass) or duck and
         * swell back (out of it). Toggling the filter cuts sounding notes;
         * it's a config action. */
        for (int i = 0; i < WT_MAX_VOICES; i++) {
            s_voice[i].active = 0;
            adsr_init(&s_voice[i].env);
            svf_reset(&s_voice[i].filt);
        }
    }
    __enable_irq();
}

void wt_osc_set_cutoff(float hz)
{
    if (hz < 20.0f)     hz = 20.0f;
    if (hz > 20000.0f)  hz = 20000.0f;
    s_cutoff = hz;
}

void wt_osc_set_resonance(float res)
{
    if (res < 0.0f) res = 0.0f;
    if (res > 1.0f) res = 1.0f;
    s_res = res;
}

void wt_osc_set_env_amount(float oct)
{
    if (oct < 0.0f) oct = 0.0f;
    if (oct > 7.0f) oct = 7.0f;
    s_env_oct = oct;
}

void wt_osc_set_vel_mode(wt_vel_mode_t m)
{
    if ((int)m < 0 || (int)m >= WT_VEL_MODE_COUNT) return;
    s_vel_mode = m;
    refresh_def_vel();
    /* vel_gain/a_coef are baked per note; rebake sounding voices from their
     * stored normalised velocity so the switch is heard immediately. Baking
     * calls powf/expf, so compute outside the critical section (vel is
     * main-loop-owned — safe to read unlocked). */
    float vg[WT_MAX_VOICES], ac[WT_MAX_VOICES];
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        vg[i] = vel_gain_for(s_voice[i].vel);
        ac[i] = atk_coef_for(s_voice[i].vel);
    }
    __disable_irq();
    for (int i = 0; i < WT_MAX_VOICES; i++) {
        s_voice[i].vel_gain = vg[i];
        s_voice[i].a_coef   = ac[i];
    }
    __enable_irq();
}

void wt_osc_set_adsr(float a_ms, float d_ms, float sustain, float r_ms)
{
    adsr_params_t p;
    adsr_config(&p, a_ms, d_ms, sustain, r_ms, WT_TICK_HZ);
    s_atk_ms = a_ms;
    refresh_def_vel();
    /* Per-voice attack coefs derive from the attack time; rebake so a live
     * attack edit is heard on retriggers (ATK_TIME mode scales per note). */
    float ac[WT_MAX_VOICES];
    for (int i = 0; i < WT_MAX_VOICES; i++) ac[i] = atk_coef_for(s_voice[i].vel);
    __disable_irq();
    s_adsr = p;                 /* swap whole so one block never sees a mix */
    for (int i = 0; i < WT_MAX_VOICES; i++) s_voice[i].a_coef = ac[i];
    __enable_irq();
}

uint32_t wt_osc_render_max_us(void)
{
    uint32_t c = s_max_cycles;
    s_max_cycles = 0;
    return c / (SystemCoreClock / 1000000u);
}

void wt_osc_install(void)
{
    /* Enable the DWT cycle counter for the render-time measurement. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    audio_set_render(wt_render, NULL);
}
