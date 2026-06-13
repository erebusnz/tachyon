#ifndef CLOCK_IN_H
#define CLOCK_IN_H

#include <stdint.h>
#include <stdbool.h>

/* External clock input: CLK-IN on PA2 captured by TIM2_CH3 (clock-input.md).
 * Non-blocking — call clock_in_poll() each loop to track the inter-edge period;
 * the period goes stale (declared "no clock") after ~1.5 s without an edge.
 */

/* Start the input-capture channel. Call once after MX_TIM2_Init. */
void clock_in_init(void);

/* Service captured edges and staleness. Pass HAL_GetTick(); call once per loop. */
void clock_in_poll(uint32_t now_ms);

/* Returns true once per captured edge (consumes the latched flag). Lets a
 * consumer (e.g. the arpeggiator) step in phase with the external clock. */
bool clock_in_edge(void);

/* True while a live clock is being received. */
bool clock_in_present(void);

/* Measured input frequency in Hz (0 if no clock). */
float clock_in_hz(void);

/* Tempo in BPM for the given pulses-per-quarter-note (0 if no clock). */
float clock_in_bpm(uint8_t ppqn);

#endif
