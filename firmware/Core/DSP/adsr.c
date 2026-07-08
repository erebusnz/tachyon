#include "adsr.h"
#include <math.h>

/* Attack aims past 1.0 so the one-pole reaches full level in finite time; it
 * is clamped at 1.0. With target 1.27, level crosses 1.0 after covering
 * 1/1.27 of the distance, i.e. ln(1.27/0.27) ≈ 1.549 time constants — the
 * attack time maps to that. */
#define ATT_TARGET     1.27f
#define ATT_TAU_FACTOR 1.5486f   /* ln(1.27 / 0.27) */

/* Decay/release "time" = 3 time constants (settles to ~5 % of the move). */
#define DR_TAU_FACTOR  3.0f

#define KILL_MS        5.0f

/* Release-to-idle threshold, ~-80 dB. */
#define IDLE_EPS       1e-4f

/* One-pole coefficient for a segment lasting `ms` at `tick_hz`, where the
 * nominal time spans `tau_factor` time constants. */
static float seg_coef(float ms, float tau_factor, float tick_hz)
{
    float tau_ticks = (ms * 0.001f * tick_hz) / tau_factor;
    if (tau_ticks < 1e-3f) tau_ticks = 1e-3f;   /* 0 ms -> completes in one tick */
    return 1.0f - expf(-1.0f / tau_ticks);
}

void adsr_config(adsr_params_t *p, float a_ms, float d_ms, float sustain,
                 float r_ms, float tick_hz)
{
    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;
    p->a_coef  = seg_coef(a_ms, ATT_TAU_FACTOR, tick_hz);
    p->d_coef  = seg_coef(d_ms, DR_TAU_FACTOR, tick_hz);
    p->r_coef  = seg_coef(r_ms, DR_TAU_FACTOR, tick_hz);
    p->k_coef  = seg_coef(KILL_MS, DR_TAU_FACTOR, tick_hz);
    p->sustain = sustain;
}

void adsr_init(adsr_t *e)
{
    e->level = 0.0f;
    e->stage = ADSR_IDLE;
}

void adsr_gate_on(adsr_t *e)
{
    e->stage = ADSR_ATTACK;
}

void adsr_gate_off(adsr_t *e)
{
    if (e->stage != ADSR_IDLE) e->stage = ADSR_RELEASE;
}

void adsr_kill(adsr_t *e)
{
    if (e->stage != ADSR_IDLE) e->stage = ADSR_KILL;
}

float adsr_step(adsr_t *e, const adsr_params_t *p)
{
    switch (e->stage) {
    case ADSR_ATTACK:
        e->level += p->a_coef * (ATT_TARGET - e->level);
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = ADSR_DECAY; }
        break;
    case ADSR_DECAY:
        e->level += p->d_coef * (p->sustain - e->level);
        if (fabsf(e->level - p->sustain) < IDLE_EPS) e->stage = ADSR_SUSTAIN;
        break;
    case ADSR_SUSTAIN:
        /* Keep tracking so live sustain edits are heard on held notes. */
        e->level += p->d_coef * (p->sustain - e->level);
        break;
    case ADSR_RELEASE:
        e->level -= p->r_coef * e->level;
        if (e->level < IDLE_EPS) { e->level = 0.0f; e->stage = ADSR_IDLE; }
        break;
    case ADSR_KILL:
        e->level -= p->k_coef * e->level;
        if (e->level < IDLE_EPS) { e->level = 0.0f; e->stage = ADSR_IDLE; }
        break;
    default:
        e->level = 0.0f;
        break;
    }
    return e->level;
}

bool adsr_idle(const adsr_t *e)
{
    return e->stage == ADSR_IDLE;
}
