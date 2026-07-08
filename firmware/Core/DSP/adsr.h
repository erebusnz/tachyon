#ifndef ADSR_H
#define ADSR_H

#include <stdint.h>
#include <stdbool.h>

/* Block-rate exponential ADSR envelope (filters.md §2.1).
 *
 * One instance per voice; adsr_step() is called once per audio block (the
 * "tick") from the render IRQ and returns the new level, 0..1. Segments are
 * exponential one-pole moves toward a target — the classic analog shape. The
 * attack overshoots toward a target above 1.0 so it actually reaches full
 * level in the configured time instead of creeping asymptotically.
 *
 * Coefficients live in a shared adsr_params_t, computed from millisecond
 * times by adsr_config() on the main loop (it calls expf; the IRQ path never
 * does). Decay/release times are defined as time-to-95% (3 time constants).
 * KILL is a fixed ~5 ms fast release for panic/mode changes, so hard stops
 * don't click.
 *
 * Pure C, no HAL — compiled host-side by firmware/test/test_adsr.c. */

typedef struct {
    float a_coef, d_coef, r_coef;  /* per-tick one-pole coefficients */
    float k_coef;                  /* fixed ~5 ms kill coefficient */
    float sustain;                 /* 0..1 */
} adsr_params_t;

enum {
    ADSR_IDLE = 0, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE,
    ADSR_KILL
};

typedef struct {
    float   level;   /* current output, 0..1 */
    uint8_t stage;   /* ADSR_* */
} adsr_t;

/* Derive coefficients from segment times (ms) at `tick_hz` steps per second
 * (the audio block rate). Times are clamped to a minimum so a 0 ms segment
 * completes in one tick rather than dividing by zero. sustain clamped 0..1. */
void adsr_config(adsr_params_t *p, float a_ms, float d_ms, float sustain,
                 float r_ms, float tick_hz);

void adsr_init(adsr_t *e);      /* -> idle, level 0 */

/* Gate transitions. gate_on (re)enters attack FROM THE CURRENT LEVEL — a
 * retrigger mid-release does not snap to zero, so fast re-strikes don't
 * click. kill is a forced ~5 ms fade for panic stops. */
void adsr_gate_on(adsr_t *e);
void adsr_gate_off(adsr_t *e);
void adsr_kill(adsr_t *e);

/* Advance one tick and return the level (0..1). Idle returns 0. */
float adsr_step(adsr_t *e, const adsr_params_t *p);

/* Fully released — the owning voice can be reclaimed. */
bool adsr_idle(const adsr_t *e);

#endif
