#ifndef WT_OSC_H
#define WT_OSC_H

#include <stdint.h>
#include <stdbool.h>
#include "multisample.h"

/* Single-cycle wavetable oscillator — polyphonic, with a per-voice envelope
 * filter (filters.md).
 *
 * Plays the loaded multisample as the audio voice: a phase accumulator walks
 * the selected zone's one-cycle table with linear interpolation, at a rate set
 * by the played note. The zone is chosen by the note's key range (the stock
 * multisamples tile the keyboard one octave per zone), which band-limits the
 * timbre per octave. Output pitch comes purely from the requested frequency —
 * phase_inc = f * frames / Fs — so it is independent of the table's recorded
 * rate (only the zone choice depends on the note).
 *
 * With the filter enabled (default), each voice runs wavetable → SVF lowpass
 * → VCA: one ADSR per voice sweeps the filter cutoff AND the voice level, and
 * note-off decays through a click-free release (the voice frees itself when
 * the envelope idles). MIDI velocity drives exactly one destination, selected
 * by wt_osc_set_vel_mode() (filters.md §3.5). With the filter off, the render
 * is the original raw path: notes hard-gate on/off. Envelope/cutoff run at
 * block rate (~1.5 kHz); only the filter state update is per-sample.
 *
 * A pool of WT_MAX_VOICES voices is summed each block. Voices share one master
 * level; the sum is scaled by 1/sqrt(active) so a chord sits near a single
 * voice's loudness without clipping. The monophonic helpers (wt_osc_note /
 * wt_osc_set_pitch) drive voice 0 only and hush the rest, so the arp, key
 * preview, and CV free-run paths behave as the old mono voice did (they use a
 * fixed default velocity of 100); the polyphonic helpers (wt_osc_chord /
 * wt_osc_note_on / wt_osc_note_off) are for USB MIDI and the Chord engine
 * (harmony-engine.md #4) — wt_osc_note_on carries the note's velocity.
 *
 * Drives the existing audio render seam (audio_set_render); wt_osc_install()
 * makes it the active source. Parameter updates from the main loop are applied
 * under a short critical section (single-float parameters are plain atomic
 * stores); the render runs in the I2S DMA IRQ. */

/* Polyphony — voice count. 4 covers a triad with a 7th; the 1/sqrt headroom and
 * the int32 output clamp keep the summed voices within the DAC's range. */
#define WT_MAX_VOICES 4

/* What MIDI velocity modulates — exactly one destination (filters.md §3.5).
 * v = vel/127, S = the configured sustain level. */
typedef enum {
    WT_VEL_VOLUME = 0,   /* voice level × v^1.5 (perceptual curve) */
    WT_VEL_ATK_AMT,      /* the A/D transient above sustain: S + (env−S)·v */
    WT_VEL_ATK_DEC,      /* the whole contour, sustain included: env·v */
    WT_VEL_FLT_ENV,      /* the cutoff sweep depth: 2^(EnvAmt·env·v) */
    WT_VEL_ATK_TIME,     /* attack time × (1−v) — harder = snappier */
    WT_VEL_MODE_COUNT
} wt_vel_mode_t;

void wt_osc_init(void);

/* Select the multisample to play. Pointer must remain valid (the PCM lives in
 * the multisample's pool). NULL = nothing loaded (renders silence). Always a
 * hard cut — the table is going away under the voices. */
void wt_osc_set_multisample(const multisample_t *ms);

/* --- Monophonic (voice 0): the arp / key preview / CV free-run path. --- */

/* Start a note: pick the zone for `midi_note` and tune to `freq_hz`, gate on
 * (retriggers the envelope). Restarts the table phase on a new zone (clean
 * note attack). Hushes any other voices left over from a chord. */
void wt_osc_note(int midi_note, float freq_hz);

/* Continuously update pitch/zone of voice 0 and keep sounding, WITHOUT
 * restarting the phase or retriggering the envelope — for free-run
 * (CV-tracked) operation, so pitch glides click-free. Gates on only if the
 * voice was silent. Hushes any other voices. */
void wt_osc_set_pitch(int midi_note, float freq_hz);

/* --- Polyphonic: USB MIDI / the Chord engine. --- */

/* Play `count` notes at once (clamped to WT_MAX_VOICES), one per voice, each
 * with a clean attack (phase reset). Unused voices are hushed. `midi[i]`
 * picks the zone, `freq[i]` sets the pitch. Default velocity. */
void wt_osc_chord(const int *midi_notes, const float *freq_hz, int count);

/* Start one voice for `midi_note` at velocity `vel` (1..127; routed to the
 * destination picked by wt_osc_set_vel_mode). Reuses the note's own voice on
 * a re-strike, else allocates a free voice, else the quietest releasing one,
 * else steals the oldest. note_off starts the release of the matching
 * voice(s); the voice keeps sounding until the envelope idles (with the
 * filter off: an immediate cut, as before). */
void wt_osc_note_on(int midi_note, float freq_hz, uint8_t vel);
void wt_osc_note_off(int midi_note);

/* Stop everything, fast: ~5 ms anti-click fade with the filter on, hard cut
 * with it off. For panic / mode changes. */
void wt_osc_all_off(void);

/* Gate off — release every voice through its envelope (filter on) or cut
 * (filter off). The arp's per-step gate. */
void wt_osc_gate_off(void);

/* Master output level for all voices, 0..1 (default 1.0). */
void wt_osc_set_level(float level);

/* --- Envelope filter parameters (filters.md §4). Safe from the main loop;
 * edits apply to already-sounding voices at the next block. --- */

/* Enable/disable the filter+envelope. Disabling reverts to the original raw
 * render path. Toggling hard-cuts all sounding notes and resets per-voice
 * envelope/filter state, so neither path inherits the other's. */
void wt_osc_set_filter_enabled(bool en);

void wt_osc_set_cutoff(float hz);          /* base cutoff, 20..20000 */
void wt_osc_set_resonance(float res);      /* 0..1 */
void wt_osc_set_env_amount(float oct);     /* envelope sweep depth, 0..7 oct */
void wt_osc_set_vel_mode(wt_vel_mode_t m); /* velocity destination; sounding
                                              voices are rebaked immediately */
void wt_osc_set_adsr(float a_ms, float d_ms, float sustain, float r_ms);

/* Worst-case wt_render block time since the last call (µs), then resets.
 * DWT cycle counter; for the filters.md §6 CPU budget check. */
uint32_t wt_osc_render_max_us(void);

/* Install the oscillator as the audio render source. Call after audio_init().
 * Also enables the DWT cycle counter used by wt_osc_render_max_us(). */
void wt_osc_install(void);

#endif
