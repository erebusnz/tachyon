#ifndef WT_OSC_H
#define WT_OSC_H

#include <stdint.h>
#include "multisample.h"

/* Single-cycle wavetable oscillator — polyphonic.
 *
 * Plays the loaded multisample as the audio voice: a phase accumulator walks
 * the selected zone's one-cycle table with linear interpolation, at a rate set
 * by the played note. The zone is chosen by the note's key range (the stock
 * multisamples tile the keyboard one octave per zone), which band-limits the
 * timbre per octave. Output pitch comes purely from the requested frequency —
 * phase_inc = f * frames / Fs — so it is independent of the table's recorded
 * rate (only the zone choice depends on the note).
 *
 * A pool of WT_MAX_VOICES voices is summed each block. Voices share one master
 * level; the sum is scaled by 1/sqrt(active) so a chord sits near a single
 * voice's loudness without clipping. The monophonic helpers (wt_osc_note /
 * wt_osc_set_pitch) drive voice 0 only and silence the rest, so the arp, key
 * preview, and CV free-run paths behave exactly as the old mono voice did; the
 * polyphonic helpers (wt_osc_chord / wt_osc_note_on / wt_osc_note_off) are for
 * the Chord engine (harmony-engine.md #4).
 *
 * Drives the existing audio render seam (audio_set_render); wt_osc_install()
 * makes it the active source. Parameter updates from the main loop are applied
 * under a short critical section; the render runs in the I2S DMA IRQ. */

/* Polyphony — voice count. 4 covers a triad with a 7th; the 1/sqrt headroom and
 * the int32 output clamp keep the summed voices within the DAC's range. */
#define WT_MAX_VOICES 4

void wt_osc_init(void);

/* Select the multisample to play. Pointer must remain valid (the PCM lives in
 * the multisample's pool). NULL = nothing loaded (renders silence). */
void wt_osc_set_multisample(const multisample_t *ms);

/* --- Monophonic (voice 0): the arp / key preview / CV free-run path. --- */

/* Start a note: pick the zone for `midi_note` and tune to `freq_hz`, gate on.
 * Restarts the table phase on a new zone (clean note attack). Silences any
 * other voices left over from a chord. */
void wt_osc_note(int midi_note, float freq_hz);

/* Continuously update pitch/zone of voice 0 and keep sounding, WITHOUT
 * restarting the phase — for free-run (CV-tracked) operation, so pitch glides
 * click-free. Silences any other voices. */
void wt_osc_set_pitch(int midi_note, float freq_hz);

/* --- Polyphonic: the Chord engine. --- */

/* Play `count` notes at once (clamped to WT_MAX_VOICES), one per voice, each
 * with a clean attack (phase reset). Unused voices are silenced. `midi[i]`
 * picks the zone, `freq[i]` sets the pitch. */
void wt_osc_chord(const int *midi_notes, const float *freq_hz, int count);

/* Add one sounding voice (allocating a free voice, else stealing the oldest)
 * and release the voice matching `midi_note`. For incremental/MIDI-style use. */
void wt_osc_note_on(int midi_note, float freq_hz);
void wt_osc_note_off(int midi_note);

/* Release every voice (render silence). Clocks keep running. */
void wt_osc_all_off(void);

/* Gate off — alias of wt_osc_all_off(), kept for the mono call sites. */
void wt_osc_gate_off(void);

/* Master output level for all voices, 0..1 (default 1.0). */
void wt_osc_set_level(float level);

/* Install the oscillator as the audio render source. Call after audio_init(). */
void wt_osc_install(void);

#endif
