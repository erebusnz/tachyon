#ifndef WT_OSC_H
#define WT_OSC_H

#include <stdint.h>
#include "multisample.h"

/* Single-cycle wavetable oscillator.
 *
 * Plays the loaded multisample as the audio voice: a phase accumulator walks
 * the selected zone's one-cycle table with linear interpolation, at a rate set
 * by the played note. The zone is chosen by the note's key range (the stock
 * multisamples tile the keyboard one octave per zone), which band-limits the
 * timbre per octave. Output pitch comes purely from the requested frequency —
 * phase_inc = f * frames / Fs — so it is independent of the table's recorded
 * rate (only the zone choice depends on the note).
 *
 * Drives the existing audio render seam (audio_set_render); wt_osc_install()
 * makes it the active source. Parameter updates from the main loop are applied
 * under a short critical section; the render runs in the I2S DMA IRQ. */

void wt_osc_init(void);

/* Select the multisample to play. Pointer must remain valid (the PCM lives in
 * the multisample's pool). NULL = nothing loaded (renders silence). */
void wt_osc_set_multisample(const multisample_t *ms);

/* Start a note: pick the zone for `midi_note` and tune to `freq_hz`, gate on. */
void wt_osc_note(int midi_note, float freq_hz);

/* Gate off — render silence (clocks keep running). */
void wt_osc_gate_off(void);

/* Output level, 0..1 (default 1.0). */
void wt_osc_set_level(float level);

/* Install the oscillator as the audio render source. Call after audio_init(). */
void wt_osc_install(void);

#endif
