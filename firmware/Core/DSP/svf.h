#ifndef SVF_H
#define SVF_H

/* Simper TPT (topology-preserving transform) state-variable filter, lowpass
 * output (filters.md §2.2). Chosen for stability under fast per-block cutoff
 * modulation — an envelope sweep is exactly that.
 *
 * svf_coef() derives coefficients from cutoff/resonance at block rate on the
 * render path (one tanf per voice per block); svf_lp() is the per-sample
 * state update, defined inline here so the O3 render loop in wt_osc.c can
 * inline it (no LTO in this build).
 *
 * The filter is linear, so it works at any signal scale — wt_osc feeds it
 * int16-range floats. Resonance 0..1 is clamped below self-oscillation.
 *
 * Pure C, no HAL — compiled host-side by firmware/test/test_svf.c. */

/* Per-sample constants derived from g = tan(pi*fc/fs) and k = 2 - 2*res:
 * a1 = 1/(1 + g*(g+k)), a2 = g*a1, a3 = g*a2. Only these three are needed
 * per sample, so only they are stored (the struct is copied per voice per
 * block on the render hot path). */
typedef struct {
    float a1, a2, a3;
} svf_coef_t;

typedef struct {
    float ic1eq, ic2eq;  /* integrator states */
} svf_t;

/* Compute coefficients for cutoff `fc_hz` and resonance `res` (0..1) at
 * sample rate `fs_hz`. fc clamped to [20 Hz, min(20 kHz, 0.45*fs)], res
 * clamped to [0, 0.97] (k >= 0.06, Q ~ 16 — below self-oscillation). */
void svf_coef(svf_coef_t *c, float fc_hz, float res, float fs_hz);

void svf_reset(svf_t *s);

/* One sample through the lowpass. */
static inline float svf_lp(svf_t *s, const svf_coef_t *c, float v0)
{
    float v3 = v0 - s->ic2eq;
    float v1 = c->a1 * s->ic1eq + c->a2 * v3;
    float v2 = s->ic2eq + c->a2 * s->ic1eq + c->a3 * v3;
    s->ic1eq = 2.0f * v1 - s->ic1eq;
    s->ic2eq = 2.0f * v2 - s->ic2eq;
    return v2;
}

#endif
