#ifndef GATE_OUT_H
#define GATE_OUT_H

#include <stdint.h>
#include <stdbool.h>

/* Gate / trigger outputs (PA3, PA6) driven as plain push-pull GPIO.
 *
 * Callers work in terms of the JACK level; the IO-board MOSFET stage inverts
 * (jack HIGH = GPIO LOW, gate-output.md) and that inversion is applied here.
 * Pulse and clock modes run off a software-timed service routine — call
 * gate_out_service() every main-loop pass.
 */

#define GATE_A   0u
#define GATE_B   1u

/* Configure both gate pins as GPIO push-pull; power-up = jack LOW. */
void gate_out_init(void);

/* Static jack level. */
void gate_out_set(uint8_t ch, bool jack_high);

/* One-shot: jack HIGH now, auto-LOW after `ms` (serviced in gate_out_service). */
void gate_out_pulse(uint8_t ch, uint32_t ms);

/* Free-running square-wave clock at `bpm` (1 pulse per beat). */
void gate_out_clock(uint8_t ch, uint16_t bpm);

/* Advance pulse/clock timing. Pass HAL_GetTick(); call once per loop. */
void gate_out_service(uint32_t now_ms);

#endif
