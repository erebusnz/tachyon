#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <stdbool.h>

/* Arpeggiator engine — pure logic, no HAL. The caller passes monotonic time
 * (now_ms) and receives a callback on each step; mapping the abstract note to
 * CV / tone / gate is the caller's job (keeps the engine decoupled).
 *
 * A note list is built from the chord offsets across the octave range, ordered
 * by direction; a step counter wraps at the sequence length so the pattern
 * loops every `length` steps with the notes cycling within it.
 */

#define ARP_MAX_CHORD   6u
#define ARP_MAX_OCT     4u
#define ARP_MAX_NOTES   (ARP_MAX_CHORD * ARP_MAX_OCT)
#define ARP_MAX_SEQ     (2u * ARP_MAX_NOTES)   /* UPDOWN ping-pong worst case */

typedef enum { ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_DIR_COUNT } arp_dir_t;
typedef enum { ARP_CLK_INTERNAL = 0, ARP_CLK_EXTERNAL } arp_clk_t;

/* semitone = absolute offset from the root (chord offset + 12*octave);
 * gate_ms  = suggested note/gate length (tracks the live inter-step interval). */
typedef void (*arp_step_fn)(int semitone, uint32_t gate_ms, void *ctx);

void arp_init(arp_step_fn fn, void *ctx);

void arp_set_chord(const uint8_t *offsets, uint8_t count);  /* semitone offsets, e.g. {0,4,7} */
void arp_set_dir(arp_dir_t dir);
void arp_set_octaves(uint8_t octaves);   /* clamped 1..ARP_MAX_OCT */
void arp_set_length(uint8_t steps);      /* pattern loop length (e.g. 4 / 8 / 16) */
void arp_set_clock(arp_clk_t src);
void arp_set_bpm(uint16_t bpm);          /* clamped 40..300 */
void arp_set_enabled(bool en);

arp_dir_t arp_dir(void);
uint8_t   arp_octaves(void);
uint8_t   arp_length(void);
arp_clk_t arp_clock(void);
uint16_t  arp_bpm(void);
bool      arp_enabled(void);

/* Internal-clock advance: fires a step when due. No-op if disabled or external. */
void arp_tick(uint32_t now_ms);

/* External-clock step: fires one step per call. No-op if disabled or internal. */
void arp_clock_pulse(uint32_t now_ms);

#endif
